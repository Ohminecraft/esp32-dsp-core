/**
 * @file stereo_widener.cpp
 * @brief Stereo Widener implementation
 */

#include "stereo_widener.h"

void StereoWidener::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _intensity = 50;
    recalcCoeffs();
    reset();
}

void IRAM_ATTR StereoWidener::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled || _numChannels < 2) return;

    for (size_t i = 0; i < numSamples; i++) {
        float L = samples[i * 2];
        float R = samples[i * 2 + 1];

        // M/S conversion
        float M = (L + R) * 0.5f;
        float S = (L - R) * 0.5f;

        // Apply width to side channel
        S *= _width;

        // Convert back to L/R
        samples[i * 2] = M + S;
        samples[i * 2 + 1] = M - S;
    }
}

void StereoWidener::reset() {
    // No state
}

void StereoWidener::recalcCoeffs() {
    // 0 intensity = 1.0 (mono mix), 100 intensity = 2.0 (super wide)
    _width = 1.0f + (float)_intensity / 100.0f;
}

void StereoWidener::setIntensity(int32_t intensity) {
    _intensity = intensity;
    recalcCoeffs();
}
