/**
 * @file dynamic_eq.h
 * @brief Dynamic EQ — Level-dependent dual-EQ system (Float32)
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
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "DynamicEQ"; }
    uint8_t getModuleId() const override { return MODULE_ID_DYNAMIC_EQ; }

    // ---- Threshold & Timing API ----
    void setLowEnergyThreshold(int32_t db_001);
    void setNormalEnergyThreshold(int32_t db_001);
    void setHighEnergyThreshold(int32_t db_001);
    void setAttackTime(int32_t ms);
    void setReleaseTime(int32_t ms);

    ParametricEQ& getEqLow()  { return _eqLow; }
    ParametricEQ& getEqHigh() { return _eqHigh; }

    void setEqLowBand(uint8_t band, const EQFilterParams& params);
    void setEqHighBand(uint8_t band, const EQFilterParams& params);

private:
    ParametricEQ _eqLow;
    ParametricEQ _eqHigh;

    float   _rmsEnergySq;
    float   _rmsCoeff;

    int32_t _lowThreshDb;
    int32_t _normalThreshDb;
    int32_t _highThreshDb;

    float   _alphaLow;
    float   _alphaHigh;

    float   _attackCoeff;
    float   _releaseCoeff;
    int32_t _attackMs;
    int32_t _releaseMs;

    void recalcCoeffs();
    float computeTargetAlphaLow(float currentDb);
    float computeTargetAlphaHigh(float currentDb);
};

#endif // DYNAMIC_EQ_H
