# Adding New DSP Effects - ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Adding New DSP Modules (English)

This guide explains how to create and integrate a new DSP module into the pipeline.

---

### Architecture Overview

Each DSP module:
- Inherits from `DSPModule` base class
- Implements `process()` method to operate on audio buffers
- Manages its own parameters and state
- Operates on float32 audio (96kHz, 256 samples/frame)

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
