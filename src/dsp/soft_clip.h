/**
 * @file soft_clip.h
 * @brief Soft Clipper — output protection module
 *
 * Prevents hard digital clipping by applying gradual saturation.
 * Two modes: cubic polynomial and tanh approximation.
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
    void process(q31_t* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "SoftClipper"; }
    uint8_t getModuleId() const override { return MODULE_ID_SOFT_CLIP; }

    // ---- Parameter API ----
    /** Set threshold in Q31. Default ~75% (0x60000000) */
    void setThreshold(q31_t threshold);

    /** Set mode: 0=cubic, 1=tanh */
    void setMode(int32_t mode);

private:
    q31_t   _threshold;
    int32_t _mode;
};

#endif // SOFT_CLIP_H
