/**
 * @file encoder.cpp
 * @brief Quadrature rotary encoder driver implementation
 */

#include "encoder.h"

RotaryEncoder* encoder = nullptr;

void IRAM_ATTR checkPosition() {
    encoder->tick();
}

void Encoder::init(uint8_t pinA, uint8_t pinB, uint8_t sw) {
    _sw = sw;
    pinMode(sw, INPUT_PULLUP);

    encoder = new RotaryEncoder(pinA, pinB, RotaryEncoder::LatchMode::FOUR3);
    attachInterrupt(digitalPinToInterrupt(pinA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pinB), checkPosition, CHANGE);
}

void Encoder::poll(EncoderEvent* event) {
    // ── Rotary position tracking ──────────────────────────────────────────────
    static int           lastPos             = 0;
    static int           posDifference       = 0;
    static unsigned long lastEncoderMoveMs   = 0;
    static unsigned long lastRotaryEventMs   = 0; // throttle: gap after button

    unsigned long now = millis();
    int newPos = encoder->getPosition();

    if (newPos != lastPos) {
        posDifference += (newPos - lastPos);
        lastPos        = newPos;
        lastEncoderMoveMs = now;
    } else if (posDifference != 0 && now - lastEncoderMoveMs > 3) { 
        // Stale queued steps — drop them
        posDifference = 0;
    }

    // ── Deliver any queued button event first ─────────────────────────────────
    if (_pendingBtn != EncoderEvent::NONE) {
        *event       = _pendingBtn;
        _pendingBtn  = EncoderEvent::NONE;
        posDifference = 0;          // discard accidental rotation during press
        lastRotaryEventMs = now;
        return;
    }

    // ── Button state machine ──────────────────────────────────────────────────
    bool pressed = (digitalRead(_sw) == LOW);

    switch (_btnState) {

    case BtnState::IDLE:
        if (pressed) {
            if (now - _btnReleaseMs < DEBOUNCE_MS) break; // glitch
            _btnState   = BtnState::PRESSED;
            _btnPressMs = now;
            _holdFired  = false;
        }
        break;

    case BtnState::PRESSED:
        if (!pressed) {
            // Released — decide single vs first-of-double
            unsigned long holdDur = now - _btnPressMs;
            if (holdDur >= HOLD5_MS) {
                // Hold-5 fires on release (we already fired it while held, see below)
                // No-op here: event was already queued while pressed
            } else if (holdDur >= HOLD3_MS) {
                // Hold-3 fires on release if not already fired
                if (!_holdFired) {
                    _pendingBtn = EncoderEvent::SW_HOLD3;
                    _holdFired  = true;
                }
            } else {
                // Normal release — wait to see if double-click follows
                _btnState      = BtnState::WAIT_DCLICK;
                _btnReleaseMs  = now;
            }
            if (_btnState == BtnState::PRESSED) _btnState = BtnState::IDLE;
        } else {
            // Still held — check hold thresholds (fire once)
            unsigned long holdDur = now - _btnPressMs;
            if (!_holdFired && holdDur >= HOLD5_MS) {
                _pendingBtn = EncoderEvent::SW_HOLD5;
                _holdFired  = true;
            } else if (!_holdFired && holdDur >= HOLD3_MS) {
                _pendingBtn = EncoderEvent::SW_HOLD3;
                _holdFired  = true;
            }
        }
        break;

    case BtnState::WAIT_DCLICK:
        if (pressed && (now - _btnReleaseMs >= DEBOUNCE_MS)) {
            // Second press arrived → double click
            _pendingBtn = EncoderEvent::SW_DOUBLE;
            _btnState   = BtnState::IDLE;
            // Wait for release (ignore it)
            while (digitalRead(_sw) == LOW) { /* busy-wait short release */ }
        } else if (now - _btnReleaseMs >= DCLICK_WINDOW_MS) {
            // Window expired — confirm single click
            _pendingBtn = EncoderEvent::SW;
            _btnState   = BtnState::IDLE;
        }
        break;
    }

    // ── Deliver rotation events ───────────────────────────────────────────────
    // Suppress rotation briefly after a button event to avoid mis-clicks
    if (now - lastRotaryEventMs < 30) { 
        *event = EncoderEvent::NONE;
        return;
    }

    if (posDifference > 0) {
        *event = EncoderEvent::CW;
        posDifference--;
        lastRotaryEventMs = now;
    } else if (posDifference < 0) {
        *event = EncoderEvent::CCW;
        posDifference++;
        lastRotaryEventMs = now;
    } else {
        *event = EncoderEvent::NONE;
    }
}