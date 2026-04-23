/**
 * @file soft_clip.cpp
 * @brief Soft clipper implementation
 */

#include "soft_clip.h"

void SoftClipper::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _threshold = 1.0f;
    _mode = 0;
    reset();
}

void IRAM_ATTR SoftClipper::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    size_t totalSamples = numSamples * _numChannels;

    for (size_t i = 0; i < totalSamples; i++) {
        // Simple cubic soft clipper
        samples[i] = soft_clip_cubic(samples[i]);
    }
}

void SoftClipper::reset() {
    // No state
}

void SoftClipper::setThreshold(int32_t db_001) {
    _thresholdDb = db_001;
    _threshold = db_to_linear_gain((float)db_001 / 100.0f);
}

void SoftClipper::setMode(int32_t mode) {
    _mode = mode;
}
