/**
 * @file dynamic_eq.h
 * @brief Dynamic EQ — Level-dependent dual-EQ system (Float32)
 *
 * Ported & re-implemented from Shanghai Mountain View Silicon NDS32 binary
 * (dynamic_eq.o, v1.1.0 by ZHAO Ying / Alfred).
 *
 * Algorithm overview (reverse-engineered from binary):
 *
 *   The module watches the energy of a signal (pcm_watch in the original C API,
 *   or an internal pointer set via setWatchBuffer()). When no watch buffer is
 *   set the input buffer is watched instead — same behaviour as the original
 *   when pcm_watch == pcm_in.
 *
 *   Energy detection:
 *     - Per-frame RMS computed over all samples & channels
 *     - Converted to dBFS via 20·log10(rms)
 *     - Smoothed by a one-pole IIR (separate attack / release time constants)
 *       using the formula from the original binary:
 *         α = exp(−frameSize / (Fs · T_ms / 1000))
 *
 *   Three-zone state machine (matches original Case 1 / 2 / 3):
 *
 *     Case 1  low_th < normal_th < high_th
 *     ─────────────────────────────────────────────────────────────────────
 *     [LOW EQ full]──ramp──[NO PROC]──ramp──[HIGH EQ full]
 *          ↑ low_th           ↑ normal_th         ↑ high_th
 *
 *     Case 2  normal_th == high_th  (direct LOW ↔ HIGH transition)
 *     Case 3  low_th  == normal_th  (direct LOW ↔ HIGH transition)
 *
 *   Output mixing (per-sample):
 *     out = dry·αFlat  +  eqLow·αLow  +  eqHigh·αHigh
 *     where αFlat = clamp(1 − αLow − αHigh, 0, 1)
 *
 *   Transition smoothing uses frame-rate coefficients so time constants are
 *   independent of frame size.
 */

#ifndef DYNAMIC_EQ_H
#define DYNAMIC_EQ_H

#include "dsp_module.h"
#include "eq.h"
#include "../utils/fixed_math.h"
#include "../utils/debug_log.h"
#include "config.h"

class DynamicEQ : public DspModule {
    friend class PresetManager;
    friend class ParamController;

public:
    DynamicEQ() = default;

    // -------------------------------------------------------------------------
    // DspModule interface
    // -------------------------------------------------------------------------
    void init(int32_t sampleRate, int32_t numChannels) override;

    /**
     * @brief Process one frame of interleaved float audio in-place.
     *
     * Energy is measured from the watch buffer (set via setWatchBuffer()).
     * If no watch buffer is registered the input buffer is watched (default).
     *
     * @param samples  Stereo interleaved buffer: L0,R0,L1,R1,...  (in-place)
     * @param numSamples  Number of frames (sample pairs), NOT total samples.
     */
    void process(float* __restrict samples, size_t numSamples) override;

    void reset() override;

    const char* getName()    const override { return "DynamicEQ"; }
    uint8_t     getModuleId() const override { return MODULE_ID_DYNAMIC_EQ; }

    // -------------------------------------------------------------------------
    // Optional: separate watch buffer (matches original pcm_watch parameter)
    // Call once before the processing loop and clear with nullptr when done.
    // -------------------------------------------------------------------------
    void setWatchBuffer(const float* watchBuf) { _watchBuf = watchBuf; }
    void clearWatchBuffer()                    { _watchBuf = nullptr;  }

    // -------------------------------------------------------------------------
    // Threshold & timing API  (units match original binary)
    //   db_001 : value in 0.01 dB steps  (e.g. -4000 = -40.00 dBFS)
    //   ms     : milliseconds
    // -------------------------------------------------------------------------
    void setLowEnergyThreshold   (int32_t db_001);
    void setNormalEnergyThreshold(int32_t db_001);
    void setHighEnergyThreshold  (int32_t db_001);
    void setAttackTime (int32_t ms);
    void setReleaseTime(int32_t ms);

    // Convenience: read back current smoothed energy (dBFS) and blend factors
    float getEnergyDb()   const { return _energyDb;   }
    float getAlphaLow()   const { return _alphaLow;   }
    float getAlphaHigh()  const { return _alphaHigh;  }

    // -------------------------------------------------------------------------
    // EQ band configuration — delegates to inner ParametricEQ objects
    // -------------------------------------------------------------------------
    ParametricEQ& getEqLow()  { return _eqLow;  }
    ParametricEQ& getEqHigh() { return _eqHigh; }

    void setEqLowBand (uint8_t band, const EQFilterParams& params);
    void setEqHighBand(uint8_t band, const EQFilterParams& params);

private:
    // ---- Inner EQ engines ------------------------------------------------
    ParametricEQ _eqLow;
    ParametricEQ _eqHigh;

    // ---- Watch buffer (nullptr → watch input) ----------------------------
    const float* _watchBuf = nullptr;

    // ---- Energy follower -------------------------------------------------
    float   _energyDb    = -96.0f;  // smoothed energy in dBFS
    float   _rmsCoeff    = 0.0f;    // per-sample RMS IIR coeff (20 ms window)
    float   _rmsEnergySq = 0.0f;    // one-pole IIR state for rms²

    // ---- Blend factors ---------------------------------------------------
    float   _alphaLow  = 0.0f;     // [0..1]  contribution of eqLow
    float   _alphaHigh = 0.0f;     // [0..1]  contribution of eqHigh

    // ---- Transition smoothing (frame-rate) --------------------------------
    float   _attackCoeff  = 0.0f;
    float   _releaseCoeff = 0.0f;
    int32_t _attackMs     = 50;
    int32_t _releaseMs    = 200;

    // ---- Thresholds (0.01 dB units) --------------------------------------
    int32_t _lowThreshDb    = -4000;
    int32_t _normalThreshDb = -2000;
    int32_t _highThreshDb   =  -600;

    // ---- Static processing buffers (IRAM, avoids stack pressure) ---------
    // Declared static so they live in DRAM and are never stack-allocated
    // inside the ISR-adjacent audio task.
    static float _dryBuf [DSP_FRAME_SAMPLES];   // copy of input
    static float _wetLow [DSP_FRAME_SAMPLES];   // eqLow output
    static float _wetHigh[DSP_FRAME_SAMPLES];   // eqHigh output

    // ---- Internal helpers ------------------------------------------------
    void  recalcCoeffs();

    /**
     * Compute per-frame RMS in dBFS from a float buffer.
     * @param buf     Interleaved float samples in [-1, 1]
     * @param total   Total number of scalar samples (numFrames * numChannels)
     */
    float computeFrameRms(const float* buf, size_t total) const;

    /**
     * Compute target blend alphas based on current dBFS and the three-zone
     * state machine (Case 1 / 2 / 3 from original header).
     */
    void computeTargetAlphas(float energyDb,
                             float& outTargetLow,
                             float& outTargetHigh) const;
};

#endif // DYNAMIC_EQ_H