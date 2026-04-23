/**
 * @file soft_clip.h
 * @brief Soft clipper for speaker protection and warm saturation
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

    const char* getName() const override { return "SoftClip"; }
    uint8_t getModuleId() const override { return MODULE_ID_SOFT_CLIP; }

    // ---- Parameter API ----
    void setThreshold(int32_t db_001); // 0.01dB steps
    void setMode(int32_t mode);

private:
    float _threshold;   // Linear threshold
    int32_t _thresholdDb; // Stored dB value for readback
    int32_t _mode;
};

#endif // SOFT_CLIP_H
