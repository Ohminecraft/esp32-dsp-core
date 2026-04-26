/**
 * @file soft_clip.h
 * @brief Soft clipper — tanh knee, linear passthrough below threshold
 *
 * UNIT: setThreshold nhận db_001 (0.01dB steps)
 *   -40.00 dB → -4000,  -6.00 dB → -600,  0 dB → 0
 */

#ifndef SOFT_CLIP_H
#define SOFT_CLIP_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

class SoftClipper : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName()     const override { return "SoftClip"; }
    uint8_t     getModuleId() const override { return MODULE_ID_SOFT_CLIP; }

    void    setThreshold(int32_t db_001);
    int32_t getThresholdDb() const { return _thresholdDb; }
    float   getThreshold()   const { return _threshold;   }

private:
    float   _threshold    = 1.0f;
    float   _headroom     = 0.001f;
    float   _invHeadroom  = 1000.0f;
    int32_t _thresholdDb  = 0;
};

#endif // SOFT_CLIP_H