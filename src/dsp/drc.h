/**
 * @file drc.h
 * @brief Dynamic Range Compressor (DRC) — Multi-band with crossover filters
 *
 * Architecture (matching MVSilicon DRC v4.1.0 logic):
 *   - Up to 3 frequency bands + optional fullband stage
 *   - Linkwitz-Riley crossover filters (LR2 default, LR4 optional)
 *   - Per-band: threshold, ratio, attack, release, pregain
 *   - Fast math: fast_linear_to_db + fast_db_to_gain (no log10f/powf in hot path)
 *   - Decimated dB computation every 8 samples
 *
 * Parameter Index Map (paramId byte in UART):
 *   Upper nibble: band (0=band1, 1=band2, 2=band3, 3=fullband)
 *   Lower nibble: param (0=threshold, 1=ratio, 2=attack, 3=release, 4=pregain)
 *
 *   Special (band-independent):
 *   0x10 = mode, 0x11 = cfType, 0x12 = fc1, 0x13 = fc2, 0x14 = qLp, 0x15 = qHp
 */

#ifndef DRC_H
#define DRC_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"
#include "../utils/dynamics_processor.h"
#include "config.h"

// Max sub-band frame buffer: 3 bands × frameSize × channels
// At 256 samples stereo: 3 * 256 * 2 * 4 = 6144 bytes (static)
static constexpr int DRC_MAX_BANDS     = 3;
static constexpr int DRC_MAX_CROSSOVERS = 2;
static constexpr int DRC_DECIM         = 8;  // decimate dB/gain every N samples

struct DRCBand {
    // ── Raw params ──────────────────────────────────────────────────
    int32_t thresholdDbInt; // 0.01 dB steps, e.g. -1500 = -15.00 dB
    int32_t ratioX100;      // 0.01 steps, e.g. 400 = 4.00:1
    int32_t attackMs;
    int32_t releaseMs;
    int32_t pregainQ412;    // Q4.12, 4096 = 1.0 (0dB)

    // ── Cached computed ──────────────────────────────────────────────
    float thresholdDb;
    float slopeAbove;       // (1 - 1/ratio) — pre-computed per SDK
    float pregain;          // linear
    float attackCoeff;
    float releaseCoeff;

    // ── Run-time state ──────────────────────────────────────────────
    EnvelopeState state;
};

class DRC : public DspModule {
    friend class PresetManager;
    friend class ParamController;
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

    // ── Per-band params (band: 0-2 = sub-bands, 3 = fullband) ────────
    void setThreshold(uint8_t band, int32_t db_001);
    void setRatio(uint8_t band, int32_t ratio_x100);
    void setAttackTime(uint8_t band, int32_t ms);
    void setReleaseTime(uint8_t band, int32_t ms);
    void setPregain(uint8_t band, int32_t gain_q412);

private:
    static const int MAX_BANDS = 4;  // [0]=band1 [1]=band2 [2]=band3 [3]=fullband

    DRCMode          _mode;
    DRCCrossoverType _cfType;
    int32_t          _fc[2];         // crossover frequencies Hz
    int32_t          _qLp;           // Q LP (Q6.10)
    int32_t          _qHp;           // Q HP (Q6.10)
    int32_t          _numCf;         // 0, 1, or 2 active crossovers
    bool             _fullbandOn;    // true if fullband stage active

    DRCBand _bands[MAX_BANDS];

    // ── Crossover filters ────────────────────────────────────────────
    // LR2: 2 biquads per crossover (1 LP + 1 HP)
    // LR4: 4 biquads per crossover (2 LP + 2 HP) — each stage = 1 Biquad
    Biquad _xoverLp[DRC_MAX_CROSSOVERS][2];  // LP stages [crossover][stage]
    Biquad _xoverHp[DRC_MAX_CROSSOVERS][2];  // HP stages [crossover][stage]

    // ── Sub-band buffer pointers (mapped from SharedScratchpad) ────────
    // DRC runs at chain slot 8 (after all EQ modules), so buf1/buf2/buf3
    // are safe to reuse here. Only needed in multi-band mode.
    float* _subBandPtr[DRC_MAX_BANDS] = {nullptr, nullptr, nullptr};

    void recalcBand(uint8_t band);
    void designCrossover(uint8_t idx);
    int  getNumBands() const;       // number of sub-bands from mode
    void applyBandDRC(DRCBand& b, float* buf, size_t numSamples);
};

#endif // DRC_H
