# Adding New DSP Effects - ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Adding New DSP Modules (English)

This guide explains how to create and integrate a new DSP module into the pipeline.

---

### Architecture Overview

Each DSP module:
- Inherits from `DspModule` base class
- Implements `process()` method to operate on audio buffers
- Declares `friend class PresetManager` and `friend class ParamController` for parameter access
- Provides setter methods like `setParam()` for parameter updates
- Manages state in private members (fixed-point Q8.8 format for dB values)
- Operates on float32 audio (96kHz, interleaved stereo)

---

### Step 1: Create Module Header File

Create `src/dsp/your_effect.h`:

```cpp
#ifndef YOUR_EFFECT_H
#define YOUR_EFFECT_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"

// Forward declarations for friend access
class PresetManager;
class ParamController;

class YourEffect : public DspModule {
    friend class PresetManager;      // Allow preset save/load
    friend class ParamController;    // Allow UART parameter control

public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "YourEffect"; }
    uint8_t getModuleId() const override { return MODULE_ID_YOUR_EFFECT; }

    // ---- Parameter Setters (called by ParamController) ----
    void setFrequency(int32_t freq);      // Frequency in Hz (as int32)
    void setGain(int32_t gain_q88);       // Gain in Q8.8 format (dB)
    void setQFactor(int32_t q_q610);      // Q in Q6.10 format

private:
    // ---- State (PresetManager accesses these via friend) ----
    Biquad _filter_left;
    Biquad _filter_right;
    
    int32_t _frequency;                   // Hz
    int32_t _gain_q88;                    // Q8.8 format dB
    int32_t _q_factor_q610;               // Q6.10 format
    
    float _gainLinear;                    // Cached linear gain
    int32_t _lastFreq;                    // For optimization

    void recalculateCoefficients();
};

#endif // YOUR_EFFECT_H
```

---

### Step 2: Implement Module

Create `src/dsp/your_effect.cpp`:

```cpp
#include "your_effect.h"
#include "config.h"

void YourEffect::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    
    // Initialize parameters to defaults
    _frequency = 1000;              // 1kHz
    _gain_q88 = 0;                  // 0dB (256 in Q8.8)
    _q_factor_q610 = 512;           // 0.5 Q (512 in Q6.10)
    _lastFreq = -1;                 // Force recalc on first process
    
    recalculateCoefficients();
    reset();
}

void IRAM_ATTR YourEffect::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;
    
    // Process stereo interleaved
    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            
            // Apply filter
            float filtered = _filter_left.processSample(samples[idx], ch);
            
            // Apply gain
            samples[idx] = filtered * _gainLinear;
        }
    }
}

void YourEffect::reset() {
    _filter_left.reset();
    _filter_right.reset();
}

void YourEffect::recalculateCoefficients() {
    // Skip if frequency unchanged (optimization)
    if (_frequency == _lastFreq) return;
    
    _lastFreq = _frequency;
    
    // Convert Q format parameters to float for biquad design
    float q_float = (float)_q_factor_q610 / 1024.0f;  // Q6.10 → float
    
    // Design filter (example: low-pass)
    _filter_left.design(
        EQ_FILTER_TYPE_LOW_PASS,        // Filter type
        (float)_frequency,               // Frequency Hz
        q_float,                         // Q factor
        0,                               // Gain (for shelf filters)
        (float)_sampleRate               // Sample rate
    );
    _filter_right = _filter_left;       // Mirror right channel
}

// ---- Parameter Setters (called by ParamController via UART) ----

void YourEffect::setFrequency(int32_t freq) {
    // Validate range
    _frequency = (freq < 20) ? 20 : (freq > 20000) ? 20000 : freq;
    recalculateCoefficients();
}

void YourEffect::setGain(int32_t gain_q88) {
    // Validate range: -60dB to +18dB = -15360 to +4608 in Q8.8
    _gain_q88 = (gain_q88 < -15360) ? -15360 : (gain_q88 > 4608) ? 4608 : gain_q88;
    
    // Convert Q8.8 dB to linear gain
    _gainLinear = db_q88_to_linear_gain((int16_t)_gain_q88);
}

void YourEffect::setQFactor(int32_t q_q610) {
    // Validate range: 0.1 to 10.0 = 102 to 10240 in Q6.10
    _q_factor_q610 = (q_q610 < 102) ? 102 : (q_q610 > 10240) ? 10240 : q_q610;
    recalculateCoefficients();
}
```

---

### Step 3: Update Module ID in Config

Edit `include/config.h`:

```cpp
// Add module ID (find next available ID)
#define MODULE_ID_YOUR_EFFECT    0x0D    // Next available ID

// Add to module count (if extending beyond 12)
#define DSP_MODULE_COUNT         13
```

