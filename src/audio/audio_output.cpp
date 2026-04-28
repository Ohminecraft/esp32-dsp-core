/**
 * @file audio_output.cpp
 * @brief Audio output — I2S TX, always MASTER.
 *
 * Clock topology:
 *   QCC5125 BCK/WS (GPIO4/5) → I2S_NUM_1 RX (slave, input)
 *                             → PCNT (AudioSync rate detection)
 *
 *   ESP32 I2S_NUM_0 TX (master) → BCK/WS (GPIO26/25) → PCM5102A
 *                               → DOUT  (GPIO22)     → PCM5102A
 *
 * AudioSync detects QCC5125's sample rate via PCNT and reinits
 * both I2S ports at the matching rate. Output always generates
 * its own clock — no bus contention with QCC5125.
 */

#include "audio_output.h"
#include "audio_sync.h"
#include <limits.h>
#include "esp_log.h"

static const char* TAG = "AudioOutput";

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

void AudioOutput::init(int32_t sampleRate, int32_t numChannels) {
    _sampleRate  = sampleRate;
    _numChannels = numChannels;
    // Always MASTER — output has its own BCK/WS pins (GPIO25/26) going to PCM5102A.
    // QCC5125 only drives GPIO4/5 (input bus), so output must generate its own clock.
    // AudioSync updates sample rate to match input.
    initI2SOutput();
}

/**
 * @brief Reinitialize output with new sample rate.
 *        Called by AudioSync on clock state change.
 * @param sampleRate   New rate in Hz. Pass 0 to keep last known rate.
 */
void AudioOutput::reinit(int32_t sampleRate) {
    deinit();
    if (sampleRate > 0) _sampleRate = sampleRate;
    // Always MASTER — output BCK/WS (GPIO25/26) are separate from input (GPIO4/5).
    // ESP32 generates clock for PCM5102A at the rate detected by AudioSync.
    initI2SOutput();
    LOG_INFO(TAG, "Reinit: %ld Hz (master)", (long)_sampleRate);
}

void AudioOutput::deinit() {
    if (_txHandle) {
        i2s_channel_disable(_txHandle);
        i2s_del_channel(_txHandle);
        _txHandle = nullptr;
    }
}

void AudioOutput::initI2SOutput() {
    // --- Channel ---
    i2s_chan_config_t m_tx_chan_cfg = {};
    m_tx_chan_cfg.id            = I2S_OUTPUT_PORT;
    m_tx_chan_cfg.role          = I2S_ROLE_MASTER;
    m_tx_chan_cfg.dma_desc_num  = 8;
    m_tx_chan_cfg.dma_frame_num = DSP_FRAME_SIZE;
    m_tx_chan_cfg.auto_clear    = true;   // Zero-fill DMA on underrun
    m_tx_chan_cfg.intr_priority = 2;

    ESP_ERROR_CHECK(i2s_new_channel(&m_tx_chan_cfg, &_txHandle, NULL));

    // --- Standard I2S (Philips) — PCM5102A ---
    i2s_std_config_t m_tx_std_cfg = {};

    m_tx_std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    );

    m_tx_std_cfg.gpio_cfg.bclk  = (gpio_num_t)I2S_OUT_BCK_PIN;   // GPIO4
    m_tx_std_cfg.gpio_cfg.ws    = (gpio_num_t)I2S_OUT_WS_PIN;     // GPIO5
    m_tx_std_cfg.gpio_cfg.dout  = (gpio_num_t)I2S_OUT_DATA_PIN;   // GPIO22
    m_tx_std_cfg.gpio_cfg.mclk  = (gpio_num_t)I2S_IN_MCLK_PIN; // PCM1808
    m_tx_std_cfg.gpio_cfg.din   = I2S_GPIO_UNUSED;
    m_tx_std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    m_tx_std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    m_tx_std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

    m_tx_std_cfg.clk_cfg.sample_rate_hz = (uint32_t)_sampleRate;
    m_tx_std_cfg.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
    // mclk_multiple is ignored in slave mode but set correctly for master fallback
    m_tx_std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_512;

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

    int32_t buf_to_write[DSP_FRAME_SAMPLES];
    for (size_t i = 0; i < totalSamples; i++) {
        buf_to_write[i] = floatToI32Sat(buffer[i]);
    }
    // Swap L/R channels
    for (size_t i = 0; i < numSamples && _numChannels == 2; i++) {
        int32_t tmp              = buf_to_write[i * 2];
        buf_to_write[i * 2]     = buf_to_write[i * 2 + 1];
        buf_to_write[i * 2 + 1] = tmp;
    }

    esp_err_t err = i2s_channel_write(_txHandle, buf_to_write,
                                      totalSamples * sizeof(int32_t),
                                      &bytesWritten, pdMS_TO_TICKS(20));
    if (err != ESP_OK) return 0;

    return bytesWritten / (sizeof(int32_t) * _numChannels);
}