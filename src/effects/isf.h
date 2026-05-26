/**
 * @file isf.h
 * @brief Index Selectable Filter — volume-dependent EQ switching with smooth slew
 *
 * Behavior:
 *   - Measures input RMS level continuously (or uses user override)
 *   - Selects target preset index based on level vs threshold table
 *   - Smoothly crossfades between adjacent presets (no hard switching)
 *   - Two independent instances run in series in the DSP pipeline
 *
 * Slew model:
 *   _slewIndex (float) moves toward target at _slewStep per sample.
 *   indexA = floor(_slewIndex), indexB = indexA + 1
 *   output = (1 - blend) * filterA_out + blend * filterB_out
 */

#ifndef ISF_H
#define ISF_H

#include "dsp_module.h"
#include "helper/biquad.h"
#include "eq.h"
#include "../utils/fixed_math.h"
#include <string.h>
#include <math.h>

#define ISF_SET_ALL 0
#define ISF_SET_BAND 1
#define ISF_SET_COMMON 2


// ── Data structures ───────────────────────────────────────────────────────────

/**
 * Parameters for one biquad band inside a preset.
 * Mirror of EQFilterParams but with guaranteed size for UART packing.
 */
struct ISFBandParams {
    bool     enabled = false;
    uint8_t  type    = 0;       // EQFilterType
    uint16_t f0      = 1000;    // Hz
    int16_t  gain    = 0;       // Q8.8 dB
    uint16_t Q       = 724;     // Q6.10 (0.707 * 1024)
};

/**
 * One preset = one complete EQ curve active at a given loudness range.
 * Coefficients are pre-computed on setPreset(), not at runtime.
 */
struct ISFPreset {
    int16_t      thresholdDb = -30 * 256;   // Q8.8 — RMS level to activate
    bool         valid       = false;
    ParametricEQ filters;
};

// ── IndexSelectableFilter ─────────────────────────────────────────────────────

class IndexSelectableFilter : public DspModule {
    friend class PresetManager;
    friend class ParamController;

public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName()     const override { return "ISF"; }
    uint8_t     getModuleId() const override { return _moduleId; }
    void        setModuleId(uint8_t id)      { _moduleId = id; }

    // ── Preset management ─────────────────────────────────────────────────────

    /**
     * Set one preset slot. Automatically recomputes biquad coefficients.
     * Can be called from any task — coefficients are copied atomically per preset.
     */
    void setPreset(uint8_t index, const ISFPreset& preset, uint8_t type = ISF_SET_ALL, uint8_t bandIdx = 0);

    const ISFPreset& getPreset(uint8_t index) const { return _presets[index]; }
    ISFPreset&       getPreset(uint8_t index)        { return _presets[index]; }

    void    setNumPresets(uint8_t n);
    uint8_t getNumPresets() const { return _numPresets; }

    // ── RMS / slew configuration ──────────────────────────────────────────────

    /** RMS averaging window. Longer = slower response to level changes. */
    void setRmsWindowMs(int32_t ms);

    /** Time in ms to slew one full index step. Longer = smoother transition. */
    void setSlewMs(int32_t ms);

    int32_t getRmsWindowMs() const { return _rmsMs; }
    int32_t getSlewMs()       const { return _slewMs; }

    /**
     * Manual level override. Pass ISF_OVERRIDE_AUTO (-9999.f) to use RMS.
     * Useful for testing or tying ISF to an external volume control.
     */
    static constexpr float ISF_OVERRIDE_AUTO = -9999.0f;
    void setOverrideDb(float db) { _overrideDb = db; }

    // ── Runtime state (read-only, for UI / UART reporting) ───────────────────

    float getCurrentLevelDb() const { return _currentLevelDb; }
    float getSlewIndex()      const { return _slewIndex; }
    int   getActiveA()        const { return _activeA; }
    int   getActiveB()        const { return _activeB; }

private:
    uint8_t   _moduleId    = 0x0B;
    uint8_t   _numPresets  = 0;
    ISFPreset _presets[ISF_MAX_PRESETS];

    // ── RMS detector ─────────────────────────────────────────────────────────
    float   _rmsSq         = 0.0f;
    float   _rmsCoeff      = 0.0f;     // frame-level IIR coeff
    int32_t _rmsMs         = ISF_DEFAULT_RMS_MS;
    float   _currentLevelDb = -96.0f;
    float   _overrideDb    = ISF_OVERRIDE_AUTO;

    // ── Smooth slew ──────────────────────────────────────────────────────────
    float   _slewIndex  = 0.0f;        // fractional, 0 .. numPresets-1
    float   _slewStep   = 0.0f;        // max index change per sample
    int32_t _slewMs     = ISF_DEFAULT_SLEW_MS;

    // ── Crossfade state (two parallel filter banks) ───────────────────────────
    // Layout: state[band][ch*2 + delay_tap], matches Biquad._state convention
    float   _stateA[ISF_MAX_BANDS][4]; // w0L,w1L,w0R,w1R for preset A
    float   _stateB[ISF_MAX_BANDS][4]; // same for preset B
    int     _activeA = 0;
    int     _activeB = 0;

    // ── Per-frame ramp state ──────────────────────────────────────────────────
    // blend và pgLin được ramp per-sample từ giá trị frame trước đến frame này.
    // Không ramp → bước nhảy biên độ tại mỗi frame boundary → click.
    float   _prevBlend  = 0.0f;   // blend cuối frame trước
    float   _prevPgLinA = 1.0f;   // pgLin preset A cuối frame trước
    float   _prevPgLinB = 1.0f;   // pgLin preset B cuối frame trước

    // ── Private helpers ───────────────────────────────────────────────────────

    //void recomputeCoeffs(uint8_t presetIdx);
    int  levelToTargetIndex(float levelDb) const;

    /**
     * Process a planar buffer through one preset's entire filter bank.
     * @param preset     which preset to use
     * @param bufL/bufR  planar stereo buffers, processed in-place
     */
    void applyPreset(
        const ISFPreset& preset,
        float (*stateBank)[4],
        float* __restrict bufL,
        float* __restrict bufR,
        size_t numSample);
};

#endif // ISF_H