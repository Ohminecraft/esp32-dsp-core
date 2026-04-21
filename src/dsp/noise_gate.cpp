/**
 * @file noise_gate.cpp
 * @brief Noise Gate implementation with state machine
 */

#include "noise_gate.h"

void NoiseGate::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    // Default: fairly aggressive gate
    _lowerThreshDb = -6000;   // -60 dB
    _upperThreshDb = -4000;   // -40 dB
    _attackMs  = 5;
    _releaseMs = 50;
    _holdMs    = 100;

    recalcCoeffs();
    reset();
}

void IRAM_ATTR NoiseGate::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        size_t idx = i * _numChannels;

        // Detect peak level across channels
        q31_t peak = 0;
        for (int ch = 0; ch < _numChannels; ch++) {
            q31_t absVal = q31_abs(samples[idx + ch]);
            if (absVal > peak) peak = absVal;
        }

        // Smooth envelope follower
        q31_t envCoeff = (peak > _envelope) ? _attackCoeff : _releaseCoeff;
        _envelope = envelope_follow(_envelope, peak, envCoeff);

        // State machine
        switch (_state) {
            case GATE_CLOSED:
                if (_envelope > _upperThreshLin) {
                    _state = GATE_ATTACK;
                }
                break;

            case GATE_ATTACK:
                _gain += _attackGainStep;
                if (_gain >= Q31_MAX) {
                    _gain = Q31_MAX;
                    _state = GATE_OPEN;
                }
                break;

            case GATE_OPEN:
                if (_envelope < _lowerThreshLin) {
                    _holdCounter = _holdSamples;
                    _state = GATE_HOLD;
                }
                break;

            case GATE_HOLD:
                _holdCounter--;
                if (_holdCounter <= 0) {
                    _state = GATE_RELEASE;
                }
                // If signal comes back during hold, go back to open
                if (_envelope > _upperThreshLin) {
                    _state = GATE_OPEN;
                }
                break;

            case GATE_RELEASE:
                _gain -= _releaseGainStep;
                if (_gain <= 0) {
                    _gain = 0;
                    _state = GATE_CLOSED;
                }
                // If signal comes back during release, re-attack
                if (_envelope > _upperThreshLin) {
                    _state = GATE_ATTACK;
                }
                break;
        }

        // Apply gate gain
        if (_gain == Q31_MAX) {
            // Fully open — no processing needed
        } else if (_gain == 0) {
            // Fully closed — zero output
            for (int ch = 0; ch < _numChannels; ch++) {
                samples[idx + ch] = 0;
            }
        } else {
            // Transition — apply gain
            for (int ch = 0; ch < _numChannels; ch++) {
                samples[idx + ch] = q31_mul(samples[idx + ch], _gain);
            }
        }
    }
}

void NoiseGate::reset() {
    _state = GATE_CLOSED;
    _envelope = 0;
    _gain = 0;
    _holdCounter = 0;
}

void NoiseGate::setLowerThreshold(int32_t db_001) {
    _lowerThreshDb = db_001;
    recalcCoeffs();
}

void NoiseGate::setUpperThreshold(int32_t db_001) {
    _upperThreshDb = db_001;
    recalcCoeffs();
}

void NoiseGate::setAttackTime(int32_t ms) {
    _attackMs = ms;
    recalcCoeffs();
}

void NoiseGate::setReleaseTime(int32_t ms) {
    _releaseMs = ms;
    recalcCoeffs();
}

void NoiseGate::setHoldTime(int32_t ms) {
    _holdMs = ms;
    recalcCoeffs();
}

void NoiseGate::recalcCoeffs() {
    _lowerThreshLin = db_to_q31_gain(_lowerThreshDb);
    _upperThreshLin = db_to_q31_gain(_upperThreshDb);

    // Envelope follower coefficients (fast attack, slower release for detection)
    int32_t detAttack = 1;   // 1ms for peak detection
    int32_t detRelease = 10; // 10ms for release detection
    _attackCoeff  = calc_envelope_coeff(_sampleRate, detAttack);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, detRelease);

    // Hold time in samples
    _holdSamples = (_sampleRate * _holdMs) / 1000;

    // Gain ramp: over attack/release time
    int32_t attackSamples  = (_sampleRate * _attackMs) / 1000;
    int32_t releaseSamples = (_sampleRate * _releaseMs) / 1000;

    _attackGainStep  = (attackSamples > 0)  ? (Q31_MAX / attackSamples)  : Q31_MAX;
    _releaseGainStep = (releaseSamples > 0) ? (Q31_MAX / releaseSamples) : Q31_MAX;
}
