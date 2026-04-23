/**
 * @file compander.cpp
 * @brief Compander implementation
 */

#include "compander.h"
#include <math.h>

void Compander::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _thresholdDbInt = -2000;
    _ratioBelowQ88 = 256;  // 1.0
    _ratioAboveQ88 = 512;  // 2.0
    _attackMs = 10;
    _releaseMs = 100;
    _pregainQ412 = 4096;   // 1.0
    
    recalcCoeffs();
    reset();
}

void IRAM_ATTR Compander::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        float maxSample = 0.0f;
        for (int ch = 0; ch < _numChannels; ch++) {
            float s = samples[i * _numChannels + ch] * _pregain;
            float absVal = fast_abs(s);
            if (absVal > maxSample) maxSample = absVal;
        }

        // Update envelope (peak detection)
        float coeff = (maxSample > _envelope) ? _attackCoeff : _releaseCoeff;
        _envelope = envelope_follow(_envelope, maxSample, coeff);

        // Compute gain in dB domain
        float envDb = linear_to_db(_envelope);
        float gainDb = 0.0f;

        if (envDb > _thresholdDb) {
            gainDb = (_thresholdDb - envDb) * (1.0f - 1.0f / _ratioAbove);
        } else {
            gainDb = (_thresholdDb - envDb) * (1.0f - 1.0f / _ratioBelow);
        }

        float gainLinear = db_to_linear_gain(gainDb);

        for (int ch = 0; ch < _numChannels; ch++) {
            samples[i * _numChannels + ch] *= (_pregain * gainLinear);
        }
    }
}

void Compander::reset() {
    _envelope = 0.0f;
}

void Compander::recalcCoeffs() {
    _thresholdDb = (float)_thresholdDbInt / 100.0f;
    _ratioBelow = (float)_ratioBelowQ88 / 256.0f;
    _ratioAbove = (float)_ratioAboveQ88 / 256.0f;
    _pregain = (float)_pregainQ412 / 4096.0f;
    _attackCoeff = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);
}

void Compander::setThreshold(int32_t db_001) { _thresholdDbInt = db_001; recalcCoeffs(); }
void Compander::setRatioBelow(int32_t ratio_q88) { _ratioBelowQ88 = ratio_q88; recalcCoeffs(); }
void Compander::setRatioAbove(int32_t ratio_q88) { _ratioAboveQ88 = ratio_q88; recalcCoeffs(); }
void Compander::setAttackTime(int32_t ms) { _attackMs = ms; recalcCoeffs(); }
void Compander::setReleaseTime(int32_t ms) { _releaseMs = ms; recalcCoeffs(); }
void Compander::setPregain(int32_t gain_q412) { _pregainQ412 = gain_q412; recalcCoeffs(); }
