/**
 * @file dynamic_eq.cpp
 * @brief Dynamic EQ implementation — dual-EQ with level-dependent interpolation
 *
 * Processing flow per frame:
 * 1. Compute RMS energy from input
 * 2. Determine target alpha_low and alpha_high based on thresholds
 * 3. Smooth alphas with attack/release
 * 4. If alpha_low > 0: process through EQ_LOW
 * 5. If alpha_high > 0: process through EQ_HIGH
 * 6. Blend: output = alpha_low*Y_low + alpha_high*Y_high + (1-alpha_low-alpha_high)*X_in
 */

#include "dynamic_eq.h"
#include <string.h>

void DynamicEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    // Initialize internal EQs (they manage their own state)
    _eqLow.init(sampleRate, numChannels);
    _eqHigh.init(sampleRate, numChannels);

    // Force internal EQs to always be "enabled" — we handle enable/disable at DynamicEQ level
    _eqLow.enable();
    _eqHigh.enable();

    // Default thresholds
    _lowThreshDb    = -4000;  // -40 dB
    _normalThreshDb = -2000;  // -20 dB
    _highThreshDb   = -600;   // -6 dB

    // Default timing
    _attackMs  = 50;
    _releaseMs = 200;

    recalcCoeffs();
    reset();
}

void IRAM_ATTR DynamicEQ::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    size_t totalSamples = numSamples * _numChannels;

    // 1. Compute RMS energy from input signal
    for (size_t i = 0; i < totalSamples; i++) {
        _rmsEnergy = rms_update(_rmsEnergy, samples[i], _rmsCoeff);
    }

    // Convert RMS energy to dB (Q16.16)
    // _rmsEnergy is squared amplitude, so q31_to_db_q16 returns 2x the actual dB.
    // We divide by 2 to get the true dB level of the RMS voltage.
    int32_t currentDb_q16 = q31_to_db_q16(_rmsEnergy) / 2;

    // 2. Compute target alpha values
    q31_t targetAlphaLow  = computeTargetAlphaLow(currentDb_q16);
    q31_t targetAlphaHigh = computeTargetAlphaHigh(currentDb_q16);

    // 3. Smooth alpha transitions using attack/release
    q31_t coeffLow  = (targetAlphaLow  > _alphaLow)  ? _attackCoeff : _releaseCoeff;
    q31_t coeffHigh = (targetAlphaHigh > _alphaHigh)  ? _attackCoeff : _releaseCoeff;

    _alphaLow  = envelope_follow(_alphaLow,  targetAlphaLow,  coeffLow);
    _alphaHigh = envelope_follow(_alphaHigh, targetAlphaHigh, coeffHigh);

    // 4. Skip processing if both alphas are near zero (flat response)
    bool needLow  = (_alphaLow  > FLOAT_TO_Q31(0.001f));
    bool needHigh = (_alphaHigh > FLOAT_TO_Q31(0.001f));

    if (!needLow && !needHigh) return;  // Flat — no processing needed

    // 5. Process through EQ paths using a single stack-local temp buffer
    //    (saves 4KB static DRAM vs. two member buffers)
    q31_t tmpBuf[DSP_FRAME_SAMPLES];

    // Process EQ_LOW path first (if needed), store result in samples temporarily
    // We need to keep original dry signal, so we work with copies
    q31_t alphaFlat = Q31_MAX - _alphaLow - _alphaHigh;
    if (alphaFlat < 0) alphaFlat = 0;

    if (needLow && needHigh) {
        // Both paths active: process low into tmpBuf, then high in a second pass
        memcpy(tmpBuf, samples, totalSamples * sizeof(q31_t));
        _eqLow.processInternal(tmpBuf, numSamples);

        // Blend: start with flat + low components
        for (size_t i = 0; i < totalSamples; i++) {
            // result = dry*alphaFlat + low*alphaLow (accumulate, high added next)
            tmpBuf[i] = sat_q31((q63_t)q31_mul(samples[i], alphaFlat) +
                                (q63_t)q31_mul(tmpBuf[i], _alphaLow));
        }

        // Now process high path: reuse a portion of logic
        // We need original signal for high EQ — copy from samples
        q31_t highBuf[DSP_FRAME_SAMPLES];
        memcpy(highBuf, samples, totalSamples * sizeof(q31_t));
        _eqHigh.processInternal(highBuf, numSamples);

        // Final blend: add high component
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] = sat_q31((q63_t)tmpBuf[i] +
                                 (q63_t)q31_mul(highBuf[i], _alphaHigh));
        }
    } else if (needLow) {
        // Only low path
        memcpy(tmpBuf, samples, totalSamples * sizeof(q31_t));
        _eqLow.processInternal(tmpBuf, numSamples);

        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] = sat_q31((q63_t)q31_mul(samples[i], alphaFlat) +
                                 (q63_t)q31_mul(tmpBuf[i], _alphaLow));
        }
    } else {
        // Only high path
        memcpy(tmpBuf, samples, totalSamples * sizeof(q31_t));
        _eqHigh.processInternal(tmpBuf, numSamples);

        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] = sat_q31((q63_t)q31_mul(samples[i], alphaFlat) +
                                 (q63_t)q31_mul(tmpBuf[i], _alphaHigh));
        }
    }
}