---

### Step 4: Register Module in Pipeline

The pipeline uses a `_chain[]` array that is built during initialization. Modules are processed by iterating through this chain.

Edit `src/dsp/dsp_pipeline.h`:

```cpp
#include "your_effect.h"

class DspPipeline {
private:
    static const size_t CHAIN_LENGTH = 13;  // Increase if adding module
    
    // ... existing modules ...
    YourEffect _yourEffect;                 // Add your module instance
    
    DspModule* _chain[CHAIN_LENGTH];        // Chain array

public:
    void init(int32_t sampleRate, int32_t numChannels);
    void processFrame(float* __restrict samples, size_t numSamples);
    
    // ... accessors ...
    YourEffect& getYourEffect() { return _yourEffect; }
};
```

Edit `src/dsp/dsp_pipeline.cpp` - in the `init()` method:

```cpp
void DspPipeline::init(int32_t sampleRate, int32_t numChannels) {
    // Build chain in processing order
    _chain[0]  = &_noiseGate;
    _chain[1]  = &_compander;
    _chain[2]  = &_exciter;
    _chain[3]  = &_virtualBass;
    _chain[4]  = &_bassClassic;
    _chain[5]  = &_stereoWidener;
    _chain[6]  = &_dynamicEq;
    _chain[7]  = &_eqDsp_1;
    _chain[8]  = &_eqDsp_2;
    _chain[9]  = &_drc;
    _chain[10] = &_volume;
    _chain[11] = &_softClipper;
    _chain[12] = &_yourEffect;      // Add at desired position in chain
    
    // Set module ID
    _yourEffect.setModuleId(MODULE_ID_YOUR_EFFECT);
    
    // Initialize all modules
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        _chain[i]->init(sampleRate, numChannels);
    }
    
    // Enable/disable modules as needed
    _volume.enable();
    _yourEffect.enable();              // Default enabled
}
```

**⚠️ Important**: 
- `processFrame()` automatically iterates through `_chain[]`
- **DO NOT** manually add calls to `process()` in `processFrame()`
- Module order in `_chain[]` determines processing order
- Insert at appropriate position (default: after SoftClipper)

---

### Step 5: Update ParamController

Edit `src/control/param_controller.cpp`:

```cpp
#include "your_effect.h"

void ParamController::handleSetParam(uint8_t moduleId, uint8_t paramIndex, int32_t value) {
    switch (moduleId) {
        // ... existing cases ...
        
        case MODULE_ID_YOUR_EFFECT: {
            YourEffect* mod = static_cast<YourEffect*>(pipeline->getModule(moduleId));
            if (!mod) break;
            
            switch (paramIndex) {
                case 0: mod->setFrequency(value); break;      // Hz
                case 1: mod->setGain(value);                  // Q8.8 dB
                case 2: mod->setQFactor(value);               // Q6.10 Q
                default: break;
            }
            break;
        }
        
        // ... other modules ...
    }
}
```

---

### Step 6: Update PresetManager

Edit `src/control/preset_manager.cpp`:

The `PresetManager` uses friend access to read/write private members automatically:

```cpp
// Example: PresetManager saves module state
struct PresetSlot {
    // Your module state (accessed via friend class)
    int32_t your_effect_frequency;
    int32_t your_effect_gain_q88;
    int32_t your_effect_q_factor_q610;
    // ... other modules ...
};

// When saving preset:
PresetSlot slot;
slot.your_effect_frequency = your_effect._frequency;
slot.your_effect_gain_q88 = your_effect._gain_q88;
slot.your_effect_q_factor_q610 = your_effect._q_factor_q610;
// ... write to NVS ...

// When loading preset:
your_effect._frequency = slot.your_effect_frequency;
your_effect._gain_q88 = slot.your_effect_gain_q88;
your_effect._q_factor_q610 = slot.your_effect_q_factor_q610;
your_effect.recalculateCoefficients();  // Recompute
```

---

### Step 7: Add GUI Control

Edit `dsp-control-app/js/app.js`:

```javascript
const yourEffectControls = {
    id: 'your_effect',
    name: 'Your Effect',
    enabled: true,
    params: [
        {
            name: 'Frequency',
            id: 'param0',
            min: 20,
            max: 20000,
            default: 1000,
            unit: 'Hz',
            type: 'slider'
        },
        {
            name: 'Gain',
            id: 'param1',
            min: -60,
            max: 18,
            default: 0,
            unit: 'dB',
            type: 'slider',
            step: 0.5
        },
        {
            name: 'Q Factor',
            id: 'param2',
            min: 0.1,
            max: 10,
            default: 0.5,
            unit: '',
            type: 'slider',
            step: 0.1
        }
    ]
};

modules.push(yourEffectControls);
```

