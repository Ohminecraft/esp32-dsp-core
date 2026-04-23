/**
 * @file noise_gate.cpp
 * @brief Noise Gate implementation
 */

#include "noise_gate.h"
#include <math.h>

void NoiseGate::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _lowerThreshDb = -6000;
    _upperThreshDb = -5000;
    _attackMs = 10;
    _releaseMs = 100;
    _holdMs = 50;
    
    recalcCoeffs();
    reset();
}

void IRAM_ATTR NoiseGate::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    for (size_t i = 0; i < numSamples; i++) {
        float maxSample = 0.0f;
        for (int ch = 0; ch < _numChannels; ch++) {
            float absVal = fast_abs(samples[i * _numChannels + ch]);
            if (absVal > maxSample) maxSample = absVal;
        }

        // Update envelope
        float coeff = (maxSample > _envelope) ? _attackCoeff : _releaseCoeff;
        _envelope = envelope_follow(_envelope, maxSample, coeff);

        // Gate logic
        bool open = false;
        if (_envelope > _upperThresh) {
            open = true;
            _holdCounter = _holdSamples;
        } else if (_envelope > _lowerThresh && _holdCounter > 0) {
            open = true;
            _holdCounter--;
        } else if (_holdCounter > 0) {
            open = true;
            _holdCounter--;
        }

        float targetGain = open ? 1.0f : 0.0f;
        _currentGain = envelope_follow(_currentGain, targetGain, coeff);

        for (int ch = 0; ch < _numChannels; ch++) {
            samples[i * _numChannels + ch] *= _currentGain;
        }
    }
}

void NoiseGate::reset() {
    _envelope = 0.0f;
    _currentGain = 0.0f;
    _holdCounter = 0;
}

void NoiseGate::recalcCoeffs() {
    _lowerThresh = db_to_linear_gain((float)_lowerThreshDb / 100.0f);
    _upperThresh = db_to_linear_gain((float)_upperThreshDb / 100.0f);
    _attackCoeff = calc_envelope_coeff(_sampleRate, _attackMs);
    _releaseCoeff = calc_envelope_coeff(_sampleRate, _releaseMs);
    _holdSamples = (_sampleRate * _holdMs) / 1000;
}

void NoiseGate::setLowerThreshold(int32_t db_001) { _lowerThreshDb = db_001; recalcCoeffs(); }
void NoiseGate::setUpperThreshold(int32_t db_001) { _upperThreshDb = db_001; recalcCoeffs(); }
void NoiseGate::setAttackTime(int32_t ms) { _attackMs = ms; recalcCoeffs(); }
void NoiseGate::setReleaseTime(int32_t ms) { _releaseMs = ms; recalcCoeffs(); }
void NoiseGate::setHoldTime(int32_t ms) { _holdMs = ms; recalcCoeffs(); }