void DynamicEQ::reset() {
    _rmsEnergy = 0;
    _alphaLow  = 0;
    _alphaHigh = 0;
    _eqLow.reset();
    _eqHigh.reset();
}

// ============================================================================
// Alpha computation (3-threshold system)
// ============================================================================

q31_t IRAM_ATTR DynamicEQ::computeTargetAlphaLow(int32_t currentDb_q16) {
    // Interpolate using dB scale for natural volume transitions
    int32_t lowThresh_q16  = (int32_t)_lowThreshDb * (65536 / 100);
    int32_t normThresh_q16 = (int32_t)_normalThreshDb * (65536 / 100);

    if (currentDb_q16 <= lowThresh_q16) {
        return Q31_MAX;  // 100% EQ_LOW
    } else if (currentDb_q16 >= normThresh_q16) {
        return 0;        // 0% EQ_LOW (in normal or high region)
    } else {
        // Interpolate between low and normal thresholds
        // alpha = (normThresh - currentDb) / (normThresh - lowThresh)
        int64_t num = (int64_t)(normThresh_q16 - currentDb_q16);
        int64_t den = (int64_t)(normThresh_q16 - lowThresh_q16);
        if (den <= 0) return 0;
        q31_t alpha = (q31_t)((num * Q31_MAX) / den);
        if (alpha < 0) alpha = 0;
        if (alpha > Q31_MAX) alpha = Q31_MAX;
        return alpha;
    }
}

q31_t IRAM_ATTR DynamicEQ::computeTargetAlphaHigh(int32_t currentDb_q16) {
    int32_t normThresh_q16 = (int32_t)_normalThreshDb * (65536 / 100);
    int32_t highThresh_q16 = (int32_t)_highThreshDb * (65536 / 100);

    if (currentDb_q16 >= highThresh_q16) {
        return Q31_MAX;  // 100% EQ_HIGH
    } else if (currentDb_q16 <= normThresh_q16) {
        return 0;        // 0% EQ_HIGH
    } else {
        // Interpolate between normal and high thresholds
        int64_t num = (int64_t)(currentDb_q16 - normThresh_q16);
        int64_t den = (int64_t)(highThresh_q16 - normThresh_q16);
        if (den <= 0) return 0;
        q31_t alpha = (q31_t)((num * Q31_MAX) / den);
        if (alpha < 0) alpha = 0;
        if (alpha > Q31_MAX) alpha = Q31_MAX;
        return alpha;
    }
}

// ============================================================================
// Parameter API
// ============================================================================

void DynamicEQ::setLowEnergyThreshold(int32_t db_001) {
    _lowThreshDb = db_001;
    recalcCoeffs();
}

void DynamicEQ::setNormalEnergyThreshold(int32_t db_001) {
    _normalThreshDb = db_001;
    recalcCoeffs();
}

void DynamicEQ::setHighEnergyThreshold(int32_t db_001) {
    _highThreshDb = db_001;
    recalcCoeffs();
}

void DynamicEQ::setAttackTime(int32_t ms) {
    _attackMs = ms;
    recalcCoeffs();
}

void DynamicEQ::setReleaseTime(int32_t ms) {
    _releaseMs = ms;
    recalcCoeffs();
}

void DynamicEQ::setEqLowBand(uint8_t band, const EQFilterParams& params) {
    _eqLow.setBand(band, params);
}

void DynamicEQ::setEqHighBand(uint8_t band, const EQFilterParams& params) {
    _eqHigh.setBand(band, params);
}

void DynamicEQ::recalcCoeffs() {
    _lowThreshLin    = db_to_q31_gain(_lowThreshDb);
    _normalThreshLin = db_to_q31_gain(_normalThreshDb);
    _highThreshLin   = db_to_q31_gain(_highThreshDb);

    _attackCoeff  = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);

    // RMS window coefficient: ~20ms window
    int32_t rmsWindowMs = 20;
    _rmsCoeff = calc_envelope_coeff(_sampleRate, rmsWindowMs);
}
