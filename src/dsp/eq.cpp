/**
 * @file eq.cpp
 * @brief 10-band Parametric EQ implementation
 */

#include "eq.h"
#include "../utils/fixed_math.h"
#include <string.h>

void ParametricEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    //_filterCount = 0;
    _pregain = Q31_MAX;
    memset(_params, 0, sizeof(_params));
    reset();
}

void IRAM_ATTR ParametricEQ::process(q31_t* __restrict samples, size_t numSamples) {
    if (!_enabled) return;
    processInternal(samples, numSamples);
}

void IRAM_ATTR ParametricEQ::processInternal(q31_t* __restrict samples, size_t numSamples) {
    size_t totalSamples = numSamples * _numChannels;

    // Apply pre-gain if not unity
    if (_pregain != Q31_MAX) {
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] = q31_mul(samples[i], _pregain);
        }
    }

    // Cascade through active biquad filters
    // Stereo loop is unrolled to avoid per-sample ternary branch on channel index
    for (uint8_t f = 0; f < MAX_EQ_BANDS; f++) {
        if (!_params[f].enabled) continue;

        BiquadState& s0 = _filters[f].getState(0);
        BiquadState& s1 = _filters[f].getState(1);

        for (size_t i = 0; i < numSamples; i++) {
            size_t idx = i * _numChannels;
            samples[idx]     = _filters[f].processSample(samples[idx],     s0);
            if (_numChannels > 1) {
                samples[idx + 1] = _filters[f].processSample(samples[idx + 1], s1);
            }
        }
    }
}

void ParametricEQ::reset() {
    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
        _filters[i].reset();
    }
}

void ParametricEQ::setBand(uint8_t bandIndex, const EQFilterParams& params) {
    if (bandIndex >= MAX_EQ_BANDS) return;

    _params[bandIndex] = params;
    _filters[bandIndex].designFromParams(params, _sampleRate);
}

/*
void ParametricEQ::setFilterCount(uint8_t count) {
    if (count > MAX_EQ_BANDS) count = MAX_EQ_BANDS;
    _filterCount = count;
}
*/

void ParametricEQ::setPregain(int16_t pregain_db) {
    _pregainDb = pregain_db;  // Store original value for readback
    if (pregain_db == 0) {
        _pregain = Q31_MAX;
    } else {
        _pregain = db_q88_to_q31_gain(pregain_db);
    }
}
