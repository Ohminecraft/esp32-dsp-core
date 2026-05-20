/**
 * @file dsp_pipeline.h
 * @brief DSP Pipeline orchestrator — manages all 12 modules in exact MVSilicon order
 */

#ifndef DSP_PIPELINE_H
#define DSP_PIPELINE_H

#include "dsp_module.h"
//#include "noise_gate.h"
#include "compander.h"
#include "exciter.h"
#include "dynamic_bass.h"
//#include "bass_classic.h"
//#include "stereo_widener.h"
#include "eq.h"
#include "dynamic_eq.h"
#include "drc.h"
#include "volume.h"
#include "leftrighteq.h"
//#include "soft_clip.h"

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

    /**
     * Initialize all modules with given audio parameters.
     */
    void init(int32_t sampleRate, int32_t numChannels);

    /**
     * Process a frame of stereo interleaved Q31 audio.
     * Runs all enabled modules in pipeline order.
     *
     * @param samples Stereo interleaved buffer: L0,R0,L1,R1,...
     * @param numSamples Number of sample PAIRS (frames)
     */
    void processFrame(float* __restrict samples, size_t numSamples);

    // ---- Module Accessors ----
    Compander&       getCompander()      { return _compander; }
    Exciter&         getExciter()        { return _exciter; }
    DynamicBass&     getDynamicBass()    { return _dynamicBass; }
    DynamicEQ&       getDynamicEq()      { return _dynamicEq; }
    ParametricEQ&    getEqDsp_1()        { return _eqDsp_1; }
    ParametricEQ&    getEqDsp_2()        { return _eqDsp_2; }
    LeftRightEQ&     getLeftRightEq()    { return _leftRightEq; }
    DRC&             getDrc()            { return _drc; }
    VolumeControl&   getPostGain()       { return _postGain; }
    VolumeControl&   getPreGain()        { return _preGain; }

    /**
     * Get module by UART module ID.
     * @return Pointer to module, or nullptr if ID not found.
     */
    DspModule* getModuleById(uint8_t moduleId);

    /**
     * Get the chain array for iteration.
     */
    DspModule** getChain() { return _chain; }
    size_t getChainLength() const { return CHAIN_LENGTH; }

private:
    static const size_t CHAIN_LENGTH = DSP_MODULE_COUNT;

    VolumeControl  _preGain;       // [00] Pre Gain
    Compander      _compander;     // [01] Compander
    Exciter        _exciter;       // [02] Harmonic Exciter
    DynamicBass    _dynamicBass;   // [03] Dynamic Bass
    DynamicEQ      _dynamicEq;     // [04] Dynamic EQ (dual-EQ system)
    ParametricEQ   _eqDsp_1;       // [05] EQ1 (main parametric EQ)
    ParametricEQ   _eqDsp_2;       // [06] EQ2 (post EQ / sound signature)
    LeftRightEQ    _leftRightEq;   // [07] Left Right EQ
    DRC            _drc;           // [08] DRC (dynamic range compression)
    VolumeControl  _postGain;      // [09] Post Gain

    DspModule* _chain[CHAIN_LENGTH];
};

#endif // DSP_PIPELINE_H
