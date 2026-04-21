/**
 * @file compander.cpp
 * @brief Compander implementation
 */

#include "compander.h"
#include <math.h>

void Compander::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _thresholdDb = -2000;   // -20 dB
    _ratioBelow  = 100;     // 1:1 (neutral below)
    _ratioAbove  = 400;     // 4:1 (compression above)
    _attackMs    = 10;
    _releaseMs   = 100;
    _pregainQ412 = PREGAIN_Q412_UNITY;

    recalcCoeffs();
    reset();
}

void IRAM_ATTR Compander::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        size_t idx = i * _numChannels;

        // Apply pregain
        if (_pregainLin != Q31_MAX) {
            for (int ch = 0; ch < _numChannels; ch++) {
                samples[idx + ch] = q31_mul(samples[idx + ch], _pregainLin);
            }
        }

        // Peak detection across channels
        q31_t peak = 0;
        for (int ch = 0; ch < _numChannels; ch++) {
            q31_t absVal = q31_abs(samples[idx + ch]);
            if (absVal > peak) peak = absVal;
        }

        // Envelope follower (attack/release)
        q31_t coeff = (peak > _envelope) ? _attackCoeff : _releaseCoeff;
        _envelope = envelope_follow(_envelope, peak, coeff);

        // Compute gain based on envelope vs threshold
        q31_t gain = computeGain(_envelope);

        // Apply gain to all channels
        if (gain != Q31_MAX) {
            for (int ch = 0; ch < _numChannels; ch++) {
                samples[idx + ch] = q31_mul(samples[idx + ch], gain);
            }
        }
    }
}

q31_t IRAM_ATTR Compander::computeGain(q31_t envelope) {
    if (envelope <= 0) return Q31_MAX;  // Silent — no change

    // Pure fixed-point gain computation (no float!)
    int32_t envDb_q16 = q31_to_db_q16(envelope);
    int32_t overDb_q16 = envDb_q16 - _threshDb_q16;  // Positive above, negative below

    if (envelope <= _threshLin) {
        // Below threshold — apply ratio_below
        if (_ratioBelow == 100) return Q31_MAX;  // 1:1 = no change

        // slope = 1 - 100/ratio, overDb < 0 (below threshold)
        // For expansion (ratio>100): slope>0, gain_db = slope*overDb < 0 = reduction ✓
        int32_t slope_q16 = (1 << 16) - ((100 << 16) / _ratioBelow);
        int32_t gainDb_q16 = (int32_t)(((int64_t)slope_q16 * overDb_q16) >> 16);

        // Clamp: only allow gain reduction (gainDb <= 0)
        if (gainDb_q16 >= 0) return Q31_MAX;
        return db_q16_to_q31_gain_fast(gainDb_q16);
    } else {
        // Above threshold — apply ratio_above (compression)
        if (_ratioAbove == 100) return Q31_MAX;

        // For compression: gain_reduction = overDb * (1 - 1/ratio)
        // Apply as negative gain
        int32_t slope_q16 = (1 << 16) - ((100 << 16) / _ratioAbove);
        int32_t reductionDb_q16 = (int32_t)(((int64_t)slope_q16 * overDb_q16) >> 16);

        // Apply as gain reduction (negative dB)
        if (reductionDb_q16 <= 0) return Q31_MAX;
        return db_q16_to_q31_gain_fast(-reductionDb_q16);
    }
}

void Compander::reset() {
    _envelope = 0;
}

void Compander::setThreshold(int32_t db_001) {
    _thresholdDb = db_001;
    recalcCoeffs();
}

void Compander::setRatioBelow(int32_t ratio_001) {
    _ratioBelow = ratio_001;
}

void Compander::setRatioAbove(int32_t ratio_001) {
    _ratioAbove = ratio_001;
}

void Compander::setAttackTime(int32_t ms) {
    _attackMs = ms;
    recalcCoeffs();
}

void Compander::setReleaseTime(int32_t ms) {
    _releaseMs = ms;
    recalcCoeffs();
}

void Compander::setPregain(int32_t pregain_q412) {
    _pregainQ412 = pregain_q412;
    recalcCoeffs();
}

void Compander::recalcCoeffs() {
    _threshLin = db_to_q31_gain(_thresholdDb);
    _threshDb_q16 = q31_to_db_q16(_threshLin);  // Pre-compute for audio path
    _attackCoeff = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);

    // Convert Q4.12 pregain to Q31
    float pregainFloat = PREGAIN_Q412_TO_FLOAT(_pregainQ412);
    if (pregainFloat >= 1.0f) {
        _pregainLin = Q31_MAX;
    } else {
        _pregainLin = FLOAT_TO_Q31(pregainFloat);
    }
}
