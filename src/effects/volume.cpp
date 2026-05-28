/**
 * @file volume.cpp
 * @brief Volume control implementation
 */

#include "volume.h"
#include <string.h>

void VolumeControl::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    reset();
}

void IRAM_ATTR VolumeControl::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    if (_mono && _numChannels == 2) {
        // Mono mode: Sum Left and Right, average them, and apply gain
        float gainStep = (_targetGain - _currentGain) / (float)numSamples;
        for (size_t i = 0; i < numSamples; i++) {
            float l = samples[i * 2];
            float r = samples[i * 2 + 1];
            float mono = 0.707f * l + 0.707f * r;

            samples[i * 2]     = mono * _currentGain;
            samples[i * 2 + 1] = mono * _currentGain;

            _currentGain += gainStep;
        }
    } else {
        size_t totalSamples = numSamples * _numChannels;
        
        // Calculate gain step for smooth ramping over the frame
        float gainStep = (_targetGain - _currentGain) / (float)totalSamples;

        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] *= _currentGain;
            _currentGain += gainStep;
        }
    }
    
    // Ensure final value is exact to avoid drift
    _currentGain = _targetGain;
}

void VolumeControl::reset() {
    _currentGain = _targetGain;
}

void VolumeControl::setGainDb(int16_t gain_db) {
    _gainDb = gain_db;
    _gainLinear = db_q88_to_linear_gain(gain_db);
    updateTargetGain();
}

void VolumeControl::setMute(bool mute) {
    _muted = mute;
    updateTargetGain();
}

void VolumeControl::updateTargetGain() {
    _targetGain = _muted ? 0.0f : _gainLinear;
}
