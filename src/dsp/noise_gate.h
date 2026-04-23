/**
 * @file noise_gate.h
 * @brief Noise Gate — silences signals below threshold
 */

#ifndef NOISE_GATE_H
#define NOISE_GATE_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

class NoiseGate : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "NoiseGate"; }
    uint8_t getModuleId() const override { return MODULE_ID_NOISE_GATE; }

    // ---- Parameter API ----
    void setLowerThreshold(int32_t db_001);
    void setUpperThreshold(int32_t db_001);
    void setAttackTime(int32_t ms);
    void setReleaseTime(int32_t ms);
    void setHoldTime(int32_t ms);

private:
    float _lowerThresh;
    float _upperThresh;
    float _currentGain;
    float _envelope;
    
    float _attackCoeff;
    float _releaseCoeff;
    int32_t _holdSamples;
    int32_t _holdCounter;

    int32_t _lowerThreshDb;
    int32_t _upperThreshDb;
    int32_t _attackMs;
    int32_t _releaseMs;
    int32_t _holdMs;

    void recalcCoeffs();
};

#endif // NOISE_GATE_H
