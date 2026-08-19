/**
 * @file dsp_types.h
 * @brief DSP Core type definitions (Refactored for Float32)
 */

#ifndef DSP_TYPES_H
#define DSP_TYPES_H

#include <stdint.h>
#include <esp_attr.h>

// ============================================================================
// Communication Format Macros
// ============================================================================

// dB value in Q8.8 format (e.g., -768 = -3.0dB, 640 = +2.5dB)
#define DB_Q8_TO_FLOAT(x)      ((float)(x) / 256.0f)
#define FLOAT_TO_DB_Q8(x)      ((int16_t)((x) * 256.0f))

// Q factor in Q6.10 format (e.g., 717 ≈ 0.70, 1024 = 1.0)
#define Q_Q610_TO_FLOAT(x)     ((float)(x) / 1024.0f)
#define FLOAT_TO_Q_Q610(x)     ((int16_t)((x) * 1024.0f))

static inline int32_t IRAM_ATTR floatToI32Sat(float x) {
    if (x >= 1.0f)  return INT32_MAX;
    if (x <= -1.0f) return INT32_MIN;
    return (int32_t)(x * 2147483648.0f);
}

// ============================================================================
// EQ Filter Types
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

// EQ Filter Parameters (matches communication protocol)
typedef struct {
    uint8_t  enabled;   // 0 or 1
    int16_t  type;      // EQFilterType enum
    uint16_t f0;        // Center frequency Hz
    int16_t  Q;         // Quality factor, Q6.10 format
    int16_t  gain;      // Gain in dB, Q8.8 format
} EQFilterParams;

// ============================================================================
// DRC & Crossover Types
// ============================================================================

// DRC Modes (simplified to 3 modes)
typedef enum {
    DRC_MODE_FULLBAND = 0,   // Single fullband compressor
    DRC_MODE_2BAND,          // 2-band with 1 crossover
    DRC_MODE_3BAND           // 3-band with 2 crossovers
} DRCMode;

// Crossover Filter Types
typedef enum {
    DRC_CF_BUTTERWORTH_1 = 0,  // Butterworth order 1
    DRC_CF_LR2,                // Linkwitz-Riley order 2
    DRC_CF_LR4,                // Linkwitz-Riley order 4
    DRC_CF_QCTRL_2             // Q-controller order 2
} DRCCrossoverType;

#endif // DSP_TYPES_H