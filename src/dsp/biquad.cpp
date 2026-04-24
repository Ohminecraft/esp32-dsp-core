/**
 * @file biquad.cpp
 * @brief Biquad filter implementation — optimized for esp-dsp
 */

#include "biquad.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void Biquad::design(EQFilterType type, float f0, float Q, float gain_dB, float fs) {
    float A = sqrtf(powf(10.0f, fast_div(gain_dB, 20.0f)));  // sqrt(10^(dB/20))
    float inv_A = fast_recipsf2(A);
    float w0 = fast_div(2.0f * M_PI * f0, fs);
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha;

    float b0f = 1.0f, b1f = 0.0f, b2f = 0.0f;
    float a0f = 1.0f, a1f = 0.0f, a2f = 0.0f;

    if (Q <= 0.001f) Q = 0.001f;

    switch (type) {
        case EQ_FILTER_TYPE_PEAKING:
            alpha = fast_div(sin_w0, 2.0f * Q);
            b0f =  1.0f + alpha * A;
            b1f = -2.0f * cos_w0;
            b2f =  1.0f - alpha * A;
            a0f =  1.0f + alpha * inv_A;
            a1f = -2.0f * cos_w0;
            a2f =  1.0f - alpha * inv_A;
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
            b0f = 1.0f; b1f = 0.0f; b2f = 0.0f;
            a0f = 1.0f; a1f = 0.0f; a2f = 0.0f;
            break;
    }

    float inv_a0 = fast_recipsf2(a0f);
    _coeffs[0] = b0f * inv_a0;
    _coeffs[1] = b1f * inv_a0;
    _coeffs[2] = b2f * inv_a0;
    _coeffs[3] = a1f * inv_a0;
    _coeffs[4] = a2f * inv_a0;
}

void Biquad::designFromParams(const EQFilterParams& params, int32_t sampleRate) {
    float f0 = (float)params.f0;
    float Q  = Q_Q610_TO_FLOAT(params.Q);
    float gain_dB = DB_Q8_TO_FLOAT(params.gain);
    EQFilterType type = (EQFilterType)params.type;

    design(type, f0, Q, gain_dB, (float)sampleRate);
}

void Biquad::reset() {
    memset(_state, 0, sizeof(_state));
}