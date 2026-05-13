/**
 * @file dynamics_processor.h
 * @brief Common state and math for dynamic range processors (Compander, DRC, etc.)
 */

#ifndef DYNAMICS_PROCESSOR_H
#define DYNAMICS_PROCESSOR_H

#include "fixed_math.h"

struct EnvelopeState {
    float envelope = 0.0f;
    float gainLinear = 1.0f;
    int decimCount = 0;

    inline void reset() {
        envelope = 0.0f;
        gainLinear = 1.0f;
        decimCount = 0;
    }
};

class DynamicsProcessor {
public:
    /**
     * Compute slope given a ratio in Q88 format (where 100 = 1.00:1)
     * e.g., ratio = 400 -> slope = 1 - 1/4 = 0.75
     */
    static inline float ratioToSlope(int32_t ratio_q88) {
        float r = (float)ratio_q88 / 100.0f;
        if (r < 0.01f) r = 0.01f;
        return 1.0f - (1.0f / r);
    }
    
    /**
     * Compute envelope coefficient from ms and sample rate
     */
    static inline float calcCoeff(int32_t sampleRate, int32_t ms) {
        return calc_envelope_coeff(sampleRate, ms);
    }
};

#endif // DYNAMICS_PROCESSOR_H
