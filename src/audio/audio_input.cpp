/**
 * @file audio_input.cpp
 * @brief Audio input — I2S RX, SLAVE under QCC5125 clock.
 *
 * QCC5125 drives BCK (GPIO4) and WS (GPIO5).
 * ESP32 I2S_NUM_1 samples DIN (GPIO35) on QCC5125's clock edges.
 *
 * reinit() is called by AudioSync when sample rate changes.
 * Both input and output reinit together to stay in sync.
 */

#include "audio_input.h"
#include "esp_log.h"

static const char* TAG = "AudioInput";

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

void AudioInput::init(int32_t sampleRate, int32_t numChannels) {
    _sampleRate  = sampleRate;
    _numChannels = numChannels;
    initI2SInput();
}

void AudioInput::reinit(int32_t sampleRate) {
    deinit();
    if (sampleRate > 0) _sampleRate = sampleRate;
    initI2SInput();
    LOG_INFO(TAG, "Reinit: %ld Hz", (long)_sampleRate);
}

void AudioInput::deinit() {
    if (_rxHandle) {
        i2s_channel_disable(_rxHandle);
        i2s_del_channel(_rxHandle);
        _rxHandle = nullptr;
    }
}

void AudioInput::initI2SInput() {
    // --- Channel config ---
    // SLAVE: QCC5125 is I2S master, drives BCK + WS.
    i2s_chan_config_t m_rx_chan_cfg = {};
    m_rx_chan_cfg.id                   = I2S_INPUT_PORT;
    m_rx_chan_cfg.role                 = I2S_ROLE_SLAVE;
    m_rx_chan_cfg.dma_desc_num         = 8;
    m_rx_chan_cfg.dma_frame_num        = DSP_FRAME_SIZE;
    m_rx_chan_cfg.auto_clear_after_cb  = true;
    m_rx_chan_cfg.auto_clear_before_cb = false;

    ESP_ERROR_CHECK(i2s_new_channel(&m_rx_chan_cfg, NULL, &_rxHandle));

    // --- Standard I2S (Philips) ---
    // QCC5125: 24-bit left-justified in 32-bit frame (8 LSBs = 0)
    i2s_std_config_t m_rx_std_cfg = {};

    m_rx_std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    );

    // QCC5125 self-clocked → MCLK not needed from ESP32
    m_rx_std_cfg.gpio_cfg.mclk  = I2S_GPIO_UNUSED;
    m_rx_std_cfg.gpio_cfg.bclk  = (gpio_num_t)I2S_IN_BCK_PIN;   // GPIO4
    m_rx_std_cfg.gpio_cfg.ws    = (gpio_num_t)I2S_IN_WS_PIN;    // GPIO5
    m_rx_std_cfg.gpio_cfg.din   = (gpio_num_t)I2S_IN_DATA_PIN;  // GPIO35
    m_rx_std_cfg.gpio_cfg.dout  = I2S_GPIO_UNUSED;
    m_rx_std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    m_rx_std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    m_rx_std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

    // In slave mode, sample_rate_hz is used only for DMA buffer sizing.
    // Set to current known rate (max 96kHz) — actual clock comes from QCC5125.
    m_rx_std_cfg.clk_cfg.sample_rate_hz = (uint32_t)_sampleRate;
    m_rx_std_cfg.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
    m_rx_std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_512;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_rxHandle, &m_rx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(_rxHandle));
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

size_t AudioInput::readFrame(float* __restrict buffer, size_t numSamples) {
    return readI2S(buffer, numSamples);
}

size_t IRAM_ATTR AudioInput::readI2S(float* __restrict buffer, size_t numSamples) {
    const size_t totalSamples = numSamples * _numChannels;
    if (totalSamples > DSP_FRAME_SAMPLES) return 0;

    size_t bytesRead = 0;

    int32_t buf_to_read[DSP_FRAME_SAMPLES];
    // Use finite timeout so audioTask doesn't block forever when BCK disappears.
    // At 44.1kHz, one frame (256 samples) takes ~5.8ms — 20ms gives plenty of margin.
    // On timeout, returns 0 → audioTask loops back and checks g_pipelineReady.
    esp_err_t err = i2s_channel_read(_rxHandle, buf_to_read,
                                     totalSamples * sizeof(int32_t),
                                     &bytesRead, pdMS_TO_TICKS(20));
    if (err != ESP_OK) return 0;

    const size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead; i++) {
        // QCC5125: 24-bit MSB in 32-bit frame — same as PCM1808.
        // Direct cast is correct, no bit shift needed.
        buffer[i] = (float)buf_to_read[i] / 2147483648.0f;
    }

    return bytesRead / (sizeof(int32_t) * _numChannels);
}