/**
 * @file dynamic_eq.h
 * @brief Dynamic EQ — Level-dependent dual-EQ system
 *
 * Based on MVSilicon DynamicEqUnit (ctrlvars.h line 969).
 * Contains: DynamicEQContext (RMS detection) + EQ_LOW (EQUnit) + EQ_HIGH (EQUnit)
 *
 * Three energy thresholds define behavior:
 *   - Low level → EQ_LOW applied (e.g., bass boost at quiet volumes)
 *   - Normal level → FLAT (no EQ)
 *   - High level → EQ_HIGH applied (e.g., taming at loud volumes)
 *
 * Smooth interpolation between states using attack/release coefficients.
 */

#ifndef DYNAMIC_EQ_H
#define DYNAMIC_EQ_H

#include "dsp_module.h"
#include "eq.h"
#include "../utils/fixed_math.h"

class DynamicEQ : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "DynamicEQ"; }
    uint8_t getModuleId() const override { return MODULE_ID_DYNAMIC_EQ; }

    // ---- Threshold & Timing API ----
    void setLowEnergyThreshold(int32_t db_001);     // e.g., -4000 = -40.00 dB
    void setNormalEnergyThreshold(int32_t db_001);   // e.g., -2000 = -20.00 dB
    void setHighEnergyThreshold(int32_t db_001);     // e.g., -600  = -6.00 dB
    void setAttackTime(int32_t ms);
    void setReleaseTime(int32_t ms);

    // ---- EQ_LOW / EQ_HIGH parameter access ----
    ParametricEQ& getEqLow()  { return _eqLow; }
    ParametricEQ& getEqHigh() { return _eqHigh; }

    /**
     * Set a band in EQ_LOW.
     */
    void setEqLowBand(uint8_t band, const EQFilterParams& params);

    /**
     * Set a band in EQ_HIGH.
     */
    void setEqHighBand(uint8_t band, const EQFilterParams& params);

    /**
     * Set filter count for EQ_LOW/EQ_HIGH.
     */
    //void setEqLowFilterCount(uint8_t count);
    //void setEqHighFilterCount(uint8_t count);

    /**
     * Get filter count for EQ_LOW/EQ_HIGH.
     */
    //uint8_t getEqLowFilterCount();
    //uint8_t getEqHighFilterCount();

private:
    // Two EQ engines
    ParametricEQ _eqLow;       // Applied at low signal levels
    ParametricEQ _eqHigh;      // Applied at high signal levels

    // RMS detection
    q31_t   _rmsEnergy;        // Running RMS² estimate (Q31)
    q31_t   _rmsCoeff;         // RMS averaging coefficient

    // Thresholds (linear Q31)
    q31_t   _lowThreshLin;
    q31_t   _normalThreshLin;
    q31_t   _highThreshLin;

    // Stored dB values
    int32_t _lowThreshDb;
    int32_t _normalThreshDb;
    int32_t _highThreshDb;

    // Interpolation state
    q31_t   _alphaLow;         // Current blend factor for EQ_LOW (Q31: 0..1)
    q31_t   _alphaHigh;        // Current blend factor for EQ_HIGH (Q31: 0..1)

    // Attack/release coefficients for alpha smoothing
    q31_t   _attackCoeff;
    q31_t   _releaseCoeff;
    int32_t _attackMs;
    int32_t _releaseMs;

    // Note: temp buffer for dual-path processing is stack-allocated in process()
    // to save 4KB of static DRAM.

    void recalcCoeffs();
    q31_t computeTargetAlphaLow(int32_t currentDb_q16);
    q31_t computeTargetAlphaHigh(int32_t currentDb_q16);
};

#endif // DYNAMIC_EQ_H
