#pragma once
#include "Shared.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/CheckpointObject.hpp>

namespace bot {

// Instantanea del estado del juego (reutiliza el sistema de checkpoints del modo practica).
struct Snapshot {
    geode::Ref<CheckpointObject> cp;
    explicit operator bool() const { return cp != nullptr; }
};

// Envoltorio sobre PlayLayer: guardar/restaurar, inyectar input y avanzar 1 tick.
class Sim {
public:
    explicit Sim(PlayLayer* pl) : m_pl(pl) {}

    Snapshot save() {
        Snapshot s;
        s.cp = m_pl->createCheckpoint();
        return s;
    }

    void load(Snapshot const& s, bool held) {
        m_pl->loadFromCheckpoint(s.cp);
        forceHeld(held);
    }

    // Fija el estado interno de "boton mantenido" SIN generar un flanco (click).
    // ATENCION: dependiente de los bindings de tu version de Geode/GD.
    void forceHeld(bool h) {
        m_held = h;
        m_pl->m_player1->m_holdingButtons[1] = h;
        if (m_pl->m_player2) m_pl->m_player2->m_holdingButtons[1] = h;
    }

    // Genera flancos de pulsar/soltar solo cuando el estado cambia.
    void setHeld(bool want) {
        if (want == m_held) return;
        auto btn = static_cast<PlayerButton>(1); // Jump
        if (want) m_pl->pushButton(btn, true);
        else      m_pl->releaseButton(btn, true);
        m_held = want;
    }

    bool held() const { return m_held; }

    // Avanza exactamente un paso de fisica. Devuelve false si el jugador murio.
    bool step() {
        uint32_t before = g.stepCounter;
        for (int i = 0; i < 4 && g.stepCounter == before; ++i)
            m_pl->update(kStepDt);
        return !g.simDied;
    }

    PlayLayer* layer() const { return m_pl; }
    float x() const { return m_pl->m_player1->getPositionX(); }
    float y() const { return m_pl->m_player1->getPositionY(); }

    // Clave cuantizada del estado, para deduplicar candidatos equivalentes.
    uint64_t stateKey(bool held) const {
        auto* p = m_pl->m_player1;
        uint64_t k = 0;
        auto push = [&](uint64_t v, int bits) { k = (k << bits) | (v & ((1ull << bits) - 1)); };
        push(p->m_isShip, 1);   push(p->m_isBird, 1);  push(p->m_isBall, 1);
        push(p->m_isDart, 1);   push(p->m_isRobot, 1); push(p->m_isSpider, 1);
        push(p->m_isSwing, 1);  push(p->m_isUpsideDown, 1);
        push(p->m_isOnGround, 1); push(held, 1);
        bool dual = m_pl->m_gameState.m_isDualMode;
        push(dual, 1);
        push((uint64_t)(int64_t)std::floor(p->getPositionY() / 2.f), 10);
        push((uint64_t)(int64_t)std::floor(p->m_yVelocity * 2.0), 10);
        if (dual && m_pl->m_player2)
            push((uint64_t)(int64_t)std::floor(m_pl->m_player2->getPositionY() / 2.f), 10);
        return k;
    }

private:
    PlayLayer* m_pl;
    bool m_held = false;
};

} // namespace bot
