#include "auto_eq.h"
#include "dsp_pipeline.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Static tables
// ─────────────────────────────────────────────────────────────────────────────

const uint16_t AutoEQ::DEFAULT_FREQ_HZ[AUTO_EQ_NUM_BANDS] = {
    63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

const float AutoEQ::DEFAULT_TARGET_DB[AUTO_EQ_NUM_BANDS] = {
    3.0f, 2.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, -2.0f, -3.0f
};

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);

    // Hann window: w[i] = 0.5 - 0.5*cos(2π*i/(N-1))
    for (int i = 0; i < AUTO_EQ_FFT_SIZE; i++) {
        _window[i] = 0.5f - 0.5f * cosf(
            (float)(2.0 * M_PI) * (float)i / (float)(AUTO_EQ_FFT_SIZE - 1)
        );
    }

    dsps_fft2r_init_fc32(NULL, AUTO_EQ_FFT_SIZE);

    // Target curve và center freq
    memcpy(_targetDb, DEFAULT_TARGET_DB, sizeof(_targetDb));
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        _centerFreq[i] = (float)DEFAULT_FREQ_HZ[i];
    }

    // Reset tất cả state
    memset(_measuredDb,    0, sizeof(_measuredDb));
    memset(_correctionDb,  0, sizeof(_correctionDb));
    memset(_ring,          0, sizeof(_ring));
    memset(_fft,           0, sizeof(_fft));

    // Đặt lastAppliedDb sao cho lần đầu sẽ luôn trigger prepareFilters
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        _lastAppliedDb[i] = _correctionDb[i] - (UPDATE_THRESHOLD_DB + 1.0f);
    }

    _ringPos      = 0;
    _frameReady   = false;
    _pendingUpdate = false;

    reset();
    prepareFiltersIfNeeded();
    commitPendingFilters();
}

// ─────────────────────────────────────────────────────────────────────────────
// process  (audio hot path — Core 0)
// ─────────────────────────────────────────────────────────────────────────────