In `dsp-control-app/index.html`:

```html
<div class="module-control">
    <input type="checkbox" id="your_effect_enable" checked>
    <label>Your Effect</label>
    <div class="params">
        <label>Frequency:</label>
        <input type="range" id="your_effect_param0" min="20" max="20000" value="1000">
        <input type="number" id="your_effect_param0_value" value="1000">
        <span>Hz</span>
    </div>
    <div class="params">
        <label>Gain:</label>
        <input type="range" id="your_effect_param1" min="-60" max="18" value="0" step="0.5">
        <input type="number" id="your_effect_param1_value" value="0">
        <span>dB</span>
    </div>
    <div class="params">
        <label>Q Factor:</label>
        <input type="range" id="your_effect_param2" min="0.1" max="10" value="0.5" step="0.1">
        <input type="number" id="your_effect_param2_value" value="0.5">
    </div>
</div>
```

---

### Step 8: Build & Test

```bash
# Verify compilation
pio run

# If errors:
#  - Check forward declarations
#  - Verify friend class declarations
#  - Check MODULE_ID matches config.h

# Upload
pio run -t upload

# Test via GUI:
#  1. Enable Your Effect
#  2. Adjust parameters
#  3. Should update in real-time
#  4. Save preset → Should persist after reboot
```

---

### Best Practices for Parameter Setters

```cpp
// ✓ GOOD: Use fixed-point formats for parameters
void setGain(int32_t gain_q88) {
    // Q8.8 format: 256 = 1dB
    _gainLinear = db_q88_to_linear_gain((int16_t)gain_q88);
}

// ✓ GOOD: Validate ranges
_frequency = (freq < 20) ? 20 : (freq > 20000) ? 20000 : freq;

// ✓ GOOD: Cache computed values
float _gainLinear;  // Store computed gain, not just dB

// ✗ BAD: Expensive per-sample computations
samples[i] *= pow(10.0f, _gain_db / 20.0f);  // Very slow!

// ✓ GOOD: Lazy recalculation (optimization)
if (_frequency != _lastFreq) {
    recalculateCoefficients();
    _lastFreq = _frequency;
}
```

---

### Parameter Formats

| Format | Used For | Example |
|--------|----------|---------|
| int32 (linear) | Hz, counts, modes | Frequency: 1000 Hz |
| Q8.8 (fixed-point dB) | Gain, levels | Gain: 256 (= 1dB) |
| Q6.10 (fixed-point) | Q factor, multipliers | Q: 512 (= 0.5 Q) |

**Conversion**:
```cpp
// Float dB → Q8.8
int32_t gain_q88 = (int32_t)(gain_db * 256.0f);

// Q8.8 → Linear gain
float linear_gain = db_q88_to_linear_gain((int16_t)gain_q88);

// Q6.10 → Float
float q_float = (float)q_q610 / 1024.0f;
```

---

<a name="tiếng-việt"></a>

## Thêm Module DSP Mới (Tiếng Việt)

Hướng dẫn này giải thích cách tạo và tích hợp một module DSP mới vào pipeline.

---

### Tổng Quan Kiến Trúc

Mỗi module DSP:
- Kế thừa từ lớp cơ sở `DspModule`
- Triển khai phương thức `process()` để xử lý buffer âm thanh
- Khai báo `friend class PresetManager` và `friend class ParamController` để truy cập tham số
- Cung cấp các method setter như `setParam()` để cập nhật tham số
- Quản lý trạng thái ở các member riêng (định dạng fixed-point Q8.8 cho giá trị dB)
- Hoạt động trên âm thanh float32 (96kHz, stereo xen kẽ)

---

### Bước 1: Tạo File Header

Tạo `src/dsp/your_effect.h`:

```cpp
#ifndef YOUR_EFFECT_H
#define YOUR_EFFECT_H

#include "dsp_module.h"
#include "biquad.h"
#include "../utils/fixed_math.h"

// Khai báo trước cho truy cập friend
class PresetManager;
class ParamController;

class YourEffect : public DspModule {
    friend class PresetManager;      // Cho phép lưu/tải preset
    friend class ParamController;    // Cho phép điều khiển tham số UART

public:
    void init(int32_t sampleRate, int32_t numChannels) override;
    void process(float* __restrict samples, size_t numSamples) override;
    void reset() override;

    const char* getName() const override { return "YourEffect"; }
    uint8_t getModuleId() const override { return MODULE_ID_YOUR_EFFECT; }

    // ---- Setter Tham Số (gọi bởi ParamController) ----
    void setFrequency(int32_t freq);      // Hz
    void setGain(int32_t gain_q88);       // Định dạng Q8.8 (dB)
    void setQFactor(int32_t q_q610);      // Định dạng Q6.10

private:
    // ---- Trạng Thái (PresetManager truy cập qua friend) ----
    Biquad _filter_left;
    Biquad _filter_right;
    
    int32_t _frequency;                   // Hz
    int32_t _gain_q88;                    // Q8.8 format dB
    int32_t _q_factor_q610;               // Q6.10 format
    
    float _gainLinear;                    // Cached linear gain
    int32_t _lastFreq;                    // Để tối ưu

    void recalculateCoefficients();
};

#endif
```

