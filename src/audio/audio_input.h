/**
 * @file audio_input.h
 * @brief Audio input abstraction — I2S (new driver) and ADC sources
 */

#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include "config.h"
#include "dsp_types.h"
#include "pin_config.h"

class AudioInput {
public:
    /**
     * Initialize audio input hardware.
     * @param sampleRate Sample rate in Hz
     * @param numChannels Number of channels (1 = mono, 2 = stereo)
     */
    void init(int32_t sampleRate, int32_t numChannels);

    /**
     * Read a frame of audio samples.
     * @param buffer Output buffer (stereo interleaved float)
     * @param numSamples Number of sample pairs to read
     * @return Number of sample pairs actually read
     */
    size_t readFrame(float* __restrict buffer, size_t numSamples);

private:
    int32_t     _sampleRate  = DSP_SAMPLE_RATE;
    int32_t     _numChannels = DSP_NUM_CHANNELS;

    i2s_chan_handle_t _rxHandle = nullptr;  // New driver channel handle

    void   initI2SInput();

    size_t readI2S(float* __restrict buffer, size_t numSamples);
};

#endif // AUDIO_INPUT_H