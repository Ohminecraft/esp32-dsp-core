/**
 * @file dsp_pipeline.cpp
 * @brief DSP Pipeline implementation
 */

#include "dsp_pipeline.h"
#include "../utils/fixed_math.h"

void DspPipeline::init(int32_t sampleRate, int32_t numChannels) {
    _chain[0]  = &_preGain;
    _chain[1]  = &_preeq;
    _chain[2]  = &_compander;
    _chain[3]  = &_exciter;
    _chain[4]  = &_dynamicBass;
    _chain[5]  = &_dynamicEq;
    _chain[6]  = &_isf1;
    _chain[7]  = &_isf2;
    _chain[8]  = &_eqDsp_1;
    _chain[9]  = &_eqDsp_2;
    _chain[10]  = &_leftRightEq;
    _chain[11] = &_drc;
    _chain[12] = &_postGain;

    // Assign module IDs for instances that share a class
    _preGain.setModuleId(MODULE_ID_PRE_GAIN);
    _postGain.setModuleId(MODULE_ID_POST_GAIN);
    _preeq.setModuleId(MODULE_ID_PRE_EQ);
    _eqDsp_1.setModuleId(MODULE_ID_EQ_DSP_1);
    _eqDsp_2.setModuleId(MODULE_ID_EQ_DSP_2);
    _isf1.setModuleId(MODULE_ID_ISF_1);
    _isf2.setModuleId(MODULE_ID_ISF_2);

    // Initialize all modules and distribute scratchpad
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        _chain[i]->setScratchpad(&scratchpad);
        _chain[i]->init(sampleRate, numChannels);
    }

    // Default enabled: pre + post gain + pre eq
    _preGain.enable();
    _preeq.enable();
    _preeq.setNumBand(3);
    _postGain.enable();
}

void IRAM_ATTR DspPipeline::processFrame(
    float* __restrict samples, size_t numSamples)
{
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