---

### Bước 2: Triển Khai Module

Tạo `src/dsp/your_effect.cpp`:

```cpp
#include "your_effect.h"
#include "config.h"

void YourEffect::init(int32_t sampleRate, int32_t numChannels) {
    DspModule::init(sampleRate, numChannels);
    
    // Khởi tạo giá trị mặc định
    _frequency = 1000;              // 1kHz
    _gain_q88 = 0;                  // 0dB
    _q_factor_q610 = 512;           // 0.5 Q
    _lastFreq = -1;                 // Buộc tính toán lại lần đầu
    
    recalculateCoefficients();
    reset();
}

void IRAM_ATTR YourEffect::process(float* __restrict samples, size_t numSamples) {
    if (!_enabled) return;
    
    // Xử lý stereo xen kẽ
    for (size_t i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < _numChannels; ch++) {
            size_t idx = i * _numChannels + ch;
            
            // Áp dụng bộ lọc
            float filtered = _filter_left.processSample(samples[idx], ch);
            
            // Áp dụng đạt được
            samples[idx] = filtered * _gainLinear;
        }
    }
}

void YourEffect::reset() {
    _filter_left.reset();
    _filter_right.reset();
}

void YourEffect::recalculateCoefficients() {
    if (_frequency == _lastFreq) return;
    
    _lastFreq = _frequency;
    float q_float = (float)_q_factor_q610 / 1024.0f;  // Q6.10 → float
    
    _filter_left.design(
        EQ_FILTER_TYPE_LOW_PASS,
        (float)_frequency,
        q_float,
        0,
        (float)_sampleRate
    );
    _filter_right = _filter_left;
}

// ---- Setter Tham Số ----

void YourEffect::setFrequency(int32_t freq) {
    _frequency = (freq < 20) ? 20 : (freq > 20000) ? 20000 : freq;
    recalculateCoefficients();
}

void YourEffect::setGain(int32_t gain_q88) {
    _gain_q88 = (gain_q88 < -15360) ? -15360 : (gain_q88 > 4608) ? 4608 : gain_q88;
    _gainLinear = db_q88_to_linear_gain((int16_t)_gain_q88);
}

void YourEffect::setQFactor(int32_t q_q610) {
    _q_factor_q610 = (q_q610 < 102) ? 102 : (q_q610 > 10240) ? 10240 : q_q610;
    recalculateCoefficients();
}
```

---

### Bước 3: Cập Nhật Config

Chỉnh sửa `include/config.h`:

```cpp
#define MODULE_ID_YOUR_EFFECT    0x0D    // ID tiếp theo
#define DSP_MODULE_COUNT         13      // Tăng lên nếu cần
```

---

### Bước 4: Đăng Ký trong Pipeline

Pipeline sử dụng array `_chain[]` được xây dựng trong quá trình khởi tạo. Các module được xử lý bằng cách lặp qua chuỗi này.

Chỉnh sửa `src/dsp/dsp_pipeline.h`:

```cpp
#include "your_effect.h"

class DspPipeline {
private:
    static const size_t CHAIN_LENGTH = 13;  // Tăng nếu thêm module
    
    // ... các module hiện có ...
    YourEffect _yourEffect;                 // Thêm instance module
    
    DspModule* _chain[CHAIN_LENGTH];        // Array chuỗi

public:
    void init(int32_t sampleRate, int32_t numChannels);
    void processFrame(float* __restrict samples, size_t numSamples);
    
    // ... các accessor ...
    YourEffect& getYourEffect() { return _yourEffect; }
};
```

Chỉnh sửa `src/dsp/dsp_pipeline.cpp` - trong method `init()`:

