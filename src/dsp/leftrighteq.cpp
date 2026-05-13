/*
 * @file leftrighteq.cpp
 * @brief Left Right Eq - For 1.1 Speaker
*/

#include "leftrighteq.h"
#include "dsp_pipeline.h"
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

    bool hasActiveLeft = false;
    bool hasActiveRight = false;
    for (uint8_t b = 0; b < MAX_EQ_BANDS; b++) {
        if (_eqLeft._params[b].enabled) hasActiveLeft = true;
        if (_eqRight._params[b].enabled) hasActiveRight = true;
    }

    if (!hasActiveLeft && !hasActiveRight && _eqLeft._pregain == 1.0f && _eqRight._pregain == 1.0f) {
        return;
    }

    if (_scratchpad && _numChannels == 2) {
        float* bufL = _scratchpad->buf3;  // buf3/buf4: reserved for LeftRightEQ (runs after DynamicEQ)
        float* bufR = _scratchpad->buf4;

        // De-interleave and apply pregain simultaneously
        float gainL = _eqLeft._pregain;
        float gainR = _eqRight._pregain;
        for (size_t i = 0; i < numSamples; i++) {
            bufL[i] = samples[i * 2] * gainL;
            bufR[i] = samples[i * 2 + 1] * gainR;
        }

        // Process through cascaded biquads for Left
        if (hasActiveLeft) {
            for (uint8_t b = 0; b < MAX_EQ_BANDS; b++) {
                if (_eqLeft._params[b].enabled) {
                    _eqLeft._filters[b].processPlanarChannel(bufL, numSamples, 0);
                }
            }
        }

        // Process through cascaded biquads for Right
        if (hasActiveRight) {
            for (uint8_t b = 0; b < MAX_EQ_BANDS; b++) {
                if (_eqRight._params[b].enabled) {
                    _eqRight._filters[b].processPlanarChannel(bufR, numSamples, 1);
                }
            }
        }

        // Re-interleave
        for (size_t i = 0; i < numSamples; i++) {
            samples[i * 2]     = bufL[i];
            samples[i * 2 + 1] = bufR[i];
        }
    } else {
        // Fallback Mono/No Scratchpad
        if (_eqLeft._pregain != 1.0f) {
            for (size_t i = 0; i < numSamples; i++) samples[i * 2] *= _eqLeft._pregain;
        }
        if (_eqRight._pregain != 1.0f) {
            for (size_t i = 0; i < numSamples; i++) samples[i * 2 + 1] *= _eqRight._pregain;
        }

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
}

// Set EQ Params

void LeftRightEQ::setEqLeft(uint8_t band, const EQFilterParams& params) {
    _eqLeft.setBand(band, params);
}

void LeftRightEQ::setEqRight(uint8_t band, const EQFilterParams& params) {
    _eqRight.setBand(band, params);
}

