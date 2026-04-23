/**
 * @file eq.cpp
 * @brief Parametric EQ implementation
 */

#include "eq.h"
#include <string.h>

void ParametricEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    _pregain = 1.0f;
    _pregainDb = 0;
    
    for (int i = 0; i < MAX_EQ_BANDS; i++) {
        _params[i].enabled = 0;
        _params[i].type = EQ_FILTER_TYPE_PEAKING;
        _params[i].f0 = 1000;
        _params[i].Q = FLOAT_TO_Q_Q610(0.707f);
        _params[i].gain = 0;
        _filters[i].reset();
    }
}

void IRAM_ATTR ParametricEQ::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;
    processInternal(samples, numSamples);
}

void IRAM_ATTR ParametricEQ::processInternal(float* __restrict samples, size_t numSamples) {
    // 1. Apply pre-gain
    if (_pregain != 1.0f) {
        size_t total = numSamples * _numChannels;
        for (size_t i = 0; i < total; i++) {
            samples[i] *= _pregain;
        }
    }

    // 2. Process through cascaded biquads
    for (uint8_t b = 0; b < MAX_EQ_BANDS; b++) {
        if (_params[b].enabled) {
            _filters[b].process(samples, numSamples);
        }
    }
}

void ParametricEQ::reset() {
    for (int i = 0; i < MAX_EQ_BANDS; i++) {
        _filters[i].reset();
    }
}

void ParametricEQ::setBand(uint8_t bandIndex, const EQFilterParams& params) {
    if (bandIndex >= MAX_EQ_BANDS) return;
    _params[bandIndex] = params;
    _filters[bandIndex].designFromParams(params, _sampleRate);
}

void ParametricEQ::setPregain(int16_t pregain_db) {
    _pregainDb = pregain_db;
    _pregain = db_q88_to_linear_gain(pregain_db);
}
