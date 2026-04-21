/**
 * @file dsp_types.h
 * @brief Fixed-point type definitions and conversion macros for ESP32 DSP Core
 *
 * Primary format: Q31 (int32_t) for 24-bit audio processing
 * Coefficient format: Q2.30 (int32_t) for biquad filter stability
 * Accumulator format: Q1.63 (int64_t) for overflow-free MAC operations
 *
 * Matches MVSilicon's EQFilterParams format conventions:
 *   - Gain: Q8.8 (int16_t) in dB
 *   - Q factor: Q6.10 (int16_t)
 *   - Pregain: Q4.12 (int32_t)
 */

#ifndef DSP_TYPES_H
#define DSP_TYPES_H

#include <stdint.h>
#include <esp_attr.h>

// ============================================================================
// Fixed-Point Type Aliases
// ============================================================================

typedef int16_t  q15_t;     // Q1.15 — legacy/I2S 16-bit
typedef int32_t  q31_t;     // Q1.31 — primary audio sample format
typedef int64_t  q63_t;     // Q1.63 — accumulator for MAC operations

// ============================================================================
// Q31 Constants
// ============================================================================

#define Q31_MAX         ((q31_t)0x7FFFFFFF)   // +1.0 - epsilon
#define Q31_MIN         ((q31_t)0x80000000)   // -1.0
#define Q31_UNITY       ((q31_t)0x7FFFFFFF)   // ~1.0
#define Q31_HALF        ((q31_t)0x40000000)   // 0.5
#define Q31_ZERO        ((q31_t)0)

// ============================================================================
// Q15 Constants
// ============================================================================

#define Q15_MAX         ((q15_t)0x7FFF)       // +1.0 - epsilon
#define Q15_MIN         ((q15_t)0x8000)       // -1.0
#define Q15_UNITY       ((q15_t)0x7FFF)
#define Q15_ZERO        ((q15_t)0)

// ============================================================================
// 24-bit Saturation (for I2S output to PCM5102A)
// ============================================================================

#define Q23_MAX         ((int32_t)0x7FFFFF)
#define Q23_MIN         ((int32_t)(-0x800000))

// ============================================================================
// Conversion Macros
// ============================================================================

// Float ↔ Q31
#define FLOAT_TO_Q31(x)     ((q31_t)((x) * 2147483648.0f))
#define Q31_TO_FLOAT(x)     ((float)(x) / 2147483648.0f)

// Float ↔ Q15
#define FLOAT_TO_Q15(x)     ((q15_t)((x) * 32768.0f))
#define Q15_TO_FLOAT(x)     ((float)(x) / 32768.0f)

// Q15 ↔ Q31
#define Q15_TO_Q31(x)       (((q31_t)(x)) << 16)
#define Q31_TO_Q15(x)       ((q15_t)((x) >> 16))

// Q31 ↔ 24-bit (for I2S)
#define Q31_TO_Q23(x)       ((x) >> 8)
#define Q23_TO_Q31(x)       (((q31_t)(x)) << 8)

// ============================================================================
// MVSilicon-Compatible Format Macros
// ============================================================================

// dB value in Q8.8 format (e.g., -768 = -3.0dB, 640 = +2.5dB)
#define DB_Q8_TO_FLOAT(x)      ((float)(x) / 256.0f)
#define FLOAT_TO_DB_Q8(x)      ((int16_t)((x) * 256.0f))

// Q factor in Q6.10 format (e.g., 717 ≈ 0.70, 1024 = 1.0)
#define Q_Q610_TO_FLOAT(x)     ((float)(x) / 1024.0f)
#define FLOAT_TO_Q_Q610(x)     ((int16_t)((x) * 1024.0f))

// Pregain in Q4.12 format (4096 = unity/0dB)
#define PREGAIN_Q412_UNITY      4096
#define PREGAIN_Q412_TO_FLOAT(x) ((float)(x) / 4096.0f)
#define FLOAT_TO_PREGAIN_Q412(x) ((int32_t)((x) * 4096.0f))

// DRC threshold: 0.01dB steps (e.g., -2550 = -25.50dB)
#define DRC_TH_TO_FLOAT_DB(x)   ((float)(x) / 100.0f)
#define FLOAT_DB_TO_DRC_TH(x)   ((int32_t)((x) * 100.0f))

// ============================================================================
// Saturation Helpers
// ============================================================================

static inline q31_t sat_q31(q63_t x) {
    if (x > Q31_MAX) return Q31_MAX;
    if (x < Q31_MIN) return Q31_MIN;
    return (q31_t)x;
}

static inline q15_t sat_q15(q31_t x) {
    if (x > Q15_MAX) return Q15_MAX;
    if (x < Q15_MIN) return Q15_MIN;
    return (q15_t)x;
}

static inline q31_t sat_q23(q31_t x) {
    if (x > Q23_MAX) return Q23_MAX;
    if (x < Q23_MIN) return Q23_MIN;
    return x;
}

// ============================================================================
// EQ Filter Types (matches MVSilicon EQ_FILTER_TYPE_SET)
// ============================================================================

typedef enum {
    EQ_FILTER_TYPE_PEAKING = 0,
    EQ_FILTER_TYPE_LOW_SHELF,
    EQ_FILTER_TYPE_HIGH_SHELF,
    EQ_FILTER_TYPE_LOW_PASS,
    EQ_FILTER_TYPE_HIGH_PASS,
    EQ_FILTER_TYPE_BAND_PASS,
    EQ_FILTER_TYPE_NOTCH,
    EQ_FILTER_TYPE_LOW_PASS_ORDER1,
    EQ_FILTER_TYPE_HIGH_PASS_ORDER1,
    EQ_FILTER_TYPE_COUNT
} EQFilterType;

// ============================================================================
// EQ Filter Parameters (matches MVSilicon EQFilterParams)
// ============================================================================

typedef struct {
    uint8_t enabled;   // 0 or 1
    int16_t  type;    // EQFilterType enum
    uint16_t f0;      // Center frequency Hz
    int16_t  Q;       // Quality factor, Q6.10 format
    int16_t  gain;    // Gain in dB, Q8.8 format
} EQFilterParams;

// ============================================================================
// DRC Mode (matches MVSilicon DRC_MODE)
// ============================================================================

typedef enum {
    DRC_MODE_FULLBAND = 0,
    DRC_MODE_2BAND,
    DRC_MODE_2BAND_FULLBAND,
    DRC_MODE_3BAND,
    DRC_MODE_3BAND_FULLBAND
} DRCMode;

// DRC Crossover Filter Type (matches MVSilicon DRC_CF_TYPE)
typedef enum {
    DRC_CF_NONE = 0,
    DRC_CF_B1,      // Butterworth 1st order
    DRC_CF_LR2,     // Linkwitz-Riley 2nd order
    DRC_CF_LR4,     // Linkwitz-Riley 4th order
    DRC_CF_Q4       // Q-controlled 4th order
} DRCCrossoverType;

#endif // DSP_TYPES_H
