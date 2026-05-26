/**
 * @file leftrighteq.h
 * @brief Contain LeftRightEQ Function 
*/

#ifndef LEFTRIGHT_EQ_H
#define LEFTRIGHT_EQ_H

#include "dsp_module.h"
#include "eq.h"
#include "config.h"


class LeftRightEQ : public DspModule {
    friend class PresetManager;
    friend class PresetController;

public:
    LeftRightEQ() = default;

    // DspModule Interface
    void init(int32_t sampleRate, int32_t numChannels) override;

    void process(float* __restrict samples, size_t numSamples) override;

    void reset() override;

    const char* getName()     const override { return "LeftRightEQ"; };
    uint8_t     getModuleId() const override { return MODULE_ID_LEFTRIGHT_EQ; }


    ParametricEQ& getEqLeft()  { return _eqLeft;  };
    ParametricEQ& getEqRight() { return _eqRight; };

    void setEqLeft (uint8_t band, const EQFilterParams& params);
    void setEqRight(uint8_t band, const EQFilterParams& params);

private:

    ParametricEQ _eqLeft;
    ParametricEQ _eqRight;
};

#endif