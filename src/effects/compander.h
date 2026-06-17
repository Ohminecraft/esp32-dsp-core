/**
 * @file compander.h
 * @brief Compander (Compressor/Expander) — dynamic range processor
 */

#ifndef COMPANDER_H
#define COMPANDER_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"
#include "../utils/psram.h"
#include "../utils/dynamics_processor.h"

class Compander : public DspModule {
    friend class PresetManager;
    friend class ParamController;
    friend class Display;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "Compander"; }
    uint8_t getModuleId() const override { return MODULE_ID_COMPANDER; }

    // ---- Parameter API ----
    void setThreshold(int32_t db_001);
    void setRatioBelow(int32_t ratio_q88);
    void setRatioAbove(int32_t ratio_q88);
    void setAttackTime(int32_t ms);
    void setReleaseTime(int32_t ms);
    void setPregain(int32_t gain_q412);
    // Lookahead: delay signal by N ms so gain is ready before transient arrives.
    // 0.0 = disabled (no latency). Typical: 3–8 ms.
    // Changing this value calls reset() internally to flush the delay buffer.
    void setLookahead(float ms);

    // ---- Runtime state getters (for live meter) ----
    float getEnvLinear() const { return _state.envelope;   }  // 0..1 linear peak
    float getGainDb()    const {
        // gainLinear → dB: log2 approximation không dùng powf
        float g = _state.gainLinear;
        return (g > 0.0f) ? (20.0f * log10f(g)) : -96.0f;
    }

private:
    // ── Raw parameter storage (written by setters) ──
    int32_t _thresholdDbInt;
    int32_t _ratioBelowQ88;
    int32_t _ratioAboveQ88;
    int32_t _attackMs;
    int32_t _releaseMs;
    int32_t _pregainQ412;

    // ── Cached computed values (recalculated by recalcCoeffs) ──
    float _thresholdDb;     // float dB
    float _slopeAbove;      // (1 - 1/R_above) — pre-computed per SDK
    float _slopeBelow;      // (1 - 1/R_below) — pre-computed per SDK
    float _pregain;         // linear pregain
    float _attackCoeff;     // envelope attack coefficient
    float _releaseCoeff;    // envelope release coefficient

    // ── Run-time state ──
    EnvelopeState _state;

    // ── Lookahead delay buffer ──────────────────────────────────────────────
    // Allocated in PSRAM via PSRAM_MALLOC to avoid DRAM overflow (~7.7 KB).
    // _lookaheadSamples = 0 → disabled, no latency, path giống cũ.
    static constexpr int COMP_LOOKAHEAD_MAX = 960; // 10ms @ 96kHz
    float   _lookaheadMs      = 0.0f;
    int     _lookaheadSamples = 0;
    int     _laWriteIdx       = 0;
    float*  _laDelayBuf       = nullptr; // PSRAM: (COMP_LOOKAHEAD_MAX+1)*2 floats

    void recalcCoeffs();
};

#endif // COMPANDER_H