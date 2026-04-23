/**
 * @file drc.h
 * @brief Dynamic Range Compressor (DRC)
 */

#ifndef DRC_H
#define DRC_H

#include "dsp_module.h"
#include "../utils/fixed_math.h"

struct DRCBand {
    float thresholdDb;
    float ratio;
    float attackCoeff;
    float releaseCoeff;
    float pregain;
    float envelope;
    
    int32_t thresholdDbInt;
    int32_t ratioQ88;
    int32_t attackMs;
    int32_t releaseMs;
    int32_t pregainQ412;
};

class DRC : public DspModule {
    friend class PresetManager;
    friend class ParamController;
public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "DRC"; }
    uint8_t getModuleId() const override { return MODULE_ID_DRC; }

    // ---- Parameter API ----
    void setThreshold(uint8_t band, int32_t db_001);
    void setRatio(uint8_t band, int32_t ratio_q88);
    void setAttackTime(uint8_t band, int32_t ms);
    void setReleaseTime(uint8_t band, int32_t ms);
    void setPregain(uint8_t band, int32_t gain_q412);
    void setMode(DRCMode mode);

private:
    static const int MAX_DRC_BANDS = 4;
    DRCBand _bands[MAX_DRC_BANDS];
    DRCMode _mode;

    void recalcCoeffs(uint8_t band);
};

#endif // DRC_H
