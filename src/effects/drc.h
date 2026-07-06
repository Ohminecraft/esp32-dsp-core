/**
 * @file drc.h
 * @brief Dynamic Range Compressor (DRC) — Multi-band with crossover filters
 *
 * Architecture (simplified):
 *   - 3 modes: Fullband (1 band), 2 Band, 3 Band
 *   - 4 crossover filter types: Butterworth-1, LR2, LR4, Q-controlled-2
 *   - Per-band: threshold, ratio, attack, release, pregain, lookahead
 *   - Fast math: fast_linear_to_db + fast_db_to_gain
 */

#ifndef DRC_H
#define DRC_H

#include "dsp_module.h"
#include "helper/biquad.h"
#include "../utils/psram.h"
#include "../utils/fixed_math.h"
#include "../utils/dynamics_processor.h"
#include "config.h"

// Max sub-band frame buffer: 3 bands × frameSize × channels
static constexpr int DRC_MAX_BANDS     = 4; // fullband, low, mid, high
static constexpr int DRC_MAX_CROSSOVERS = 2;
static constexpr int DRC_DECIM         = 8;  // decimate dB/gain every N samples

struct DRCBand {
    // ── Raw params ──────────────────────────────────────────────────
    int32_t ratioX100;      // 0.01 steps, e.g. 400 = 4.00:1
    int32_t attackMs;
    int32_t releaseMs;

    // ── Cached computed ──────────────────────────────────────────────
    float thresholdDb;
    float slopeAbove;       // (1 - 1/ratio) — pre-computed per SDK
    float pregain;          // linear
    float attackCoeff; 
    float releaseCoeff;

    // ── Run-time state ──────────────────────────────────────────────
    EnvelopeState state;

    // ── Per-band lookahead delay buffer ──────────────────────────────
    static constexpr int DRC_LOOKAHEAD_MAX = 960; // 10ms @ 96kHz
    float   lookaheadMs      = 0.0f;
    int     lookaheadSamples = 0;
    int     laWriteIdx       = 0;
    float*  laDelayBuf       = nullptr; // PSRAM: (DRC_LOOKAHEAD_MAX+1)*2 floats
};

class DRC : public DspModule {
    friend class PresetManager;
    friend class ParamController;
    friend class Display;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "DRC"; }
    uint8_t getModuleId() const override { return MODULE_ID_DRC; }

    // ── Mode & Crossover ─────────────────────────────────────────────
    void setMode(DRCMode mode);
    void setCrossoverType(DRCCrossoverType cfType);
    void setCrossoverFreq(uint8_t idx, int32_t hz);
    void setCrossoverQ(uint8_t idx, int32_t q_q610);  // Q6.10 format

    // ── Per-band params (band: 0-2 = bands, mapping depends on mode) ────────
    void setThreshold(uint8_t band, float db);
    void setRatio(uint8_t band, int32_t ratio_x100);
    void setAttackTime(uint8_t band, int32_t ms);
    void setReleaseTime(uint8_t band, int32_t ms);
    void setPregain(uint8_t band, float gaindb);
    void setLookahead(uint8_t band, float ms);

    // ---- Runtime state getters (for live meter) ----
    float getBandGainDb(uint8_t band) const {
        if (band >= DRC_MAX_BANDS) return 0.0f;
        float g = _bands[band].state.gainLinear;
        return (g > 0.0f) ? (20.0f * log10f(g)) : -96.0f;
    }

    static constexpr int MAX_BANDS = DRC_MAX_BANDS;  // For friend class access

private:
    DRCMode          _mode;
    DRCCrossoverType _cfType;
    int32_t          _fc[2];         // crossover frequencies Hz
    int32_t          _qLp;           // Q LP (Q6.10) - only for QCTRL mode
    int32_t          _qHp;           // Q HP (Q6.10) - only for QCTRL mode

    DRCBand _bands[DRC_MAX_BANDS];

    // ── Crossover filters ────────────────────────────────────────────
    // LR4/QCTRL: 2 cascaded biquads per path
    Biquad _xoverLp[DRC_MAX_CROSSOVERS][2];  // LP stages [crossover][stage]
    Biquad _xoverHp[DRC_MAX_CROSSOVERS][2];  // HP stages [crossover][stage]

    // ── Sub-band buffer pointers (mapped from SharedScratchpad) ────────
    float* _subBandPtr[DRC_MAX_BANDS - 1] = {nullptr, nullptr, nullptr};

    void recalcBand(uint8_t band);
    void designCrossover(uint8_t idx);
    int  getNumBands() const;       // number of bands from mode (1, 2, or 3)
    void applyBandDRC(DRCBand& b, float* buf, size_t numSamples);
};

#endif // DRC_H