/**
 * @file dsp_pipeline.h
 * @brief DSP Pipeline orchestrator — manages all 12 modules in processing order
 *
 * Chain order:
 *  [00] Pre Gain
 *  [01] Compander
 *  [02] Exciter
 *  [03] Dynamic Bass
 *  [04] Dynamic EQ
 *  [05] ISF 1   ← Index Selectable Filter (replaces AutoEQ)
 *  [06] ISF 2   ← second independent ISF instance
 *  [07] EQ DSP 1
 *  [08] EQ DSP 2
 *  [09] Left Right EQ
 *  [10] DRC
 *  [11] Post Gain
 */

#ifndef DSP_PIPELINE_H
#define DSP_PIPELINE_H

#include "dsp_module.h"
#include "compander.h"
#include "exciter.h"
#include "dynamic_bass.h"
#include "eq.h"
#include "dynamic_eq.h"
#include "isf.h"          // replaces auto_eq.h
#include "drc.h"
#include "volume.h"
#include "leftrighteq.h"

struct SharedScratchpad {
    float __attribute__((aligned(16))) buf1[DSP_FRAME_SAMPLES];
    float __attribute__((aligned(16))) buf2[DSP_FRAME_SAMPLES];
    float __attribute__((aligned(16))) buf3[DSP_FRAME_SAMPLES];
    float __attribute__((aligned(16))) buf4[DSP_FRAME_SAMPLES];
    float __attribute__((aligned(16))) buf5[DSP_FRAME_SAMPLES];
    float __attribute__((aligned(16))) buf6[DSP_FRAME_SAMPLES];
};

class DspPipeline {
public:
    SharedScratchpad scratchpad;

    void init(int32_t sampleRate, int32_t numChannels);

    void processFrame(float* __restrict samples, size_t numSamples);

    // ── Module Accessors ──────────────────────────────────────────────────────

    Compander&               getCompander()   { return _compander; }
    Exciter&                 getExciter()     { return _exciter; }
    DynamicBass&             getDynamicBass() { return _dynamicBass; }
    DynamicEQ&               getDynamicEq()   { return _dynamicEq; }
    IndexSelectableFilter&   getIsf1()        { return _isf1; }
    IndexSelectableFilter&   getIsf2()        { return _isf2; }
    ParametricEQ&            getEqDsp_1()     { return _eqDsp_1; }
    ParametricEQ&            getEqDsp_2()     { return _eqDsp_2; }
    LeftRightEQ&             getLeftRightEq() { return _leftRightEq; }
    DRC&                     getDrc()         { return _drc; }
    VolumeControl&           getPostGain()    { return _postGain; }
    VolumeControl&           getPreGain()     { return _preGain; }

    DspModule* getModuleById(uint8_t moduleId);
    DspModule** getChain()         { return _chain; }
    size_t getChainLength()  const { return CHAIN_LENGTH; }

private:
    // NOTE: DSP_MODULE_COUNT must be 12 in config.h
    static const size_t CHAIN_LENGTH = DSP_MODULE_COUNT;

    VolumeControl           _preGain;       // [00]
    Compander               _compander;     // [01]
    Exciter                 _exciter;       // [02]
    DynamicBass             _dynamicBass;   // [03]
    DynamicEQ               _dynamicEq;     // [04]
    IndexSelectableFilter   _isf1;          // [05] ISF instance 1
    IndexSelectableFilter   _isf2;          // [06] ISF instance 2
    ParametricEQ            _eqDsp_1;       // [07]
    ParametricEQ            _eqDsp_2;       // [08]
    LeftRightEQ             _leftRightEq;   // [09]
    DRC                     _drc;           // [10]
    VolumeControl           _postGain;      // [11]

    DspModule* _chain[CHAIN_LENGTH];
};

#endif // DSP_PIPELINE_H