void IRAM_ATTR AutoEQ::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled || !samples || numSamples == 0) return;

    // 1. Tích lũy samples cho FFT analyzer (non-blocking)
    pushAnalysisFrame(samples, numSamples);

    // 2. Nếu đủ 1024 sample: chạy analysis + update controller + chuẩn bị filter mới
    //    Toàn bộ khối này chạy trong audio callback — chấp nhận được vì:
    //    FFT 1024 trên S3 ~0.5ms, tần suất mỗi ~21ms → duty cycle ~2.5%
    //    Nếu muốn tách Core 1, chỉ cần move khối này sang task riêng.
    if (_frameReady) {
        runAnalyzer();       // FFT → _measuredDb[] (normalized)
        updateController();  // error + smooth → _correctionDb[]
        prepareFiltersIfNeeded(); // redesign pending filters nếu cần
    }

    // 3. Swap pending filter vào active (đầu mỗi block)
    commitPendingFilters();

    // 4. Apply Biquad filter bank lên audio
    if (_scratchpad) {
        float* bufL = _scratchpad->buf5;
        float* bufR = _scratchpad->buf6;

        // Deinterleave
        for (size_t i = 0; i < numSamples; i++) {
            bufL[i] = samples[i * 2];
            bufR[i] = samples[i * 2 + 1];
        }

        // Apply từng band nối tiếp
        for (int b = 0; b < AUTO_EQ_NUM_BANDS; b++) {
            _filters[b].processPlanar(bufL, bufR, numSamples);
        }

        // Reinterleave + NaN guard + clamp
        for (size_t i = 0; i < numSamples; i++) {
            float l = bufL[i];
            float r = bufR[i];

            if (!isfinite(l) || !isfinite(r)) {
                // Blow-up detected: reset filter state, mute sample
                reset();
                l = 0.0f;
                r = 0.0f;
            }

            samples[i * 2]     = clampf(l, -1.0f, 1.0f);
            samples[i * 2 + 1] = clampf(r, -1.0f, 1.0f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::reset() {
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        _filters[i].reset();
        _pendingFilters[i].reset();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Setters
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::setTargetDb(const float* db, int count) {
    if (!db || count <= 0) return;
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    for (int i = 0; i < n; i++) {
        _targetDb[i] = clampf(db[i], -24.0f, 24.0f);
    }
}

void AutoEQ::setTargetQ88(const int16_t* q88, int count) {
    if (!q88 || count <= 0) return;
    float target[AUTO_EQ_NUM_BANDS];
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    for (int i = 0; i < n; i++) {
        target[i] = (float)q88[i] / 256.0f;
    }
    setTargetDb(target, n);
}

void AutoEQ::setBandConfig(const uint16_t* freqHz, const int16_t* targetQ88, int count) {
    if (!freqHz || !targetQ88 || count <= 0) return;
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    const float nyquistSafe = ((float)_sampleRate * 0.5f) - 100.0f;
    for (int i = 0; i < n; i++) {
        _centerFreq[i] = clampf((float)freqHz[i], 20.0f, nyquistSafe);
        _targetDb[i]   = clampf((float)targetQ88[i] / 256.0f, -24.0f, 24.0f);
    }
    // Force redesign ngay lần tiếp theo
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        _lastAppliedDb[i] = _correctionDb[i] - (UPDATE_THRESHOLD_DB + 1.0f);
    }
    prepareFiltersIfNeeded();
}

// ─────────────────────────────────────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::getFreqHz(uint16_t* out, int count) const {
    if (!out || count <= 0) return;
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    for (int i = 0; i < n; i++) out[i] = (uint16_t)lrintf(_centerFreq[i]);
}

void AutoEQ::getTargetQ88(int16_t* out, int count) const {
    if (!out || count <= 0) return;
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    for (int i = 0; i < n; i++) out[i] = dbToQ88(_targetDb[i]);
}

void AutoEQ::getCorrectionQ88(int16_t* out, int count) const {
    if (!out || count <= 0) return;
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    for (int i = 0; i < n; i++) out[i] = dbToQ88(_correctionDb[i]);
}

void AutoEQ::getMeasuredQ88(int16_t* out, int count) const {
    if (!out || count <= 0) return;
    const int n = count < AUTO_EQ_NUM_BANDS ? count : AUTO_EQ_NUM_BANDS;
    for (int i = 0; i < n; i++) out[i] = dbToQ88(_measuredDb[i]);
}

// ─────────────────────────────────────────────────────────────────────────────
// pushAnalysisFrame  (IRAM — hot path)
//
// Fix so với bản cũ:
//   - Không dừng toàn bộ loop khi _frameReady = true
//   - Khi ring đầy: set flag rồi BREAK ngay, giữ lại samples còn lại
//     cho block tiếp theo (không drop)
// ─────────────────────────────────────────────────────────────────────────────

void IRAM_ATTR AutoEQ::pushAnalysisFrame(const float* samples, size_t numFrames) {
    if (_frameReady) return; // frame trước chưa xử lý xong, không đè

    for (size_t i = 0; i < numFrames; i++) {
        // Downmix stereo → mono
        float mono = samples[i * _numChannels];
        if (_numChannels > 1) {
            mono = 0.5f * (mono + samples[i * _numChannels + 1]);
        }

        _ring[_ringPos++] = mono;

        if (_ringPos >= AUTO_EQ_FFT_SIZE) {
            _ringPos   = 0;
            _frameReady = true;
            break; // dừng: frame đã đầy, xử lý ở process()
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runAnalyzer
//
// Fix so với bản cũ:
//   - Normalize _measuredDb[] về relative (trừ đi max band)
//     → cùng reference scale với _targetDb[]
//   - Silence gate: không update nếu tín hiệu quá yếu
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::runAnalyzer() {
    // Windowed copy vào FFT buffer (complex interleaved)
    for (int i = 0; i < AUTO_EQ_FFT_SIZE; i++) {
        _fft[i * 2]     = _ring[i] * _window[i];
        _fft[i * 2 + 1] = 0.0f;
    }

    dsps_fft2r_fc32(_fft, AUTO_EQ_FFT_SIZE);
    dsps_bit_rev_fc32(_fft, AUTO_EQ_FFT_SIZE);

    // Tính magnitude (dB) per band
    float rawDb[AUTO_EQ_NUM_BANDS];
    float maxDb = -200.0f;

    for (int b = 0; b < AUTO_EQ_NUM_BANDS; b++) {
        const float center = clampf(_centerFreq[b], 20.0f, (float)_sampleRate * 0.5f - 1.0f);

        // Bin range: geometric mean giữa band kề nhau (log scale)
        const float lower = (b == 0)
            ? 20.0f
            : sqrtf(_centerFreq[b - 1] * center);
        const float upper = (b == AUTO_EQ_NUM_BANDS - 1)
            ? ((float)_sampleRate * 0.5f)
            : sqrtf(center * _centerFreq[b + 1]);

        int binStart = (int)floorf(lower * (float)AUTO_EQ_FFT_SIZE / (float)_sampleRate);
        int binEnd   = (int)ceilf (upper * (float)AUTO_EQ_FFT_SIZE / (float)_sampleRate);

        // Guard
        if (binStart < 1)                    binStart = 1;
        if (binEnd   > AUTO_EQ_FFT_SIZE / 2) binEnd   = AUTO_EQ_FFT_SIZE / 2;
        if (binEnd   <= binStart)             binEnd   = binStart + 1;

        // Average energy (dB) trong band — dùng double tránh accumulation error
        double energy = 0.0;
        for (int bin = binStart; bin < binEnd; bin++) {
            const float re = _fft[bin * 2];
            const float im = _fft[bin * 2 + 1];
            energy += (double)re * re + (double)im * im;
        }
        const int binCount = binEnd - binStart;
        rawDb[b] = 10.0f * log10f((float)(energy / (double)binCount) + 1.0e-12f);

        if (rawDb[b] > maxDb) maxDb = rawDb[b];
    }

    // Silence gate: nếu band mạnh nhất vẫn quá yếu, bỏ frame này
    // Tránh auto-EQ boost noise khi không có audio
    if (maxDb < SILENCE_GATE_DB) {
        _frameReady = false;
        return;
    }

    // Normalize về relative: _measuredDb[b] là độ lệch so với band mạnh nhất
    // Giờ cùng reference scale với _targetDb[] (đều là dB relative)
    for (int b = 0; b < AUTO_EQ_NUM_BANDS; b++) {
        _measuredDb[b] = rawDb[b] - maxDb;
    }

    _frameReady = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// updateController
//
// Tính error per band và smooth với attack/release asymmetric
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::updateController() {
    for (int b = 0; b < AUTO_EQ_NUM_BANDS; b++) {
        // error = bao nhiêu dB cần bù để khớp target
        float error = _targetDb[b] - _measuredDb[b];
        error = clampf(error, -MAX_CORRECTION_DB, MAX_CORRECTION_DB);

        // Attack nhanh (giảm gain) — Release chậm (tăng gain)
        // Tránh pumping artifact khi transient
        const float alpha = (error < _correctionDb[b]) ? ATTACK_ALPHA : RELEASE_ALPHA;
        _correctionDb[b] += alpha * (error - _correctionDb[b]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// prepareFiltersIfNeeded
//
// Redesign toàn bộ filter bank nếu bất kỳ band nào thay đổi > threshold
// Kết quả lưu vào _pendingFilters[], commit sau bởi commitPendingFilters()
// ─────────────────────────────────────────────────────────────────────────────

void AutoEQ::prepareFiltersIfNeeded() {
    // Kiểm tra xem có band nào thay đổi đủ để redesign không
    bool changed = false;
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        if (fabsf(_correctionDb[i] - _lastAppliedDb[i]) > UPDATE_THRESHOLD_DB) {
            changed = true;
            break;
        }
    }
    if (!changed) return;

    // Tính coefficient mới trên stack (không alloc heap trong audio path)
    Biquad next[AUTO_EQ_NUM_BANDS];
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        // Band đầu: Low Shelf (bass nhẹ nhàng hơn Peaking)
        // Band cuối: High Shelf
        // Các band giữa: Peaking EQ
        EQFilterType type;
        if      (i == 0)                   type = EQ_FILTER_TYPE_LOW_SHELF;
        else if (i == AUTO_EQ_NUM_BANDS-1) type = EQ_FILTER_TYPE_HIGH_SHELF;
        else                               type = EQ_FILTER_TYPE_PEAKING;

        next[i].design(type, _centerFreq[i], DEFAULT_Q, _correctionDb[i], (float)_sampleRate);
    }

    // Atomic write vào pending buffer
    portENTER_CRITICAL(&_pendingMux);
    for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
        _pendingFilters[i]  = next[i];
        _lastAppliedDb[i]   = _correctionDb[i];
    }
    _pendingUpdate = true;
    portEXIT_CRITICAL(&_pendingMux);
}

// ─────────────────────────────────────────────────────────────────────────────
// commitPendingFilters  (IRAM — gọi đầu mỗi audio block)
//
// Swap _pendingFilters → _filters nếu có update mới
// Double-check flag bên trong critical section để tránh race
// ─────────────────────────────────────────────────────────────────────────────

void IRAM_ATTR AutoEQ::commitPendingFilters() {
    if (!_pendingUpdate) return;

    portENTER_CRITICAL(&_pendingMux);
    if (_pendingUpdate) {
        for (int i = 0; i < AUTO_EQ_NUM_BANDS; i++) {
            _filters[i] = _pendingFilters[i];
        }
        _pendingUpdate = false;
    }
    portEXIT_CRITICAL(&_pendingMux);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────────────────────────

inline float AutoEQ::clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline int16_t AutoEQ::dbToQ88(float db) {
    const float q = db * 256.0f;
    if (q >  32767.0f) return  32767;
    if (q < -32768.0f) return -32768;
    return (int16_t)lrintf(q);
}