/**
 * @file soft_clip.cpp
 * @brief Soft Clipper implementation
 */

#include "soft_clip.h"

void SoftClipper::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _threshold = 0x60000000;  // ~75% of full scale
    _mode = 0;                // Cubic
}

void IRAM_ATTR SoftClipper::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    size_t totalSamples = numSamples * _numChannels;

    if (_mode == 0) {
        // Cubic soft clip
        for (size_t i = 0; i < totalSamples; i++) {
            q31_t x = samples[i];
            q31_t absX = q31_abs(x);

            if (absX > _threshold) {
                samples[i] = soft_clip_cubic(x);
            }
        }
    } else {
        // Tanh approximation
        for (size_t i = 0; i < totalSamples; i++) {
            q31_t x = samples[i];
            q31_t absX = q31_abs(x);

            if (absX > _threshold) {
                samples[i] = soft_clip_tanh(x);
            }
        }
    }
}

void SoftClipper::reset() {
    // No state — stateless processing
}

void SoftClipper::setThreshold(q31_t threshold) {
    if (threshold < 0) threshold = 0;
    _threshold = threshold;
}

void SoftClipper::setMode(int32_t mode) {
    _mode = (mode == 1) ? 1 : 0;
}
