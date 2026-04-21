/**
 * @file volume.cpp
 * @brief Master volume control implementation with gain ramping
 */

#include "volume.h"

void VolumeControl::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _gainDb = 0;
    _gainLinear = (1 << 27);  // Q4.27 unity gain
    _targetGain = (1 << 27);
    _currentGain = (1 << 27);
    _muted = false;
}

void IRAM_ATTR VolumeControl::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    size_t totalSamples = numSamples * _numChannels;

    // Check if we need ramping
    if (_currentGain == _targetGain) {
        // No ramping needed — constant gain
        if (_currentGain == (1 << 27)) return;  // Unity gain — no-op

        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] = q31_mul_q4_27(samples[i], _currentGain);
        }
    } else {
        // Ramp gain across the frame
        q31_t gain_step = gain_ramp_step(_currentGain, _targetGain, (int32_t)numSamples);
        q31_t gain = _currentGain;

        for (size_t i = 0; i < numSamples; i++) {
            size_t idx = i * _numChannels;

            // Apply same gain to all channels in this frame
            for (int ch = 0; ch < _numChannels; ch++) {
                samples[idx + ch] = q31_mul_q4_27(samples[idx + ch], gain);
            }
            gain += gain_step;
        }
        _currentGain = _targetGain;  // Snap to target at end of frame
    }
}

void VolumeControl::reset() {
    _currentGain = _targetGain;  // Immediate — no ramp on reset
}

void VolumeControl::setGainDb(int16_t gain_db) {
    _gainDb = gain_db;
    _gainLinear = db_q88_to_q4_27_gain(gain_db);
    updateTargetGain();
}

void VolumeControl::setMute(bool mute) {
    _muted = mute;
    updateTargetGain();
}

void VolumeControl::updateTargetGain() {
    _targetGain = _muted ? 0 : _gainLinear;
}
