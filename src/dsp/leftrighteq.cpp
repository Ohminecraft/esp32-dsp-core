/*
 * @file leftrighteq.cpp
 * @brief Left Right Eq - For 1.1 Speaker
*/

#include "leftrighteq.h"
#include <string.h>

void LeftRightEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    _eqLeft .init(sampleRate, numChannels);
    _eqRight.init(sampleRate, numChannels);
    _eqLeft .enable();
    _eqRight.enable();

    reset();
}

// Reset
void LeftRightEQ::reset() {
    _eqLeft .reset();
    _eqRight.reset();
}

// Process
void IRAM_ATTR LeftRightEQ::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;

    // Apply pre-gain for Left
    if (_eqLeft._pregain != 1.0f) {
        for (size_t i = 0; i < numSamples; i++) {
            samples[i * 2] *= _eqLeft._pregain;
        }
    }

    // Apply pre-gain for Right
    if (_eqRight._pregain != 1.0f) {
        for (size_t i = 0; i < numSamples; i++) {
            samples[i * 2 + 1] *= _eqRight._pregain;
        }
    }

    // Process through cascaded biquads for Left (channel 0)
    for (uint8_t b = 0; b < MAX_EQ_BANDS; b++) {
        if (_eqLeft._params[b].enabled) {
            for (size_t i = 0; i < numSamples; i++) {
                samples[i * _numChannels] = _eqLeft._filters[b].processSample(samples[i * 2], 0);
            }
        }
        if (_eqRight._params[b].enabled) {
            for (size_t i = 0; i < numSamples; i++) {
                samples[i * _numChannels + 1] = _eqRight._filters[b].processSample(samples[i * 2 + 1], 1);
            }
        }
    }
}

// Set EQ Params

void LeftRightEQ::setEqLeft(uint8_t band, const EQFilterParams& params) {
    _eqLeft.setBand(band, params);
}

void LeftRightEQ::setEqRight(uint8_t band, const EQFilterParams& params) {
    _eqRight.setBand(band, params);
}

