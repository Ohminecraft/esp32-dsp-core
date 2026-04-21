/**
 * @file compander.h
 * @brief Compander (Compressor + Expander) module
 *
 * Based on MVSilicon CompanderContext (compander.h v1.0.2).
 * Single threshold with separate ratios above and below.
 */

#ifndef COMPANDER_H
#define COMPANDER_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

class Compander : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "Compander"; }
    uint8_t getModuleId() const override { return MODULE_ID_COMPANDER; }

    // ---- Parameter API ----
    /** Set threshold in 0.01dB steps (-9000 = -90.00dB, 0 = 0dB) */
    void setThreshold(int32_t db_001);

    /** Set ratio below threshold (0.01 steps: 100 = 1:1, 200 = 2:1, 50 = 0.5:1) */
    void setRatioBelow(int32_t ratio_001);

    /** Set ratio above threshold (0.01 steps) */
    void setRatioAbove(int32_t ratio_001);

    /** Set attack time in ms */
    void setAttackTime(int32_t ms);

    /** Set release time in ms */
    void setReleaseTime(int32_t ms);

    /** Set pregain in Q4.12 format (4096 = 0dB) */
    void setPregain(int32_t pregain_q412);

private:
    // Parameters (stored)
    int32_t _thresholdDb;    // 0.01dB
    int32_t _ratioBelow;     // 0.01
    int32_t _ratioAbove;     // 0.01
    int32_t _attackMs;
    int32_t _releaseMs;
    int32_t _pregainQ412;

    // Derived coefficients
    q31_t   _threshLin;      // Linear threshold (Q31)
    int32_t _threshDb_q16;   // Pre-computed threshold in dB (Q16.16)
    q31_t   _attackCoeff;
    q31_t   _releaseCoeff;
    q31_t   _pregainLin;     // Linear pregain (Q31)

    // Runtime state
    q31_t   _envelope;       // Smoothed signal level (Q31)

    void recalcCoeffs();
    q31_t computeGain(q31_t envelope);
};

#endif // COMPANDER_H
