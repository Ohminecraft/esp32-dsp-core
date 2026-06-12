/**
 * @file audio_io.cpp
 * @brief Full-duplex audio I/O implementation — see audio_io.h for topology.
 */

#include "audio_io.h"
#include "esp_log.h"

static const char* TAG = "AudioIO";

// ---------------------------------------------------------------------------
// Static buffer definitions
// ---------------------------------------------------------------------------

int32_t AudioIO::s_rxBuf[DSP_FRAME_SAMPLES];
int32_t AudioIO::s_txBuf[DSP_FRAME_SAMPLES];

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

void AudioIO::init(int32_t sampleRate, int32_t numChannels) {
    _sampleRate  = sampleRate;
    _numChannels = numChannels;
    initI2S();
}

void AudioIO::reinit(int32_t sampleRate) {
    if (sampleRate <= 0) return;
    _sampleRate = sampleRate;

    // Slave mode: only update sample_rate_hz for DMA buffer sizing.
    // Clock registers are irrelevant — QCC5125 drives BCLK/LRCK directly.
    i2s_std_clk_config_t clk = {};
    clk.sample_rate_hz = (uint32_t)_sampleRate;
    clk.clk_src        = I2S_CLK_SRC_DEFAULT;  // unused in slave, but required by API
    clk.mclk_multiple  = I2S_MCLK_MULTIPLE_256;

    i2s_channel_disable(_rxHandle);
    i2s_channel_disable(_txHandle);

    i2s_channel_reconfig_std_clock(_rxHandle, &clk);
    i2s_channel_reconfig_std_clock(_txHandle, &clk);

    i2s_channel_enable(_rxHandle);
    i2s_channel_enable(_txHandle);

    LOG_INFO(TAG, "Reinit: %ld Hz (slave, QCC5125 clock)", (long)_sampleRate);
}

void AudioIO::deinit() {
    if (_rxHandle) {
        i2s_channel_disable(_rxHandle);
        i2s_del_channel(_rxHandle);
        _rxHandle = nullptr;
    }
    if (_txHandle) {
        i2s_channel_disable(_txHandle);
        i2s_del_channel(_txHandle);
        _txHandle = nullptr;
    }
}

void AudioIO::initI2S() {
    // ── Channel config ────────────────────────────────────────────────────
    // Single port, slave role, full-duplex.
    // i2s_new_channel with both tx + rx handles = full-duplex pair.
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id            = I2S_INPUT_OUTPUT_FULL_PORT;
    chan_cfg.role          = I2S_ROLE_SLAVE;     // QCC5125 is master
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = DSP_FRAME_SIZE;
    chan_cfg.auto_clear    = true;               // zero-fill TX on underrun → no noise

    // Pass both handles → IDF allocates a full-duplex pair on the same port
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &_txHandle, &_rxHandle));

    // ── Slot config ───────────────────────────────────────────────────────
    // Philips/I2S standard, 32-bit frame, stereo.
    // QCC5125 outputs 24-bit MSB in a 32-bit frame (8 LSBs = 0) — matches.
    // PCM5102A accepts standard Philips I2S — matches.
    i2s_std_config_t std_cfg = {};
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    );

    // ── GPIO config ───────────────────────────────────────────────────────
    // BCLK and WS are inputs (driven by QCC5125).
    // DOUT goes to PCM5102A DIN.
    // DIN comes from QCC5125 DOUT.
    // MCLK: PCM5102A does not need MCLK (uses BCK-derived internal clock).
    //       Set to UNUSED unless your PCM5102A board requires it.
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = (gpio_num_t)I2S_IN_BCK_PIN;      // shared BCLK from QCC5125
    std_cfg.gpio_cfg.ws   = (gpio_num_t)I2S_IN_WS_PIN;       // shared LRCK from QCC5125
    std_cfg.gpio_cfg.din  = (gpio_num_t)I2S_IN_DATA_PIN;     // QCC5125 DOUT → ESP32 DIN
    std_cfg.gpio_cfg.dout = (gpio_num_t)I2S_OUT_DATA_PIN;    // ESP32 DOUT → PCM5102A DIN
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

    // ── Clock config ──────────────────────────────────────────────────────
    // In slave mode the IDF ignores these for actual clock generation,
    // but sample_rate_hz is still used to size the DMA ring buffer.
    std_cfg.clk_cfg.sample_rate_hz = (uint32_t)_sampleRate;
    std_cfg.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_256;

    // ── Init both directions with the same config ─────────────────────────
    // RX and TX share BCLK/WS pins — init_std_mode on each handle sets
    // the direction-specific registers while keeping the shared clock pins.
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_rxHandle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_txHandle, &std_cfg));

    // Enable RX first so DMA starts filling before TX drains
    ESP_ERROR_CHECK(i2s_channel_enable(_rxHandle));
    ESP_ERROR_CHECK(i2s_channel_enable(_txHandle));

    LOG_INFO(TAG, "Init: full-duplex slave on I2S_NUM_0, %ld Hz", (long)_sampleRate);
    LOG_INFO(TAG, "  BCLK=GPIO%d  WS=GPIO%d  DIN=GPIO%d  DOUT=GPIO%d",
             I2S_IN_BCK_PIN, I2S_IN_WS_PIN, I2S_IN_DATA_PIN, I2S_OUT_DATA_PIN);
}

// ---------------------------------------------------------------------------
// Read — hot path
// ---------------------------------------------------------------------------

size_t IRAM_ATTR AudioIO::readFrame(float* __restrict buffer, size_t numSamples) {
    const size_t totalSamples = numSamples * (size_t)_numChannels;
    if (totalSamples > DSP_FRAME_SAMPLES) return 0;

    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(
        _rxHandle, s_rxBuf,
        totalSamples * sizeof(int32_t),
        &bytesRead,
        pdMS_TO_TICKS(20)   // 1.25× frame budget @ 48kHz/768
    );

    if (err == ESP_ERR_TIMEOUT) {
        _readTimeouts++;
        return 0;
    }
    if (err != ESP_OK) return 0;

    const size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead; i++) {
        // QCC5125: 24-bit MSB in 32-bit frame — direct cast, no shift needed.
        buffer[i] = (float)s_rxBuf[i] * (1.0f / 2147483648.0f);
    }

    return bytesRead / (sizeof(int32_t) * (size_t)_numChannels);
}

// ---------------------------------------------------------------------------
// Write — hot path
// ---------------------------------------------------------------------------

size_t IRAM_ATTR AudioIO::writeFrame(const float* __restrict buffer, size_t numSamples) {
    const size_t totalSamples = numSamples * (size_t)_numChannels;
    if (totalSamples > DSP_FRAME_SAMPLES) return 0;

    for (size_t i = 0; i < totalSamples; i++) {
        s_txBuf[i] = audioIO_floatToI32Sat(buffer[i]);
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_channel_write(
        _txHandle, s_txBuf,
        totalSamples * sizeof(int32_t),
        &bytesWritten,
        pdMS_TO_TICKS(20)   // 1.25× frame budget @ 48kHz/768
    );

    if (err == ESP_ERR_TIMEOUT) {
        _writeTimeouts++;
        return 0;
    }
    if (err != ESP_OK) return 0;

    return bytesWritten / (sizeof(int32_t) * (size_t)_numChannels);
}