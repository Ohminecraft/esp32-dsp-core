/**
 * @file compander.h
 * @brief Compander (Compressor/Expander) — dynamic range processor
 */

#ifndef COMPANDER_H
#define COMPANDER_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"
#include "../utils/dynamics_processor.h"

class Compander : public DspModule {
    friend class PresetManager;
    friend class ParamController;
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

    void recalcCoeffs();
};

#endif // COMPANDER_H
