/**
 * @file dsp_pipeline.cpp
 * @brief DSP Pipeline implementation
 */

#include "dsp_pipeline.h"
#include "../utils/fixed_math.h"

void DspPipeline::init(int32_t sampleRate, int32_t numChannels) {
    // Build chain in exact processing order
    _chain[0]  = &_preGain;
    _chain[1]  = &_compander;
    _chain[2]  = &_exciter;
    _chain[3]  = &_dynamicBass;
    _chain[4]  = &_dynamicEq;
    _chain[5]  = &_eqDsp_1;
    _chain[6]  = &_eqDsp_2;
    _chain[7]  = &_leftRightEq;
    _chain[8]  = &_drc;
    _chain[9]  = &_postGain;

    // Set module IDs for EQ and Volume instances
    _preGain.setModuleId(MODULE_ID_PRE_GAIN);
    _eqDsp_1.setModuleId(MODULE_ID_EQ_DSP_1);
    _eqDsp_2.setModuleId(MODULE_ID_EQ_DSP_2);
    _postGain.setModuleId(MODULE_ID_POST_GAIN);

    // Initialize all modules and assign scratchpad
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        _chain[i]->setScratchpad(&scratchpad);
        _chain[i]->init(sampleRate, numChannels);
    }
    
    _preGain.enable();
    _postGain.enable();
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