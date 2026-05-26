/**
 * @file compander.cpp
 * @brief Compander implementation — optimized per MVSilicon SDK patterns
 *
 * Key optimizations over previous version:
 *   - Pre-computed slopes (1 - 1/R) in recalcCoeffs(), not per-sample
 *   - Pregain applied BEFORE peak detection (matching SDK behavior)
 *   - fast_linear_to_db() instead of log10f() — ~200 cycles → ~5 cycles/sample
 *   - fast_db_to_gain() instead of powf(10,..) — ~100 cycles → ~8 cycles/sample
 *   - Decimated dB computation every 8 samples (same pattern as Dynamic Bass)
 */

#include "compander.h"
#include <math.h>

// Decimate expensive dB+gain computation every N samples
static constexpr int COMP_DECIM = 8;

void Compander::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _thresholdDbInt = -2000;
    _ratioBelowQ88 = 100;  // 1.00:1, SDK/UI 0.01 steps
    _ratioAboveQ88 = 400;  // 4.00:1
    _attackMs = 10;
    _releaseMs = 100;
    _pregainQ412 = 4096;   // 1.0 (0 dB)

    recalcCoeffs();
    reset();
}

void IRAM_ATTR Compander::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    float envelope   = _state.envelope;
    float gainLinear = _state.gainLinear;

    const float pregain      = _pregain;
    const float thresholdDb  = _thresholdDb;
    const float slopeAbove   = _slopeAbove;
    const float slopeBelow   = _slopeBelow;
    const float attackCoeff  = _attackCoeff;
    const float releaseCoeff = _releaseCoeff;

    for (size_t i = 0; i < numSamples; i++) {
        const int base = (int)(i * _numChannels);

        // ── 1. Pregain + peak detection ──
        float peak = 0.0f;
        for (int ch = 0; ch < _numChannels; ch++) {
            float s = samples[base + ch] * pregain;
            float a = fast_abs(s);
            if (a > peak) peak = a;
        }

        // ── 2. Envelope follower ──
        const float coeff = (peak > envelope) ? attackCoeff : releaseCoeff;
        envelope = envelope_follow(envelope, peak, coeff);

        // ── 3. Decimated gain computation ──
        if (++_state.decimCount >= COMP_DECIM) {
            _state.decimCount = 0;
            const float envDb = fast_linear_to_db(envelope);
            float gainDb = 0.0f;

            if (envDb > thresholdDb) {
                gainDb = (thresholdDb - envDb) * slopeAbove;
            } else {
                gainDb = (thresholdDb - envDb) * slopeBelow;
            }

            gainLinear = fast_db_to_gain(gainDb);
        }

        // ── 4. Apply gain ──
        const float totalGain = pregain * gainLinear;
        for (int ch = 0; ch < _numChannels; ch++) {
            samples[base + ch] *= totalGain;
        }
    }

    // ── 5. Persist state ──
    _state.envelope = envelope;
    _state.gainLinear = gainLinear;
}

void Compander::reset() {
    _state.reset();
}

void Compander::recalcCoeffs() {
    _thresholdDb = (float)_thresholdDbInt / 100.0f;

    // Ratio format matches the control UI and SDK-style storage:
    // 100 = 1.00:1, 400 = 4.00:1.
    _slopeAbove = DynamicsProcessor::ratioToSlope(_ratioAboveQ88);
    _slopeBelow = DynamicsProcessor::ratioToSlope(_ratioBelowQ88);

    _pregain = (float)_pregainQ412 / 4096.0f;
    _attackCoeff  = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);
}

void Compander::setThreshold(int32_t db_001)     { _thresholdDbInt = db_001; recalcCoeffs(); }
void Compander::setRatioBelow(int32_t ratio_q88) { _ratioBelowQ88 = (ratio_q88 < 10) ? 100 : ratio_q88; recalcCoeffs(); }
void Compander::setRatioAbove(int32_t ratio_q88) { _ratioAboveQ88 = (ratio_q88 < 100) ? 100 : ratio_q88; recalcCoeffs(); }
void Compander::setAttackTime(int32_t ms)        { _attackMs = (ms < 1) ? 1 : ms; recalcCoeffs(); }
void Compander::setReleaseTime(int32_t ms)       { _releaseMs = (ms < 1) ? 1 : ms; recalcCoeffs(); }
void Compander::setPregain(int32_t gain_q412)    { _pregainQ412 = (gain_q412 < 1) ? 4096 : gain_q412; recalcCoeffs(); }
