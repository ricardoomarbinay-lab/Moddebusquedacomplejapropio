#pragma once
#include "Sim.hpp"
#include <chrono>
#include <memory>
#include <random>
#include <vector>

namespace bot {

// Patron de input durante `len` ticks.
//  kind 0 = soltar, 1 = mantener, 2 = tap (1 tick pulsado y luego soltar)
struct Act {
    uint8_t kind = 0;
    uint8_t len = 1;
};
inline bool actBit(Act a, int i) {
    switch (a.kind) {
        case 0: return false;
        case 1: return true;
        default: return i == 0;
    }
}

struct Node {
    std::shared_ptr<Node> parent;
    Snapshot snap;
    Act act{};
    bool held = false;
    bool won = false;
    bool pinned = false;
    uint32_t step = 0;
    float x = 0.f, y = 0.f;
    uint64_t key = 0;
    int clicks = 0;
    int probe = 0;
};
using NodeP = std::shared_ptr<Node>;

class Solver {
public:
    Solver(PlayLayer* pl, Config cfg, uint32_t seed, float beamMul);

    // Trabaja como maximo `budgetMs` milisegundos. Llamar una vez por frame.
    void tick(int budgetMs);

    bool done() const { return m_done; }
    bool failed() const { return m_failed; }
    std::vector<uint8_t> buildPath() const;

    float progress() const;
    int generation() const { return m_gen; }
    size_t beamSize() const { return m_beam.size(); }
    float elapsedSec() const { return m_elapsed; }
    int collapses() const { return m_collapses; }

private:
    enum class Phase { Expand, Probe, Select };

    std::vector<Act> actionSet();
    NodeP expand(NodeP const& n, Act a);
    void expandOne();
    void finishExpand();
    void probeOne();
    void select();
    void collapse();
    void pinAnchor();

    Sim m_sim;
    Config m_cfg;
    std::mt19937 m_rng;
    size_t m_beamMax;

    std::vector<NodeP> m_beam, m_cands;
    std::vector<std::vector<NodeP>> m_anchors;
    NodeP m_win;
    Phase m_phase = Phase::Expand;
    size_t m_i = 0, m_pi = 0;

    int m_gen = 0, m_stuck = 0, m_wide = 0, m_collapses = 0, m_collapseGen = -1000;
    float m_bestX = 0.f, m_levelLen = 1.f;
    float m_elapsed = 0.f;
    bool m_done = false, m_failed = false;
};

} // namespace bot
