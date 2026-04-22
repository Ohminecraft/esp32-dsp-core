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
    _pregain = (1 << 27);  // Unity gain in Q4.27
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
    if (_pregain != (1 << 27)) {
        for (size_t i = 0; i < totalSamples; i++) {
            samples[i] = q31_mul_q4_27(samples[i], _pregain);
        }
    }

    for (size_t s = 0; s < numSamples; s++) {
        size_t idx = s * _numChannels;

        for (uint8_t b = 0; b < MAX_EQ_BANDS; b++) {
            if (_params[b].enabled) {
                samples[idx] = _filters[b].processSample(samples[idx], _filters[b].getState(0));
                 if (_numChannels > 1) {
                    samples[idx + 1] = _filters[b].processSample(samples[idx + 1], _filters[b].getState(1));
                }
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
        _pregain = (1 << 27);
    } else {
        _pregain = db_q88_to_q4_27_gain(pregain_db);
    }
}
