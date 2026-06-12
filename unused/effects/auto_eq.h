#ifndef AUTO_EQ_H
#define AUTO_EQ_H

#include "dsp_module.h"
#include "biquad.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <esp_dsp.h>
#include <string.h>

#define AUTO_EQ_FFT_SIZE  1024
#define AUTO_EQ_NUM_BANDS 9

class AutoEQ : public DspModule {
    friend class PresetManager;
    friend class ParamController;

public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName()    const override { return "AutoEQ"; }
    uint8_t     getModuleId() const override { return MODULE_ID_AUTO_EQ; }

    // Setter / getter — đơn vị dB (float)
    void setTargetDb(const float*    db,    int count);
    void setTargetQ88(const int16_t* q88,   int count);
    void setBandConfig(const uint16_t* freqHz, const int16_t* targetQ88, int count);

    void getFreqHz(uint16_t*       out, int count) const;
    void getTargetQ88(int16_t*     out, int count) const;
    void getCorrectionQ88(int16_t* out, int count) const;
    void getMeasuredQ88(int16_t*   out, int count) const;

private:
    // ── Tuning constants ──────────────────────────────────────────────────────
    static constexpr float ATTACK_ALPHA        = 0.05f;  // nhanh khi cần giảm
    static constexpr float RELEASE_ALPHA       = 0.01f;  // chậm khi tăng (tránh pumping)
    static constexpr float MAX_CORRECTION_DB   = 12.0f;  // clamp correction +/-12 dB
    static constexpr float UPDATE_THRESHOLD_DB = 0.1f;   // chỉ redesign filter khi thay đổi > 0.1 dB
    static constexpr float DEFAULT_Q           = 1.41421356f; // sqrt(2), Butterworth
    // Silence gate: bỏ qua frame nếu năng lượng quá thấp (tránh boost noise)
    static constexpr float SILENCE_GATE_DB     = -60.0f;

    // ── Default tables ────────────────────────────────────────────────────────
    static const uint16_t DEFAULT_FREQ_HZ[AUTO_EQ_NUM_BANDS];
    static const float    DEFAULT_TARGET_DB[AUTO_EQ_NUM_BANDS];

    // ── FFT / analysis ────────────────────────────────────────────────────────
    float _window[AUTO_EQ_FFT_SIZE];        // Hann window, tính sẵn lúc init
    float _fft[AUTO_EQ_FFT_SIZE * 2];       // complex interleaved (re,im,re,im,...)
    float _ring[AUTO_EQ_FFT_SIZE];          // ring accumulator cho FFT
    int   _ringPos   = 0;
    bool  _frameReady = false;

    // ── EQ state ──────────────────────────────────────────────────────────────
    float _targetDb[AUTO_EQ_NUM_BANDS];     // target curve (user-defined)
    float _centerFreq[AUTO_EQ_NUM_BANDS];   // center freq mỗi band (Hz)
    float _measuredDb[AUTO_EQ_NUM_BANDS];   // relative magnitude đo được (sau normalize)
    float _correctionDb[AUTO_EQ_NUM_BANDS]; // correction hiện tại (sau smooth)
    float _lastAppliedDb[AUTO_EQ_NUM_BANDS];// correction đã apply vào filter lần cuối

    // ── Filter bank (double buffer) ───────────────────────────────────────────
    Biquad           _filters[AUTO_EQ_NUM_BANDS];        // đang dùng (audio path)
    Biquad           _pendingFilters[AUTO_EQ_NUM_BANDS]; // chuẩn bị sẵn (background)
    volatile bool    _pendingUpdate = false;
    portMUX_TYPE     _pendingMux    = portMUX_INITIALIZER_UNLOCKED;

    // ── Private methods ───────────────────────────────────────────────────────

    // Tích lũy samples vào ring buffer; KHÔNG block khi frameReady
    void IRAM_ATTR pushAnalysisFrame(const float* samples, size_t numFrames);

    // FFT + tính magnitude dB per band (relative, normalized)
    void runAnalyzer();

    // Tính error và smooth correction per band
    void updateController();

    // Redesign pending filter bank nếu correction thay đổi đủ lớn
    void prepareFiltersIfNeeded();

    // Swap pending → active (gọi đầu mỗi audio block)
    void IRAM_ATTR commitPendingFilters();

    // Utilities
    static inline float   clampf(float v, float lo, float hi);
    static inline int16_t dbToQ88(float db);
};

#endif // AUTO_EQ_H