```cpp
void DspPipeline::init(int32_t sampleRate, int32_t numChannels) {
    // Xây dựng chuỗi theo thứ tự xử lý
    _chain[0]  = &_noiseGate;
    _chain[1]  = &_compander;
    _chain[2]  = &_exciter;
    _chain[3]  = &_virtualBass;
    _chain[4]  = &_bassClassic;
    _chain[5]  = &_stereoWidener;
    _chain[6]  = &_dynamicEq;
    _chain[7]  = &_eqDsp_1;
    _chain[8]  = &_eqDsp_2;
    _chain[9]  = &_drc;
    _chain[10] = &_volume;
    _chain[11] = &_softClipper;
    _chain[12] = &_yourEffect;      // Thêm vào vị trí mong muốn
    
    // Đặt module ID
    _yourEffect.setModuleId(MODULE_ID_YOUR_EFFECT);
    
    // Khởi tạo tất cả modules
    for (size_t i = 0; i < CHAIN_LENGTH; i++) {
        _chain[i]->init(sampleRate, numChannels);
    }
    
    // Bật/tắt modules
    _volume.enable();
    _yourEffect.enable();              // Bật mặc định
}
```

**⚠️ Quan Trọng**: 
- `processFrame()` tự động lặp qua `_chain[]`
- **KHÔNG** thêm trực tiếp gọi `process()` trong `processFrame()`
- Thứ tự module trong `_chain[]` xác định thứ tự xử lý
- Chèn vào vị trí phù hợp (mặc định: sau SoftClipper)

---

### Bước 5: Cập Nhật ParamController

Thêm case trong `src/control/param_controller.cpp`:

```cpp
case MODULE_ID_YOUR_EFFECT: {
    YourEffect* mod = static_cast<YourEffect*>(pipeline->getModule(moduleId));
    if (!mod) break;
    
    switch (paramIndex) {
        case 0: mod->setFrequency(value); break;
        case 1: mod->setGain(value); break;
        case 2: mod->setQFactor(value); break;
        default: break;
    }
    break;
}
```

---

### Bước 6: PresetManager Tự Động

`PresetManager` sử dụng friend access để tự động lưu/tải:

```cpp
// Lưu: Truy cập private members qua friend
slot.your_effect_frequency = your_effect._frequency;

// Tải: Ghi lại và tính toán
your_effect._frequency = slot.your_effect_frequency;
your_effect.recalculateCoefficients();
```

---

### Bước 7: Thêm GUI

Thêm vào `dsp-control-app/js/app.js` và `index.html` với các slider cho các tham số.

---

### Bước 8: Xây Dựng & Kiểm Tra

```bash
pio run -t upload && pio device monitor -b 115200
```

Kiểm tra:
1. Bật module
2. Điều chỉnh tham số → Cập nhật real-time
3. Lưu preset → Lưu được sau khởi động lại

---

### Danh Sách Kiểm Tra

- ✓ Module kế thừa `DspModule`
- ✓ Khai báo `friend` cho PresetManager/ParamController
- ✓ Các method setter với xác thực phạm vi
- ✓ Sử dụng fixed-point format (Q8.8, Q6.10)
- ✓ Cấp phát bộ nhớ tĩnh (không malloc)
- ✓ Đăng ký module ID trong config.h
- ✓ Thêm vào pipeline và ParamController
- ✓ Cập nhật GUI controls
- ✓ Biên dịch thành công
- ✓ Kiểm tra trên ESP32

---

### Step 1: Create Module Header File

Create `src/dsp/your_effect.h`:

```cpp
#ifndef YOUR_EFFECT_H
#define YOUR_EFFECT_H

#include "dsp_module.h"
#include "biquad.h"

class YourEffect : public DSPModule {
private:
    // Internal state
    Biquad filter_left;
    Biquad filter_right;
    float param1;
    float param2;
    
    // Coefficients
    float last_frequency;

public:
    YourEffect();
    ~YourEffect();
    
    // Override base class methods
    void process(float32_t* buffer, uint32_t samples) override;
    void setParameter(uint8_t index, float value) override;
    float getParameter(uint8_t index) const override;
    void reset() override;
    
    // Optional: custom methods
    void updateCoefficients(float freq, float q);
};

#endif
```

---

### Step 2: Implement Module

Create `src/dsp/your_effect.cpp`:

