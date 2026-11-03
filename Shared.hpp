#pragma once
#include <Geode/Geode.hpp>
#include <cstdint>

namespace bot {

enum class Mode { Idle, Solving, Playing };

// Estado global compartido entre hooks, simulador y solver.
struct Shared {
    bool simulating = false;   // true mientras el solver avanza el juego
    bool simDied = false;      // un destroyPlayer fue interceptado en simulacion
    bool simWon = false;       // un levelComplete fue interceptado en simulacion
    uint32_t stepCounter = 0;  // pasos de fisica (processCommands) desde el reset
    Mode mode = Mode::Idle;
};
inline Shared g;

// El juego (2.2) corre la fisica a 240 pasos/seg.
constexpr float kStepDt = 1.f / 240.f;

struct Config {
    bool enabled;
    int beam, stride, probe, maxSeconds, sliceMs;

    static Config load() {
        auto* m = geode::Mod::get();
        Config c;
        c.enabled    = m->getSettingValue<bool>("enabled");
        c.beam       = (int)m->getSettingValue<int64_t>("beam-width");
        c.stride     = (int)m->getSettingValue<int64_t>("stride");
        c.probe      = (int)m->getSettingValue<int64_t>("probe-steps");
        c.maxSeconds = (int)m->getSettingValue<int64_t>("max-seconds");
        c.sliceMs    = (int)m->getSettingValue<int64_t>("slice-ms");
        return c;
    }
};

} // namespace bot
