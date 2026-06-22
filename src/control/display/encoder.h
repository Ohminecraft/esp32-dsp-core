/**
 * @file encoder.h
 * @brief Quadrature rotary encoder driver (interrupt-based, with push button)
 *
 * Usage:
 *   Encoder enc;
 *   enc.init(PIN_A, PIN_B, PIN_SW);
 *
 *   // In task loop:
 *   EncoderEvent ev;
 *   enc.poll(&ev);
 *   if (ev != EncoderEvent::NONE) { ... }
 *
 * Button events:
 *   SW        — single click (confirmed after DCLICK_WINDOW_MS with no 2nd click)
 *   SW_DOUBLE — double click (2nd press within DCLICK_WINDOW_MS)
 *   SW_HOLD3  — held ≥ 3 s (fires once, on release or at threshold)
 *   SW_HOLD5  — held ≥ 5 s (fires once, supersedes SW_HOLD3)
 */

#pragma once

#include <Arduino.h>
#include <RotaryEncoder.h>
#include "display.h"   // for EncoderEvent

class Encoder {
public:
    /**
     * Initialise encoder on two GPIO pins + switch.
     * Attaches interrupts — call once from setup() or init task.
     *
     * @param pinA  CLK pin  (any GPIO with interrupt support)
     * @param pinB  DT  pin
     * @param sw    Switch pin (active-LOW, internal pull-up)
     */
    void init(uint8_t pinA, uint8_t pinB, uint8_t sw);

    /**
     * Poll for a pending event (non-blocking).
     * Consumes one event per call.
     */
    void poll(EncoderEvent* event);

private:
    uint8_t _sw;

    // ── Button state machine ──────────────────────────────────────────────────
    enum class BtnState : uint8_t {
        IDLE,
        PRESSED,        // button is currently held down
        WAIT_DCLICK,    // released once, waiting to see if a 2nd press arrives
    };

    BtnState     _btnState       = BtnState::IDLE;
    unsigned long _btnPressMs    = 0;   // time of last falling edge (press)
    unsigned long _btnReleaseMs  = 0;   // time of last rising edge (release)
    bool         _holdFired      = false; // prevent repeat hold events

    // Timing constants (ms)
    static constexpr uint32_t DEBOUNCE_MS      = 20;
    static constexpr uint32_t DCLICK_WINDOW_MS = 60; // max gap between two clicks
    static constexpr uint32_t HOLD3_MS         = 3000;
    static constexpr uint32_t HOLD5_MS         = 5000;

    // Queued button event (one slot; processed next poll())
    EncoderEvent _pendingBtn = EncoderEvent::NONE;
};