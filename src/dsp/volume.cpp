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

    size_t totalSamples = numSamples * _numChannels;
    
    // Calculate gain step for smooth ramping over the frame
    float gainStep = (_targetGain - _currentGain) / (float)totalSamples;

    for (size_t i = 0; i < totalSamples; i++) {
        samples[i] *= _currentGain;
        _currentGain += gainStep;
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
