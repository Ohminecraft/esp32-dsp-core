/**
 * @file dsp_pipeline.cpp
 * @brief DSP Pipeline implementation
 */

#include "dsp_pipeline.h"
#include "../utils/fixed_math.h"

void DspPipeline::init(int32_t sampleRate, int32_t numChannels) {
    // Build chain in exact processing order
    _chain[0]  = &_compander;     // UNSTABLE
    _chain[1]  = &_exciter;       // STABLE
    _chain[2]  = &_dynamicBass;   // STABLE
    _chain[3]  = &_dynamicEq;     // STABLE
    _chain[4]  = &_eqDsp_1;       // STABLE
    _chain[5]  = &_eqDsp_2;       // STABLE
    _chain[6]  = &_drc;           // UNSTABLE
    _chain[7]  = &_volume;        // STABLE

    // Set module IDs for EQ instances
    _eqDsp_1.setModuleId(MODULE_ID_EQ_DSP_1);
    _eqDsp_2.setModuleId(MODULE_ID_EQ_DSP_2);

    // Initialize all modules
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        _chain[i]->init(sampleRate, numChannels);
    }
    
    _volume.enable();
}

void IRAM_ATTR DspPipeline::processFrame(float* __restrict samples, size_t numSamples) {
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        if (_chain[i]->isEnabled()) {
            _chain[i]->process(samples, numSamples);
        }
    }
}

DspModule* DspPipeline::getModuleById(uint8_t moduleId) {
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        if (_chain[i]->getModuleId() == moduleId) {
            return _chain[i];
        }
    }
    return nullptr;
}