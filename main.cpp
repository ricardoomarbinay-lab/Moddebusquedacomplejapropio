#include "Shared.hpp"
#include "Solver.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

namespace bot {
// Reproduccion de la ruta calculada.
struct Playback {
    std::vector<uint8_t> path;
    bool held = false;
    void reset() { held = false; }
};
inline Playback playback;
} // namespace bot

// ---------------------------------------------------------------------------
// Silenciar efectos de sonido mientras se simula (miles de saltos/orbs por segundo)
// ---------------------------------------------------------------------------
class $modify(BotAudio, FMODAudioEngine) {
    int playEffect(gd::string path, float speed, float p2, float volume) {
        if (bot::g.simulating) return 0;
        return FMODAudioEngine::playEffect(path, speed, p2, volume);
    }
};

// ---------------------------------------------------------------------------
// Contador de pasos de fisica + inyeccion de input en reproduccion
// ---------------------------------------------------------------------------
class $modify(BotGJBase, GJBaseGameLayer) {
    void processCommands(float dt) {
        bot::g.stepCounter++;
        if (bot::g.mode == bot::Mode::Playing && !bot::g.simulating) {
            size_t idx = bot::g.stepCounter - 1;
            bool want = idx < bot::playback.path.size() ? bot::playback.path[idx] != 0 : false;
            auto btn = static_cast<PlayerButton>(1);
            if (want && !bot::playback.held)       this->pushButton(btn, true);
            else if (!want && bot::playback.held)  this->releaseButton(btn, true);
            bot::playback.held = want;
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

// ---------------------------------------------------------------------------
// PlayLayer: orquestacion (armar -> resolver -> reproducir -> reintentar)
// ---------------------------------------------------------------------------
class $modify(BotPlayLayer, PlayLayer) {
    struct Fields {
        std::unique_ptr<bot::Solver> solver;
        Ref<CCLayerColor> overlay;
        CCLabelBMFont* title = nullptr;
        CCLabelBMFont* info = nullptr;
        bool armed = false;
        bool gaveUp = false;
        CCLabelBMFont* status = nullptr;
        std::string lastStatus;
        bool savedPractice = false;
        int attempt = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        bot::g = {};
        bot::playback = {};
        // Armar desde el inicio: no depender de que resetLevel se llame despues de init.
        m_fields->armed = true;
        geode::log::info("PathBot: nivel cargado, bot armado");
        return true;
    }

    void buildOverlay() {
        auto win = CCDirector::get()->getWinSize();
        auto f = m_fields.self();
        f->overlay = CCLayerColor::create({0, 0, 0, 235}, win.width, win.height);
        f->overlay->setZOrder(99999);
        f->title = CCLabelBMFont::create("PathBot: buscando camino...", "bigFont.fnt");
        f->title->setPosition({win.width / 2, win.height / 2 + 40});
        f->title->setScale(0.8f);
        f->info = CCLabelBMFont::create("", "chatFont.fnt");
        f->info->setPosition({win.width / 2, win.height / 2 - 10});
        f->overlay->addChild(f->title);
        f->overlay->addChild(f->info);
        this->addChild(f->overlay);
    }

    void removeOverlay() {
        auto f = m_fields.self();
        if (f->overlay) { f->overlay->removeFromParent(); f->overlay = nullptr; }
    }

    void startSolving() {
        auto f = m_fields.self();
        auto cfg = bot::Config::load();

        // loadFromCheckpoint puede requerir el flag de practica; se restaura despues.
        f->savedPractice = m_isPracticeMode;
        m_isPracticeMode = true;
        // Siempre en modo test: el bot NO registra completados ni estadisticas.
        m_isTestMode = true;

        float mul = (float)(1 << std::min(f->attempt, 3));
        f->solver = std::make_unique<bot::Solver>(this, cfg, 1337u + f->attempt * 7919u, mul);
        bot::g.mode = bot::Mode::Solving;
        buildOverlay();
        geode::log::info("PathBot: empezando busqueda (intento {})", f->attempt + 1);
    }

    void finishSolving() {
        auto f = m_fields.self();
        bool failed = f->solver->failed();
        auto path = failed ? std::vector<uint8_t>{} : f->solver->buildPath();
        f->solver.reset();
        m_isPracticeMode = f->savedPractice;
        removeOverlay();

        if (failed || path.empty()) {
            bot::g.mode = bot::Mode::Idle;
            m_fields->gaveUp = true;
            Notification::create("PathBot: no se encontro camino (sube el ancho del haz)",
                                 NotificationIcon::Error)->show();
            return;
        }
        bot::playback.path = std::move(path);
        bot::playback.reset();
        bot::g.mode = bot::Mode::Playing;
        Notification::create("PathBot: camino encontrado, reproduciendo",
                             NotificationIcon::Success)->show();
        this->resetLevel();
    }

    void updateOverlay() {
        auto f = m_fields.self();
        if (!f->solver || !f->info) return;
        float p = f->solver->progress();
        float el = f->solver->elapsedSec();
        float eta = p > 0.01f ? el * (1.f - p) / p : 0.f;
        f->info->setString(fmt::format(
            "Progreso {:.1f}%  |  Gen {}  |  Haz {}  |  Colapsos {}\nTranscurrido {:.0f}s  |  ETA ~{:.0f}s  |  Intento {}",
            p * 100.f, f->solver->generation(), f->solver->beamSize(),
            f->solver->collapses(), el, eta, f->attempt + 1).c_str());
    }

    void refreshStatus(bool enabled) {
        auto f = m_fields.self();
        if (!f->status) {
            auto win = CCDirector::get()->getWinSize();
            f->status = CCLabelBMFont::create("", "bigFont.fnt");
            f->status->setScale(0.35f);
            f->status->setAnchorPoint({0.f, 1.f});
            f->status->setPosition({6.f, win.height - 6.f});
            f->status->setZOrder(100000);
            this->addChild(f->status);
        }
        std::string t;
        if (!enabled) t = "PathBot: DESACTIVADO (actívalo en ajustes)";
        else if (bot::g.mode == bot::Mode::Solving) t = "PathBot: buscando camino...";
        else if (bot::g.mode == bot::Mode::Playing) t = "PathBot: reproduciendo";
        else if (f->gaveUp) t = "PathBot: no encontró camino";
        else t = "PathBot: ACTIVADO (esperando)";
        if (t != f->lastStatus) { f->lastStatus = t; f->status->setString(t.c_str()); }
    }

    void update(float dt) {
        // Durante la simulacion el juego avanza con normalidad.
        if (bot::g.simulating) { PlayLayer::update(dt); return; }

        auto cfg = bot::Config::load();
        auto f = m_fields.self();
        refreshStatus(cfg.enabled);

        if (cfg.enabled && f->armed && !f->gaveUp && bot::g.mode == bot::Mode::Idle
            && m_player1 && !m_player1->m_isDead) {
            f->armed = false;
            startSolving();
        }

        if (bot::g.mode == bot::Mode::Solving && f->solver) {
            f->solver->tick(cfg.sliceMs);
            updateOverlay();
            if (f->solver->done()) finishSolving();
            return; // el juego "real" queda congelado bajo el overlay
        }

        PlayLayer::update(dt);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (bot::g.simulating) return;
        bot::g.stepCounter = 0;
        bot::playback.reset();
        if (bot::g.mode == bot::Mode::Idle && bot::Config::load().enabled && !m_fields->gaveUp)
            m_fields->armed = true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (bot::g.simulating) { bot::g.simDied = true; return; }
        PlayLayer::destroyPlayer(player, obj);
        // Murio durante la reproduccion: la fisica divergio -> resolver otra vez, mas ancho.
        if (bot::g.mode == bot::Mode::Playing) {
            bot::g.mode = bot::Mode::Idle;
            m_fields->attempt++;
            m_fields->armed = true;
        }
    }

    void levelComplete() {
        if (bot::g.simulating) { bot::g.simWon = true; return; }
        PlayLayer::levelComplete();
        bot::g.mode = bot::Mode::Idle;
        m_fields->armed = false;
    }

    void onQuit() {
        removeOverlay();
        m_fields->solver.reset();
        bot::g = {};
        bot::playback = {};
        PlayLayer::onQuit();
    }
};
