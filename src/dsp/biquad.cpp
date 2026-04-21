/**
 * @file biquad.cpp
 * @brief Biquad filter implementation — coefficient design and block processing
 */

#include "biquad.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ============================================================================
// Float to Q2.30 conversion (used only during coefficient design)
// ============================================================================

static inline int32_t float_to_q428(float x) {
    // Q4.28: range approximately [-8.0, +8.0)
    if (x >= 8.0f) return 0x7FFFFFFF;
    if (x <= -8.0f) return 0x80000000;
    return (int32_t)(x * (float)(1 << 28));
}

// ============================================================================
// Coefficient Design (float internally, output Q2.30)
// ============================================================================

void Biquad::design(EQFilterType type, float f0, float Q, float gain_dB, float fs) {
    float A = powf(10.0f, gain_dB / 40.0f);  // sqrt(10^(dB/20))
    float w0 = 2.0f * M_PI * f0 / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha;

    float b0f = 1.0f, b1f = 0.0f, b2f = 0.0f;
    float a0f = 1.0f, a1f = 0.0f, a2f = 0.0f;

    if (Q <= 0.001f) Q = 0.001f;  // Prevent divide by zero

    switch (type) {
        case EQ_FILTER_TYPE_PEAKING:
            alpha = sin_w0 / (2.0f * Q);
            b0f =  1.0f + alpha * A;
            b1f = -2.0f * cos_w0;
            b2f =  1.0f - alpha * A;
            a0f =  1.0f + alpha / A;
            a1f = -2.0f * cos_w0;
            a2f =  1.0f - alpha / A;
            break;

        case EQ_FILTER_TYPE_LOW_SHELF: {
            alpha = sin_w0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / Q - 1.0f) + 2.0f);
            float sqrtA = sqrtf(A);
            float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0f =  A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + twoSqrtAAlpha);
            b1f =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
            b2f =  A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - twoSqrtAAlpha);
            a0f =  (A + 1.0f) + (A - 1.0f) * cos_w0 + twoSqrtAAlpha;
            a1f = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0);
            a2f =  (A + 1.0f) + (A - 1.0f) * cos_w0 - twoSqrtAAlpha;
            break;
        }

        case EQ_FILTER_TYPE_HIGH_SHELF: {
            alpha = sin_w0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / Q - 1.0f) + 2.0f);
            float sqrtA = sqrtf(A);
            float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0f =  A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + twoSqrtAAlpha);
            b1f = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
            b2f =  A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - twoSqrtAAlpha);
            a0f =  (A + 1.0f) - (A - 1.0f) * cos_w0 + twoSqrtAAlpha;
            a1f =  2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0);
            a2f =  (A + 1.0f) - (A - 1.0f) * cos_w0 - twoSqrtAAlpha;
            break;
        }

        case EQ_FILTER_TYPE_LOW_PASS:
            alpha = sin_w0 / (2.0f * Q);
            b0f = (1.0f - cos_w0) / 2.0f;
            b1f =  1.0f - cos_w0;
            b2f = (1.0f - cos_w0) / 2.0f;
            a0f =  1.0f + alpha;
            a1f = -2.0f * cos_w0;
            a2f =  1.0f - alpha;
            break;

        case EQ_FILTER_TYPE_HIGH_PASS:
            alpha = sin_w0 / (2.0f * Q);
            b0f =  (1.0f + cos_w0) / 2.0f;
            b1f = -(1.0f + cos_w0);
            b2f =  (1.0f + cos_w0) / 2.0f;
            a0f =  1.0f + alpha;
            a1f = -2.0f * cos_w0;
            a2f =  1.0f - alpha;
            break;

        case EQ_FILTER_TYPE_BAND_PASS:
            alpha = sin_w0 / (2.0f * Q);
            b0f =  alpha;
            b1f =  0.0f;
            b2f = -alpha;
            a0f =  1.0f + alpha;
            a1f = -2.0f * cos_w0;
            a2f =  1.0f - alpha;
            break;

        case EQ_FILTER_TYPE_NOTCH:
            alpha = sin_w0 / (2.0f * Q);
            b0f =  1.0f;
            b1f = -2.0f * cos_w0;
            b2f =  1.0f;
            a0f =  1.0f + alpha;
            a1f = -2.0f * cos_w0;
            a2f =  1.0f - alpha;
            break;

        case EQ_FILTER_TYPE_LOW_PASS_ORDER1:
            // 1st order: y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1]
            // H(z) = (1 - cos(w0)) / (1 + alpha) * (1 + z^-1) / (1 - a1*z^-1)
            {
                float K = tanf(M_PI * f0 / fs);
                b0f = K / (K + 1.0f);
                b1f = K / (K + 1.0f);
                b2f = 0.0f;
                a0f = 1.0f;
                a1f = (K - 1.0f) / (K + 1.0f);
                a2f = 0.0f;
            }
            break;

        case EQ_FILTER_TYPE_HIGH_PASS_ORDER1:
            {
                float K = tanf(M_PI * f0 / fs);
                b0f =  1.0f / (K + 1.0f);
                b1f = -1.0f / (K + 1.0f);
                b2f = 0.0f;
                a0f = 1.0f;
                a1f = (K - 1.0f) / (K + 1.0f);
                a2f = 0.0f;
            }
            break;

        default:
            // Passthrough (unity)
            b0f = 1.0f; b1f = 0.0f; b2f = 0.0f;
            a0f = 1.0f; a1f = 0.0f; a2f = 0.0f;
            break;
    }

    // Normalize by a0
    float inv_a0 = 1.0f / a0f;
    b0f *= inv_a0;
    b1f *= inv_a0;
    b2f *= inv_a0;
    a1f *= inv_a0;
    a2f *= inv_a0;

    // Convert to Q4.28, store a1/a2 as negated (for MAC accumulation)
    _coeffs.b0 = float_to_q428(b0f);
    _coeffs.b1 = float_to_q428(b1f);
    _coeffs.b2 = float_to_q428(b2f);
    _coeffs.a1 = float_to_q428(-a1f);  // Negated for Direct Form II
    _coeffs.a2 = float_to_q428(-a2f);  // Negated for Direct Form II
}

void Biquad::designFromParams(const EQFilterParams& params, int32_t sampleRate) {
    float f0 = (float)params.f0;
    float Q  = Q_Q610_TO_FLOAT(params.Q);
    float gain_dB = DB_Q8_TO_FLOAT(params.gain);
    EQFilterType type = (EQFilterType)params.type;

    design(type, f0, Q, gain_dB, (float)sampleRate);
}

// ============================================================================
// Block Processing
// ============================================================================

/*
void IRAM_ATTR Biquad::process(q31_t* __restrict samples, size_t numSamples) {
    for (size_t i = 0; i < numSamples; i++) {
        size_t idx = i * 2;  // Stereo interleaved
        samples[idx]     = processSample(samples[idx],     _state[0]);  // Left
        samples[idx + 1] = processSample(samples[idx + 1], _state[1]);  // Right
    }
}

void IRAM_ATTR Biquad::processMono(q31_t* __restrict samples, size_t numSamples) {
    for (size_t i = 0; i < numSamples; i++) {
        samples[i] = processSample(samples[i], _state[0]);
    }
}
*/

void Biquad::reset() {
    memset(_state, 0, sizeof(_state));
}
