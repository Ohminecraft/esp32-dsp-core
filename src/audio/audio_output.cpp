/**
 * @file audio_output.cpp
 * @brief Audio output implementation — I2S (new driver) and internal DAC (dac_continuous)
 *
 * ESP-IDF >= 5.0 notes:
 *  - I2S_MODE_DAC_BUILT_IN is removed from the new I2S driver.
 *    Internal DAC is now controlled via the dedicated dac_continuous driver.
 *  - Both paths share the same writeFrame() interface; switching is handled
 *    by setSource() with proper deinit/reinit.
 */

#include "audio_output.h"
#include <limits.h>

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

void AudioOutput::init(int32_t sampleRate, int32_t numChannels) {
    _sampleRate  = sampleRate;
    _numChannels = numChannels;

    initI2SOutput();
}

void AudioOutput::initI2SOutput() {
    // --- Channel ---
    i2s_chan_config_t m_tx_chan_cfg = {};
    m_tx_chan_cfg.id            = I2S_OUTPUT_PORT;
    m_tx_chan_cfg.role          = I2S_ROLE_MASTER;
    m_tx_chan_cfg.dma_desc_num  = 8;
    m_tx_chan_cfg.dma_frame_num = DSP_FRAME_SIZE;
    m_tx_chan_cfg.auto_clear    = true;   // Zero-fill DMA on underrun (avoids noise pops)
    m_tx_chan_cfg.intr_priority = 2;

    // TX only (rx = NULL)
    ESP_ERROR_CHECK(i2s_new_channel(&m_tx_chan_cfg, &_txHandle, NULL));

    // --- Standard I2S (Philips) — PCM5102A / similar external DAC ---
    i2s_std_config_t m_tx_std_cfg = {};

    m_tx_std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    );

    m_tx_std_cfg.gpio_cfg.bclk  = (gpio_num_t)I2S_OUT_BCK_PIN;
    m_tx_std_cfg.gpio_cfg.ws    = (gpio_num_t)I2S_OUT_WS_PIN;
    m_tx_std_cfg.gpio_cfg.dout  = (gpio_num_t)I2S_OUT_DATA_PIN;
    // Output port is the MASTER, so it MUST generate MCLK for the PCM1808
    m_tx_std_cfg.gpio_cfg.mclk  = (gpio_num_t)I2S_IN_MCLK_PIN;
    m_tx_std_cfg.gpio_cfg.din   = I2S_GPIO_UNUSED;
    m_tx_std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    m_tx_std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    m_tx_std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

    m_tx_std_cfg.clk_cfg.sample_rate_hz = (uint32_t)_sampleRate;
    m_tx_std_cfg.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
    m_tx_std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_512; // 512x MCLK to ensure accurate sample rates with 24-bit data

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_txHandle, &m_tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(_txHandle));
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

size_t AudioOutput::writeFrame(const float* __restrict buffer, size_t numSamples) {
    return writeI2S(buffer, numSamples);
}

size_t IRAM_ATTR AudioOutput::writeI2S(const float* __restrict buffer, size_t numSamples) {
    const size_t totalSamples = numSamples * _numChannels;
    if (totalSamples > DSP_FRAME_SAMPLES) return 0;

    size_t bytesWritten = 0;

    int32_t buf_to_write[DSP_FRAME_SAMPLES]; // Max frame size in samples (interleaved)
    for (size_t i = 0; i < totalSamples; i++) {
        buf_to_write[i] = floatToI32Sat(buffer[i]);
    }
    for (size_t i = 0; i < numSamples && _numChannels == 2; i++) {
        int32_t tmp = buf_to_write[i * 2];
        buf_to_write[i * 2] = buf_to_write[i * 2 + 1];
        buf_to_write[i * 2 + 1] = tmp;
    }

    esp_err_t err = i2s_channel_write(_txHandle, buf_to_write, totalSamples * sizeof(int32_t),
                                      &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK) return 0;

    return bytesWritten / (sizeof(int32_t) * _numChannels);
}
