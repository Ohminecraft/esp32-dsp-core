/**
 * @file dynamic_eq.cpp
 * @brief Dynamic EQ implementation — Aligned and optimized
 */

#include "dynamic_eq.h"
#include <string.h>

void DynamicEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _eqLow.init(sampleRate, numChannels);
    _eqHigh.init(sampleRate, numChannels);
    _eqLow.enable();
    _eqHigh.enable();

    _lowThreshDb    = -4000;
    _normalThreshDb = -2000;
    _highThreshDb   = -600;
    _attackMs  = 50;
    _releaseMs = 200;

    recalcCoeffs();
    reset();
}

void IRAM_ATTR DynamicEQ::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    size_t totalSamples = numSamples * _numChannels;

    // 1. Compute RMS energy
    for (size_t i = 0; i < totalSamples; i++) {
        _rmsEnergySq = rms_update(_rmsEnergySq, samples[i], _rmsCoeff);
    }

    float currentDb = linear_to_db(sqrtf(_rmsEnergySq));

    // 2. Compute target alpha values
    float targetAlphaLow  = computeTargetAlphaLow(currentDb);
    float targetAlphaHigh = computeTargetAlphaHigh(currentDb);

    // 3. Smooth transitions
    float coeffLow  = (targetAlphaLow  > _alphaLow)  ? _attackCoeff : _releaseCoeff;
    float coeffHigh = (targetAlphaHigh > _alphaHigh)  ? _attackCoeff : _releaseCoeff;

    _alphaLow  = envelope_follow(_alphaLow,  targetAlphaLow,  coeffLow);
    _alphaHigh = envelope_follow(_alphaHigh, targetAlphaHigh, coeffHigh);

    // 4. Determine if processing is needed
    bool needLow  = (_alphaLow  > 0.001f);
    bool needHigh = (_alphaHigh > 0.001f);
    if (!needLow && !needHigh) return;

    // 5. Optimized Processing with 16-byte alignment
    float tempBuf[DSP_FRAME_SAMPLES];
    float alphaFlat = 1.0f - _alphaLow - _alphaHigh;
    if (alphaFlat < 0.0f) alphaFlat = 0.0f;

    // Store original signal
    memcpy(tempBuf, samples, totalSamples * sizeof(float));

    // Initial state of output: dry * alphaFlat
    for (size_t i = 0; i < totalSamples; i++) {
        samples[i] = tempBuf[i] * alphaFlat;
    }

    // Process and add LOW path
    if (needLow) {
        float processingBuf[DSP_FRAME_SAMPLES];
        memcpy(processingBuf, tempBuf, totalSamples * sizeof(float));
        _eqLow.processInternal(processingBuf, numSamples);
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] += processingBuf[i] * _alphaLow;
        }
    }

    // Process and add HIGH path
    if (needHigh) {
        float processingBuf[DSP_FRAME_SAMPLES];
        memcpy(processingBuf, tempBuf, totalSamples * sizeof(float));
        _eqHigh.processInternal(processingBuf, numSamples);
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] += processingBuf[i] * _alphaHigh;
        }
    }
}

void DynamicEQ::reset() {
    _rmsEnergySq = 0.0f;
    _alphaLow  = 0.0f;
    _alphaHigh = 0.0f;
    _eqLow.reset();
    _eqHigh.reset();
}

float IRAM_ATTR DynamicEQ::computeTargetAlphaLow(float currentDb) {
    float lowThresh  = (float)_lowThreshDb / 100.0f;
    float normThresh = (float)_normalThreshDb / 100.0f;
    if (currentDb <= lowThresh) return 1.0f;
    if (currentDb >= normThresh) return 0.0f;
    float alpha = (normThresh - currentDb) / (normThresh - lowThresh);
    return (alpha < 0.0f) ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

float IRAM_ATTR DynamicEQ::computeTargetAlphaHigh(float currentDb) {
    float normThresh = (float)_normalThreshDb / 100.0f;
    float highThresh = (float)_highThreshDb / 100.0f;
    if (currentDb >= highThresh) return 1.0f;
    if (currentDb <= normThresh) return 0.0f;
    float alpha = (currentDb - normThresh) / (highThresh - normThresh);
    return (alpha < 0.0f) ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

void DynamicEQ::setLowEnergyThreshold(int32_t db_001) { _lowThreshDb = db_001; }
void DynamicEQ::setNormalEnergyThreshold(int32_t db_001) { _normalThreshDb = db_001; }
void DynamicEQ::setHighEnergyThreshold(int32_t db_001) { _highThreshDb = db_001; }
void DynamicEQ::setAttackTime(int32_t ms) { _attackMs = ms; recalcCoeffs(); }
void DynamicEQ::setReleaseTime(int32_t ms) { _releaseMs = ms; recalcCoeffs(); }

void DynamicEQ::setEqLowBand(uint8_t band, const EQFilterParams& params) { _eqLow.setBand(band, params); }
void DynamicEQ::setEqHighBand(uint8_t band, const EQFilterParams& params) { _eqHigh.setBand(band, params); }

void DynamicEQ::recalcCoeffs() {
    _attackCoeff  = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);
    _rmsCoeff = calc_envelope_coeff(_sampleRate, 20);
}
