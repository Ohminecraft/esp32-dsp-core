/**
 * @file eq.h
 * @brief 10-band Parametric EQ module — used by EQ1, EQ2, and DynamicEQ
 */

#ifndef EQ_H
#define EQ_H

#include "../utils/fixed_math.h"
#include "dsp_module.h"
#include "biquad.h"

class ParametricEQ : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    ParametricEQ() : _pregain(1.0f) {}

    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "ParametricEQ"; }
    uint8_t getModuleId() const override { return _moduleId; }

    void setModuleId(uint8_t id) { _moduleId = id; }

    // ---- Parameter API ----
    void setBand(uint8_t bandIndex, const EQFilterParams& params);
    void setPregain(int16_t pregain_db);
    int16_t getPregain() const { return _pregainDb; };

    const EQFilterParams& getBandParams(uint8_t bandIndex) const {
        return _params[bandIndex];
    }

    void processInternal(float* __restrict samples, size_t numSamples);

private:
    uint8_t        _moduleId = MODULE_ID_EQ_DSP_1; // Default ID, can be changed for EQ2
    float          _pregain;        // Linear gain
    int16_t        _pregainDb = 0;  // Original dB value in Q8.8

    Biquad         _filters[MAX_EQ_BANDS];      // Biquad sections
    EQFilterParams _params[MAX_EQ_BANDS];        // Stored parameters
};

#endif // EQ_H
