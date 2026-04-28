/**
 * @file dsp_types.h
 * @brief DSP Core type definitions (Refactored for Float32)
 */

#ifndef DSP_TYPES_H
#define DSP_TYPES_H

#include <stdint.h>
#include <esp_attr.h>
//#include <esp_dsp.h>

// ============================================================================
// Communication Format Macros
// ============================================================================

// dB value in Q8.8 format (e.g., -768 = -3.0dB, 640 = +2.5dB)
#define DB_Q8_TO_FLOAT(x)      ((float)(x) / 256.0f)
#define FLOAT_TO_DB_Q8(x)      ((int16_t)((x) * 256.0f))

// Q factor in Q6.10 format (e.g., 717 ≈ 0.70, 1024 = 1.0)
#define Q_Q610_TO_FLOAT(x)     ((float)(x) / 1024.0f)
#define FLOAT_TO_Q_Q610(x)     ((int16_t)((x) * 1024.0f))

// Pregain in Q4.12 format (4096 = unity/0dB)
#define PREGAIN_Q412_TO_FLOAT(x) ((float)(x) / 4096.0f)
#define FLOAT_TO_PREGAIN_Q412(x) ((int32_t)((x) * 4096.0f))

// DRC threshold: 0.01dB steps (e.g., -2550 = -25.50dB)
#define DRC_TH_TO_FLOAT_DB(x)   ((float)(x) / 100.0f)
#define FLOAT_DB_TO_DRC_TH(x)   ((int32_t)((x) * 100.0f))

static inline int32_t IRAM_ATTR floatToI32Sat(float x) {
    if (x >= 1.0f)  return INT32_MAX;
    if (x <= -1.0f) return INT32_MIN;
    return (int32_t)(x * 2147483648.0f);
}

static __attribute__((always_inline)) inline
float IRAM_ATTR fast_recipsf2(float a) {
    float result;
    asm volatile (
        "wfr f1, %1\n"
        "recip0.s f0, f1\n"
        "const.s f2, 1\n"
        "msub.s f2, f1, f0\n"
        "maddn.s f0, f0, f2\n"
        "const.s f2, 1\n"
        "msub.s f2, f1, f0\n"
        "maddn.s f0, f0, f2\n"
        "rfr %0, f0\n"
        :"=r"(result):"r"(a):"f0","f1","f2"
    );
    return result;
}

#define fast_div(a, b) ((a) * fast_recipsf2(b))

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

typedef enum {
    DRC_MODE_FULLBAND = 0,
    DRC_MODE_2BAND,
    DRC_MODE_2BAND_FULLBAND,
    DRC_MODE_3BAND,
    DRC_MODE_3BAND_FULLBAND
} DRCMode;

typedef enum {
    DRC_CF_NONE = 0,
    DRC_CF_B1,
    DRC_CF_LR2,
    DRC_CF_LR4,
    DRC_CF_Q4
} DRCCrossoverType;

#endif // DSP_TYPES_H
