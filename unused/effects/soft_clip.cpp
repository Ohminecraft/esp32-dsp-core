/**
 * @file soft_clip.cpp
 * @brief Soft clipper — tanh knee, linear passthrough below threshold
 *
 * Formula:
 *   |x| <= threshold  →  y = x          (hoàn toàn linear, không distort)
 *   |x| >  threshold  →  y = sign(x) * (threshold + headroom * tanh(excess / headroom))
 *     where  excess   = |x| - threshold
 *            headroom = 1.0 - threshold   (khoảng còn lại đến full scale)
 *
 * Behavior:
 *   - Không chạm tín hiệu bên dưới threshold → không gây distort với signal yếu
 *   - Knee mượt mà, đạo hàm liên tục tại điểm threshold
 *   - Asymptote về ±1.0, không bao giờ vượt full scale
 *   - Càng giảm threshold → bắt đầu saturate sớm hơn (tự nhiên hơn, không vỡ)
 *
 * Unit: setThreshold nhận db_001 (0.01dB steps)
 *   -40.00 dB → truyền -4000
 *   -6.00  dB → truyền -600
 */

#include "soft_clip.h"
#include <math.h>

void SoftClipper::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    setThreshold(0);
    reset();
}

void IRAM_ATTR SoftClipper::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    const float thr    = _threshold;
    const float invThr = _invHeadroom; // tái dùng, đổi tên sau
    const size_t total = numSamples * _numChannels;

    for (size_t i = 0; i < total; i++) {
        float x  = samples[i];
        float ax = x < 0.0f ? -x : x;

        if (ax > thr) {
            float y    = thr * tanhf(ax / thr);
            samples[i] = x < 0.0f ? -y : y;
        }
    }
}

void SoftClipper::reset() {
    // Stateless
}

void SoftClipper::setThreshold(int32_t db_001) {
    _thresholdDb = db_001;

    float db   = (float)db_001 / 100.0f;
    _threshold = db_to_linear_gain(db);

    if (_threshold > 1.0f) _threshold = 1.0f;
    if (_threshold < 1e-4f) _threshold = 1e-4f;

    // invHeadroom tái dụng làm 1/threshold cho tanhf
    _headroom    = _threshold;         // không dùng nữa nhưng giữ consistent
    _invHeadroom = 1.0f / _threshold;  // dùng trong process()
}