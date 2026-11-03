#include "Solver.hpp"
#include <algorithm>
#include <unordered_map>

using namespace geode::prelude;
using clk = std::chrono::steady_clock;

namespace bot {

static constexpr int kAnchorEvery = 40;   // generaciones entre anclas
static constexpr int kAnchorKeep = 6;     // nodos por ancla
static constexpr size_t kMaxAnchors = 14;

Solver::Solver(PlayLayer* pl, Config cfg, uint32_t seed, float beamMul)
    : m_sim(pl), m_cfg(cfg), m_rng(seed) {
    m_beamMax = std::max<size_t>(4, (size_t)(cfg.beam * beamMul));
    m_levelLen = std::max(1.f, pl->m_levelLength);

    g.simulating = true;
    g.simDied = g.simWon = false;

    auto root = std::make_shared<Node>();
    root->snap = m_sim.save();
    root->held = false;
    root->x = m_sim.x();
    root->y = m_sim.y();
    root->key = m_sim.stateKey(false);
    root->pinned = true;
    m_beam.push_back(root);
    m_anchors.push_back({root});
    g.simulating = false;
}

float Solver::progress() const { return std::clamp(m_bestX / m_levelLen, 0.f, 1.f); }

std::vector<Act> Solver::actionSet() {
    uint8_t s = (uint8_t)m_cfg.stride;
    std::vector<Act> v = {{0, s}, {1, s}, {2, s}};
    if (m_wide > 0) {
        v.push_back({0, (uint8_t)(s * 2)});
        v.push_back({1, (uint8_t)(s * 2)});
        v.push_back({0, (uint8_t)(s * 4)});
        v.push_back({1, (uint8_t)(s * 4)});
        std::uniform_int_distribution<int> k(0, 2), l(1, 8 * m_wide);
        v.push_back({(uint8_t)k(m_rng), (uint8_t)l(m_rng)});
        v.push_back({(uint8_t)k(m_rng), (uint8_t)l(m_rng)});
    }
    return v;
}

NodeP Solver::expand(NodeP const& n, Act a) {
    m_sim.load(n->snap, n->held);
    g.simDied = g.simWon = false;
    bool held = n->held;
    int clicks = n->clicks;

    for (int i = 0; i < a.len; ++i) {
        bool want = actBit(a, i);
        if (want && !held) ++clicks;
        m_sim.setHeld(want);
        held = want;
        if (!m_sim.step()) return nullptr;

        bool past = m_sim.x() > m_levelLen + 300.f;
        if (g.simWon || past) {
            auto c = std::make_shared<Node>();
            c->parent = n; c->act = {a.kind, (uint8_t)(i + 1)};
            c->held = held; c->won = true; c->step = n->step + i + 1;
            return c;
        }
    }
    auto c = std::make_shared<Node>();
    c->parent = n;
    c->act = a;
    c->held = held;
    c->clicks = clicks;
    c->step = n->step + a.len;
    c->x = m_sim.x();
    c->y = m_sim.y();
    c->key = m_sim.stateKey(held);
    c->snap = m_sim.save();
    return c;
}

void Solver::expandOne() {
    auto const& n = m_beam[m_i];
    for (auto a : actionSet()) {
        auto c = expand(n, a);
        if (!c) continue;
        if (c->won) { m_win = c; m_done = true; return; }
        m_cands.push_back(std::move(c));
    }
    if (++m_i >= m_beam.size()) finishExpand();
}

void Solver::finishExpand() {
    if (m_cands.empty()) { collapse(); return; }

    // Deduplicar por clave de estado: se queda el de menos clicks.
    std::unordered_map<uint64_t, NodeP> best;
    for (auto& c : m_cands) {
        auto it = best.find(c->key);
        if (it == best.end() || c->clicks < it->second->clicks) best[c->key] = c;
    }
    m_cands.clear();
    for (auto& [k, v] : best) m_cands.push_back(v);

    if (m_cands.size() > m_beamMax && m_cfg.probe > 0) {
        m_phase = Phase::Probe;
        m_pi = 0;
    } else {
        m_phase = Phase::Select;
    }
}

// Sonda: sobrevive el candidato `probe` ticks si mantiene / si suelta?
void Solver::probeOne() {
    auto& c = m_cands[m_pi];
    int surv = 0;
    for (bool pol : {false, true}) {
        m_sim.load(c->snap, c->held);
        g.simDied = g.simWon = false;
        bool alive = true;
        for (int s = 0; s < m_cfg.probe; ++s) {
            m_sim.setHeld(pol);
            if (!m_sim.step()) { alive = false; break; }
            if (g.simWon) break;
        }
        surv += alive;
    }
    c->probe = surv;
    if (++m_pi >= m_cands.size()) m_phase = Phase::Select;
}

void Solver::select() {
    std::shuffle(m_cands.begin(), m_cands.end(), m_rng);
    std::stable_sort(m_cands.begin(), m_cands.end(), [](NodeP const& a, NodeP const& b) {
        if (a->probe != b->probe) return a->probe > b->probe;
        return a->x > b->x;
    });

    std::vector<NodeP> chosen;
    if (m_cands.size() <= m_beamMax) {
        chosen = m_cands;
    } else {
        // Reparto por cubetas de altura para conservar diversidad.
        std::unordered_map<int, int> perBucket;
        for (auto& c : m_cands) perBucket[(int)std::floor(c->y / 30.f)]++;
        size_t cap = std::max<size_t>(2, m_beamMax / std::max<size_t>(1, perBucket.size()) + 1);
        std::unordered_map<int, size_t> used;
        std::vector<char> taken(m_cands.size(), 0);
        for (size_t i = 0; i < m_cands.size() && chosen.size() < m_beamMax; ++i) {
            int b = (int)std::floor(m_cands[i]->y / 30.f);
            if (used[b] < cap) { chosen.push_back(m_cands[i]); used[b]++; taken[i] = 1; }
        }
        for (size_t i = 0; i < m_cands.size() && chosen.size() < m_beamMax; ++i)
            if (!taken[i]) chosen.push_back(m_cands[i]);
    }

    // Liberar snapshots de la generacion anterior (salvo anclas).
    for (auto& n : m_beam) if (!n->pinned) n->snap = {};
    m_cands.clear();
    m_beam = std::move(chosen);
    ++m_gen;
    for (auto& n : m_beam) m_bestX = std::max(m_bestX, n->x);

    if (m_gen % kAnchorEvery == 0) pinAnchor();
    if (m_wide > 0 && m_gen > m_collapseGen + kAnchorEvery * 2) { m_wide = 0; m_stuck = 0; }

    m_i = 0;
    m_phase = Phase::Expand;
}

void Solver::pinAnchor() {
    std::vector<NodeP> a;
    for (size_t i = 0; i < m_beam.size() && (int)a.size() < kAnchorKeep; ++i) {
        m_beam[i]->pinned = true;
        a.push_back(m_beam[i]);
    }
    m_anchors.push_back(std::move(a));
    if (m_anchors.size() > kMaxAnchors) {
        for (auto& n : m_anchors.front()) {
            bool inBeam = std::find(m_beam.begin(), m_beam.end(), n) != m_beam.end();
            if (!inBeam) { n->pinned = false; n->snap = {}; }
        }
        m_anchors.erase(m_anchors.begin());
    }
}

// El haz murio por completo: retroceder a un ancla y ensanchar la busqueda.
void Solver::collapse() {
    ++m_collapses;
    ++m_stuck;
    m_collapseGen = m_gen;
    m_wide = std::min(m_stuck, 6);

    if (m_anchors.empty()) { m_failed = m_done = true; return; }
    size_t back = std::min<size_t>(m_stuck, m_anchors.size());
    size_t idx = m_anchors.size() - back;

    // Tras muchos colapsos seguidos sin avanzar, ensanchar tambien el haz.
    if (m_stuck > 8 && m_beamMax < 512) m_beamMax = (size_t)(m_beamMax * 1.5);
    if (m_stuck > 40) { m_failed = m_done = true; return; }

    for (auto& n : m_beam) if (!n->pinned) n->snap = {};
    m_beam = m_anchors[idx];
    m_anchors.resize(idx + 1);
    m_gen = std::max(0, m_gen - (int)(back * kAnchorEvery));
    m_cands.clear();
    m_i = 0;
    m_phase = Phase::Expand;
}

void Solver::tick(int budgetMs) {
    if (m_done) return;
    auto t0 = clk::now();
    g.simulating = true;
    while (!m_done) {
        switch (m_phase) {
            case Phase::Expand: expandOne(); break;
            case Phase::Probe:  probeOne();  break;
            case Phase::Select: select();    break;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
        if (ms >= budgetMs) break;
    }
    g.simulating = false;
    m_elapsed += std::chrono::duration<float>(clk::now() - t0).count();

    if (!m_done && m_elapsed > (float)m_cfg.maxSeconds) m_failed = m_done = true;
}

std::vector<uint8_t> Solver::buildPath() const {
    std::vector<Act> acts;
    for (Node* n = m_win.get(); n && n->parent; n = n->parent.get()) acts.push_back(n->act);
    std::reverse(acts.begin(), acts.end());
    std::vector<uint8_t> path;
    for (auto a : acts)
        for (int i = 0; i < a.len; ++i) path.push_back(actBit(a, i) ? 1 : 0);
    return path;
}

} // namespace bot
