/**
 * @file dsp_pipeline.h
 * @brief DSP Pipeline orchestrator — manages all 12 modules in exact MVSilicon order
 */

#ifndef DSP_PIPELINE_H
#define DSP_PIPELINE_H

#include "dsp_module.h"
#include "noise_gate.h"
#include "compander.h"
#include "exciter.h"
#include "virtual_bass.h"
#include "bass_classic.h"
#include "stereo_widener.h"
#include "eq.h"
#include "dynamic_eq.h"
#include "drc.h"
#include "volume.h"
#include "soft_clip.h"

class DspPipeline {
public:
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
    void processFrame(q31_t* __restrict samples, size_t numSamples);

    // ---- Module Accessors ----
    NoiseGate&       getNoiseGate()      { return _noiseGate; }
    Compander&       getCompander()      { return _compander; }
    Exciter&         getExciter()        { return _exciter; }
    VirtualBass&     getVirtualBass()    { return _virtualBass; }
    BassClassic&     getBassClassic()    { return _bassClassic; }
    StereoWidener&   getStereoWidener()  { return _stereoWidener; }
    ParametricEQ&    getEqDsp()          { return _eqDsp; }
    DynamicEQ&       getDynamicEq()      { return _dynamicEq; }
    ParametricEQ&    getEqDspPost()      { return _eqDspPost; }
    DRC&             getDrc()            { return _drc; }
    VolumeControl&   getVolume()         { return _volume; }
    SoftClipper&     getSoftClipper()    { return _softClipper; }

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
    static const size_t CHAIN_LENGTH = 12;

    // Pipeline modules in EXACT processing order (MVSilicon v0.3.2)
    NoiseGate      _noiseGate;     // [01] Noise Gate
    Compander      _compander;     // [02] Compander
    Exciter        _exciter;       // [03] Harmonic Exciter
    VirtualBass    _virtualBass;   // [04] Virtual Bass
    BassClassic    _bassClassic;   // [05] Bass Classic
    StereoWidener  _stereoWidener; // [06] 3D / Stereo Widener
    ParametricEQ   _eqDsp;         // [07] EQ1 (main parametric EQ)
    DynamicEQ      _dynamicEq;     // [08] Dynamic EQ (dual-EQ system)
    ParametricEQ   _eqDspPost;     // [09] EQ2 (post EQ / sound signature)
    DRC            _drc;           // [10] DRC (dynamic range compression)
    VolumeControl  _volume;        // [11] Master Volume
    SoftClipper    _softClipper;   // [12] Soft Clipper

    DspModule* _chain[CHAIN_LENGTH];
};

#endif // DSP_PIPELINE_H