```cpp
#include "your_effect.h"
#include <cmath>

#define SAMPLE_RATE 96000.0f

YourEffect::YourEffect() 
    : DSPModule("YourEffect"), 
      param1(0.0f), 
      param2(1.0f),
      last_frequency(0.0f) {
    // Initialize filters with neutral coefficients
    filter_left.setNeutral();
    filter_right.setNeutral();
}

YourEffect::~YourEffect() {
    // Cleanup if needed
}

void YourEffect::process(float32_t* buffer, uint32_t samples) {
    if (!enabled) return;
    
    // Process stereo interleaved: [L0, R0, L1, R1, ...]
    for (uint32_t i = 0; i < samples; i += 2) {
        // Left channel
        buffer[i] = filter_left.process(buffer[i] * param1);
        
        // Right channel
        buffer[i + 1] = filter_right.process(buffer[i + 1] * param1);
    }
}

void YourEffect::setParameter(uint8_t index, float value) {
    switch (index) {
        case 0:  // Frequency parameter
            param1 = fmax(20.0f, fmin(20000.0f, value));  // Clamp to audio range
            updateCoefficients(param1, param2);
            break;
        case 1:  // Q parameter
            param2 = fmax(0.1f, fmin(10.0f, value));
            updateCoefficients(param1, param2);
            break;
        default:
            break;
    }
}

float YourEffect::getParameter(uint8_t index) const {
    switch (index) {
        case 0: return param1;
        case 1: return param2;
        default: return 0.0f;
    }
}

void YourEffect::reset() {
    filter_left.reset();
    filter_right.reset();
    param1 = 0.0f;
    param2 = 1.0f;
}

void YourEffect::updateCoefficients(float freq, float q) {
    if (freq == last_frequency) return;  // Skip if unchanged
    
    // Compute biquad coefficients (low-pass example)
    float w0 = 2.0f * M_PI * freq / SAMPLE_RATE;
    float sin_w0 = sinf(w0);
    float cos_w0 = cosf(w0);
    float alpha = sin_w0 / (2.0f * q);
    
    float b0 = (1.0f - cos_w0) / 2.0f;
    float b1 = 1.0f - cos_w0;
    float b2 = (1.0f - cos_w0) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha;
    
    // Normalize
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;
    
    // Update both channels
    filter_left.setCoefficients(b0, b1, b2, a1, a2);
    filter_right.setCoefficients(b0, b1, b2, a1, a2);
    
    last_frequency = freq;
}
```

---

### Step 3: Register Module in Pipeline

Edit `src/dsp/dsp_pipeline.h`:

```cpp
#include "your_effect.h"

class DSPPipeline {
private:
    // ... existing modules ...
    YourEffect your_effect;  // Add your module
    
public:
    void initModules() {
        modules[YOUR_MODULE_ID] = &your_effect;  // Register
        // ...
    }
};
```

Define the module ID in `include/config.h`:

```cpp
#define DSP_MODULE_YOUR_EFFECT    13  // Next available ID
```

---

### Step 4: Add to Pipeline Order

In `src/dsp/dsp_pipeline.cpp`, add to the processing chain:

```cpp
void DSPPipeline::process(float32_t* buffer, uint32_t samples) {
    // Existing modules...
    noise_gate.process(buffer, samples);
    compander.process(buffer, samples);
    // ... other modules ...
    
    // Add your module at appropriate position
    your_effect.process(buffer, samples);  // Process in chain
    
    // Continue with rest...
    volume.process(buffer, samples);
    soft_clipper.process(buffer, samples);
}
```

---

### Step 5: Add GUI Control

Edit `dsp-control-app/js/app.js` to add UI elements:

```javascript
// In the module controls section:
const yourEffectControls = {
    id: 'your_effect',
    name: 'Your Effect',
    params: [
        {
            name: 'Frequency',
            id: 'param0',
            min: 20,
            max: 20000,
            default: 1000,
            unit: 'Hz',
            type: 'slider'
        },
        {
            name: 'Q Factor',
            id: 'param1',
            min: 0.1,
            max: 10.0,
            default: 1.0,
            type: 'slider'
        }
    ]
};

modules.push(yourEffectControls);
```

In `dsp-control-app/index.html`:

```html
<div class="module-control">
    <input type="checkbox" id="your_effect_enable" checked>
    <label>Your Effect</label>
    <div class="params">
        <label>Frequency:</label>
        <input type="range" id="your_effect_param0" min="20" max="20000" value="1000">
        <span id="your_effect_param0_label">1000 Hz</span>
    </div>
    <div class="params">
        <label>Q Factor:</label>
        <input type="range" id="your_effect_param1" min="0.1" max="10.0" value="1.0" step="0.1">
        <span id="your_effect_param1_label">1.0</span>
    </div>
</div>
```

---

### Step 6: Build & Test

```bash
# Build firmware
pio run

# If compilation fails:
# - Check header guards
# - Verify #include paths
# - Check for syntax errors

# Upload
pio run -t upload

# Monitor
pio device monitor -b 115200
```

---

### Module Design Best Practices

#### 1. **Memory Efficiency**
```cpp
// ✓ GOOD: Static allocation
class MyEffect : public DSPModule {
private:
    float state[256];  // Pre-allocated buffer
    Biquad filters[2]; // Left/right channels
};

// ✗ BAD: Dynamic allocation
void process(...) {
    float* buffer = new float[256];  // No! Causes fragmentation
    // ...
    delete[] buffer;
}
```

