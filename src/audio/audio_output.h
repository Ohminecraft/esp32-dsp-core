/**
 * @file audio_output.h
 * @brief Audio output abstraction — I2S (external DAC) and Analog (internal DAC continuous)
 */

#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include "config.h"
#include "dsp_types.h"
#include "pin_config.h"

class AudioOutput {
public:
    /**
     * Initialize audio output hardware.
     * @param sampleRate Sample rate in Hz
     * @param numChannels Number of channels (1 = mono, 2 = stereo)
     */
    void init(int32_t sampleRate, int32_t numChannels);

    /**
     * Write a frame of audio samples to output.
     * @param buffer Stereo interleaved Q31 samples
     * @param numSamples Number of sample pairs to write
     * @return Number of sample pairs actually written
     */
    size_t writeFrame(const float* __restrict buffer, size_t numSamples);

private:
    int32_t      _sampleRate  = DSP_SAMPLE_RATE;
    int32_t      _numChannels = DSP_NUM_CHANNELS;

    i2s_chan_handle_t     _txHandle  = nullptr;   // New I2S driver handle

    void initI2SOutput();

    size_t writeI2S   (const float* __restrict buffer, size_t numSamples);
};

#endif // AUDIO_OUTPUT_H