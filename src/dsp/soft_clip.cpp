/**
 * @file soft_clip.cpp
 * @brief Soft Clipper implementation
 */

#include "soft_clip.h"

void SoftClipper::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    setThreshold(0x60000000); // ~75% of full scale
    _mode = 0;                // Not used anymore, kept for API compat
}

void IRAM_ATTR SoftClipper::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    size_t totalSamples = numSamples * _numChannels;
    q31_t T = _threshold;

    // Smooth continuous parabolic saturation above threshold
    // y = x - (x - T)^2 / (2 * (1 - T))
    for (size_t i = 0; i < totalSamples; i++) {
        q31_t x = samples[i];
        q31_t absX = q31_abs(x);

        if (absX > T) {
            q31_t diff = absX - T;
            q31_t diff_sq = q31_mul(diff, diff);
            q31_t correction = q31_mul_q4_27(diff_sq, _scale_q427);
            q31_t y = absX - correction;
            samples[i] = (x > 0) ? y : -y;
        }
    }
}

void SoftClipper::reset() {
    // No state — stateless processing
}

void SoftClipper::setThreshold(q31_t threshold) {
    if (threshold < 0) threshold = 0;
    if (threshold > Q31_MAX - 100) threshold = Q31_MAX - 100;
    _threshold = threshold;

    // Precalculate scale factor for parabolic saturation
    // scale = 1.0 / (2.0 * (1.0 - T_float))
    float t_float = (float)threshold / 2147483648.0f;
    float scale = 1.0f / (2.0f * (1.0f - t_float));
    _scale_q427 = (int32_t)(scale * (1 << 27));
}

void SoftClipper::setMode(int32_t mode) {
    _mode = (mode == 1) ? 1 : 0;
}