#### 2. **Real-Time Safety**
```cpp
// ✓ GOOD: No locks, no mallocs in process()
void process(float32_t* buffer, uint32_t samples) {
    // Simple, deterministic operations only
    for (uint32_t i = 0; i < samples; i++) {
        buffer[i] *= gain;  // Safe
    }
}

// ✗ BAD: Non-deterministic
void process(float32_t* buffer, uint32_t samples) {
    vector<float> v(samples);  // Dynamic allocation!
    // ...
}
```

#### 3. **Parameter Bounds**
```cpp
// ✓ GOOD: Clamp parameters
void setParameter(uint8_t index, float value) {
    param = fmax(MIN_VAL, fmin(MAX_VAL, value));
}

// ✗ BAD: Unchecked values
void setParameter(uint8_t index, float value) {
    param = value;  // Could crash if out of range
}
```

#### 4. **CPU Efficiency**
```cpp
// ✓ GOOD: Avoid expensive operations in tight loop
void process(float32_t* buffer, uint32_t samples) {
    float gain_scale = db_to_linear(gain);  // Outside loop
    for (uint32_t i = 0; i < samples; i++) {
        buffer[i] *= gain_scale;
    }
}

// ✗ BAD: Expensive per-sample
void process(float32_t* buffer, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
        buffer[i] *= pow(10.0f, gain / 20.0f);  // Very slow!
    }
}
```

---

### Example: Complete Simple Module

Here's a complete example of a simple **Gain** module:

**Header** (`gain_custom.h`):
```cpp
#ifndef GAIN_CUSTOM_H
#define GAIN_CUSTOM_H

#include "dsp_module.h"

class GainCustom : public DSPModule {
private:
    float gain_db;
    float gain_linear;
    
    void updateGain(float db);

public:
    GainCustom();
    void process(float32_t* buffer, uint32_t samples) override;
    void setParameter(uint8_t index, float value) override;
    float getParameter(uint8_t index) const override;
    void reset() override;
};

#endif
```

**Implementation** (`gain_custom.cpp`):
```cpp
#include "gain_custom.h"
#include <cmath>

GainCustom::GainCustom() 
    : DSPModule("GainCustom"), 
      gain_db(0.0f), 
      gain_linear(1.0f) {
}

void GainCustom::updateGain(float db) {
    gain_db = db;
    gain_linear = powf(10.0f, db / 20.0f);
}

void GainCustom::process(float32_t* buffer, uint32_t samples) {
    if (!enabled) return;
    
    for (uint32_t i = 0; i < samples; i++) {
        buffer[i] *= gain_linear;
    }
}

void GainCustom::setParameter(uint8_t index, float value) {
    if (index == 0) {
        updateGain(fmax(-80.0f, fmin(12.0f, value)));
    }
}

float GainCustom::getParameter(uint8_t index) const {
    return (index == 0) ? gain_db : 0.0f;
}

void GainCustom::reset() {
    gain_db = 0.0f;
    gain_linear = 1.0f;
}
```

---

<a name="tiếng-việt"></a>

## Thêm Module DSP Mới (Tiếng Việt)

Hướng dẫn này giải thích cách tạo và tích hợp một module DSP mới vào pipeline.

---

### Tổng Quan Kiến Trúc

Mỗi module DSP:
- Kế thừa từ lớp cơ sở `DSPModule`
- Triển khai phương thức `process()` để xử lý buffer âm thanh
- Quản lý tham số và trạng thái riêng
- Hoạt động trên âm thanh float32 (96kHz, 256 mẫu/frame)

---

### Bước 1: Tạo File Header

Tạo `src/dsp/your_effect.h`:

```cpp
#ifndef YOUR_EFFECT_H
#define YOUR_EFFECT_H

#include "dsp_module.h"
#include "biquad.h"

class YourEffect : public DSPModule {
private:
    Biquad filter_left;
    Biquad filter_right;
    float param1;
    float param2;
    float last_frequency;

public:
    YourEffect();
    ~YourEffect();
    
    void process(float32_t* buffer, uint32_t samples) override;
    void setParameter(uint8_t index, float value) override;
    float getParameter(uint8_t index) const override;
    void reset() override;
    void updateCoefficients(float freq, float q);
};

#endif
```

---

### Bước 2: Triển Khai Module

Tạo `src/dsp/your_effect.cpp`:

```cpp
#include "your_effect.h"
#include <cmath>

#define SAMPLE_RATE 96000.0f

YourEffect::YourEffect() 
    : DSPModule("YourEffect"), 
      param1(0.0f), 
      param2(1.0f) {
    filter_left.setNeutral();
    filter_right.setNeutral();
}

void YourEffect::process(float32_t* buffer, uint32_t samples) {
    if (!enabled) return;
    
    // Xử lý stereo xen kẽ: [L0, R0, L1, R1, ...]
    for (uint32_t i = 0; i < samples; i += 2) {
        buffer[i] = filter_left.process(buffer[i] * param1);
        buffer[i + 1] = filter_right.process(buffer[i + 1] * param1);
    }
}

void YourEffect::setParameter(uint8_t index, float value) {
    switch (index) {
        case 0:  // Tần số
            param1 = fmax(20.0f, fmin(20000.0f, value));
            updateCoefficients(param1, param2);
            break;
        case 1:  // Q
            param2 = fmax(0.1f, fmin(10.0f, value));
            updateCoefficients(param1, param2);
            break;
    }
}

float YourEffect::getParameter(uint8_t index) const {
    return (index == 0) ? param1 : param2;
}

void YourEffect::reset() {
    filter_left.reset();
    filter_right.reset();
}

void YourEffect::updateCoefficients(float freq, float q) {
    // Tính hệ số biquad...
    // (chi tiết giống như phiên bản tiếng Anh)
}
```

---

### Bước 3: Đăng Ký Module

Chỉnh sửa `src/dsp/dsp_pipeline.h`:

```cpp
#include "your_effect.h"

class DSPPipeline {
private:
    YourEffect your_effect;  // Thêm module
    
public:
    void initModules() {
        modules[YOUR_MODULE_ID] = &your_effect;
    }
};
```

---

### Bước 4: Thêm vào Thứ Tự Pipeline

Trong `src/dsp/dsp_pipeline.cpp`:

```cpp
void DSPPipeline::process(float32_t* buffer, uint32_t samples) {
    noise_gate.process(buffer, samples);
    // ... các module khác ...
    your_effect.process(buffer, samples);  // Thêm vào chuỗi
    volume.process(buffer, samples);
}
```

---

### Bước 5: Thêm Điều Khiển GUI

Chỉnh sửa `dsp-control-app/js/app.js`:

```javascript
const yourEffectControls = {
    id: 'your_effect',
    name: 'Your Effect',
    params: [
        {
            name: 'Tần Số',
            id: 'param0',
            min: 20,
            max: 20000,
            default: 1000,
            unit: 'Hz'
        },
        {
            name: 'Q Factor',
            id: 'param1',
            min: 0.1,
            max: 10.0,
            default: 1.0
        }
    ]
};
```

---

### Bước 6: Xây Dựng & Kiểm Tra

```bash
# Xây dựng
pio run

# Tải lên
pio run -t upload

# Giám sát
pio device monitor -b 115200
```

---

### Mẹo Thiết Kế Module

#### 1. **Hiệu Quả Bộ Nhớ**
```cpp
// ✓ TỐT: Cấp phát tĩnh
class MyEffect : public DSPModule {
private:
    float state[256];  // Buffer được cấp phát trước
};

// ✗ XẤU: Cấp phát động
void process(...) {
    float* buffer = new float[256];  // Gây phân mảnh!
}
```

#### 2. **An Toàn Real-Time**
```cpp
// ✓ TỐT: Không có khóa, không có malloc
void process(float32_t* buffer, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
        buffer[i] *= gain;  // An toàn
    }
}
```

#### 3. **Hiệu Suất CPU**
```cpp
// ✓ TỐT: Tính toán tốn kém bên ngoài vòng lặp
float gain_scale = db_to_linear(gain);  // Ngoài vòng lặp
for (...) {
    buffer[i] *= gain_scale;  // Bên trong vòng lặp
}
```

---

### Ví Dụ: Module Hoàn Chỉnh Đơn Giản

Một ví dụ hoàn chỉnh của module **Gain** đơn giản:

```cpp
// gain_custom.h
#ifndef GAIN_CUSTOM_H
#define GAIN_CUSTOM_H
#include "dsp_module.h"

class GainCustom : public DSPModule {
private:
    float gain_db;
    float gain_linear;
    void updateGain(float db);

public:
    GainCustom();
    void process(float32_t* buffer, uint32_t samples) override;
    void setParameter(uint8_t index, float value) override;
    float getParameter(uint8_t index) const override;
    void reset() override;
};
#endif
```

---

### Kiểm Tra Danh Sách

- ✓ Module kế thừa từ `DSPModule`
- ✓ Triển khai `process()` method
- ✓ Triển khai `setParameter()` / `getParameter()`
- ✓ Cấp phát bộ nhớ tĩnh
- ✓ Không có vòng lặp vô hạn
- ✓ Tham số được kiểm tra giới hạn
- ✓ Đăng ký trong pipeline
- ✓ Thêm GUI control
- ✓ Biên dịch thành công
- ✓ Kiểm tra trên ESP32
