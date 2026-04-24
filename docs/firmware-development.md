# Firmware Development Guide

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Firmware Development (English)

### Development Environment Setup

#### Prerequisites

1. **VS Code** + PlatformIO Extension
   - Install: https://platformio.org/platformio-ide
   - Or: `code --install-extension platformio.platformio-ide`

2. **Python 3.8+**
   ```bash
   python --version  # Should be 3.8 or newer
   ```

3. **Git**
   ```bash
   git --version
   ```

#### Installation

```bash
# Clone repository
git clone https://github.com/ohminecraft/esp32-dsp-core.git
cd esp32-dsp-core

# Install PlatformIO CLI (if not using VS Code)
pip install platformio

# Initialize project
pio project init --ide vscode
```

---

### Build Configuration

**platformio.ini** controls build settings:

```ini
[env:esp32-devkit]
platform = espressif32
board = esp32-devkit-v1
framework = arduino
monitor_speed = 115200

build_flags =
    -DCORE_DEBUG_LEVEL=0    # Change to 1-5 for debug output
    -O3
    -funroll-loops
    -fstrict-aliasing

lib_deps =
    # Add external libraries here if needed
```

**Debug Levels** (change `-DCORE_DEBUG_LEVEL`):
- `0` = NONE (no debug, smallest binary)
- `1` = ERROR (errors only)
- `2` = WARN (warnings + errors)
- `3` = INFO (general info)
- `4` = DEBUG (detailed info, recommended for development)
- `5` = VERBOSE (very detailed, for module debugging)

---

### Building

```bash
# Build only (no upload)
pio run

# Upload to device
pio run -t upload

# Clean build
pio run -t clean
pio run

# Monitor serial output
pio device monitor -b 115200

# Build and upload in one command
pio run -t upload && pio device monitor -b 115200
```

---

### Development Workflow

1. **Edit Code**
   ```bash
   # Modify src/dsp/your_module.cpp
   nano src/dsp/your_module.cpp
   ```

2. **Build & Upload**
   ```bash
   pio run -t upload
   ```

3. **Test & Debug**
   ```bash
   pio device monitor -b 115200
   
   # Example output:
   # [DSP] Module enabled: Exciter
   # [DSP] Exciter gain: 5.0 dB
   # [AUDIO] I2S initialized at 96000 Hz
   ```

4. **Iterate**
   - Make changes to code
   - Build and upload
   - Monitor output
   - Adjust parameters via GUI

---

### Debugging

#### Debug Logging via CORE_DEBUG_LEVEL

Control debug output from `platformio.ini` without modifying source code:

**Step 1**: Edit `platformio.ini` to change debug level:

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=4    # Change this value (0-5)
    -O3
    # ... other flags ...
```

**Step 2**: Rebuild and upload:

```bash
pio run -t upload
```

**Step 3**: View debug output in serial monitor:

```bash
pio device monitor -b 115200
```

**Example Output** (with CORE_DEBUG_LEVEL=4):

```
I (1234) ESP32_DSP: DSP Core Initializing...
D (1235) ESP32_DSP: Core 1: Audio Task Started
D (1236) ESP32_DSP: I2S Input configured: 96kHz, PCM1808
D (1237) ESP32_DSP: Processing pipeline: 12 modules active
D (1238) ESP32_DSP: Exciter gain: 5.0 dB
```

#### Using Debug Macros in Code

Debug macros are defined in `include/config.h` and automatically controlled by `CORE_DEBUG_LEVEL`:

```cpp
#include "config.h"

void YourModule::process(...) {
    // These only compile/run if CORE_DEBUG_LEVEL >= 4
    DEBUG_LOG("Processing %d samples", samples);
    DEBUG_LOG("Parameter value: %f", param);
    
    // CORE_DEBUG_LEVEL >= 3
    INFO_LOG("Module enabled");
    
    // CORE_DEBUG_LEVEL >= 2
    WARN_LOG("Near clipping: %f", level);
    
    // CORE_DEBUG_LEVEL >= 1
    ERROR_LOG("Invalid parameter");
}
```

#### Debug Level Reference

| Level | Purpose | Output | Binary Size |
|-------|---------|--------|-------------|
| 0 | Production | None | Smallest |
| 1 | Critical errors only | Errors | Small |
| 2 | Warnings + errors | Warn/Error | Medium |
| 3 | General info | Info/Warn/Error | Medium |
| 4 | Development | Debug/Info/Warn/Error | Larger |
| 5 | Deep debugging | Verbose/Debug/Info/... | Largest |

**Recommended**: Use Level 0 for production, Level 4 for development

---

### Performance Profiling

Monitor CPU usage and heap via GUI:

```
CPU Usage: 45%        → Safe (plenty of headroom)
CPU Usage: 80%        → High (consider disabling modules)
CPU Usage: > 95%      → Danger (audio will glitch)

Heap Left: 52 KB      → Healthy
Heap Left: < 20 KB    → Warning (fragmentation possible)
```

---

### Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `implicit declaration of function 'abs'` | Missing `#include <cmath>` | Add header |
| `undefined reference to '_Z...'` | Function not implemented | Implement or link library |
| `memory exhausted` | Code size too large | Enable `-Os` optimization |
| `No such file or directory: 'config.h'` | Include path wrong | Check `-I` flags in platformio.ini |

---

### Code Organization Best Practices

#### File Structure
```
src/
├── main.cpp              # Entry point, task creation
├── audio/
│   ├── audio_input.cpp   # I2S input implementation
│   └── audio_output.cpp  # I2S output implementation
├── dsp/
│   ├── dsp_pipeline.cpp  # Pipeline orchestrator
│   ├── dsp_module.h      # Base class definition
│   ├── noise_gate.cpp
│   ├── exciter.cpp
│   └── ...
├── control/
│   ├── uart_protocol.cpp # UART handler
│   ├── param_controller.cpp
│   └── preset_manager.cpp
└── utils/
    ├── debug_log.h       # Debug macros
    └── fixed_math.h      # Math utilities
```

#### Header Guards
Always use header guards:
```cpp
#ifndef MODULE_NAME_H
#define MODULE_NAME_H

// ... content ...

#endif
```

#### Includes
Order includes logically:
```cpp
#include <math.h>           // Standard library
#include <cstring>

#include "config.h"        // Project headers
#include "dsp_module.h"
#include "biquad.h"

#include "debug_log.h"     // Debug/utility headers
```

---

### Testing

#### Unit Testing (Manual)

Test individual modules:

```cpp
// In test_module.cpp
void test_module() {
    YourModule mod;
    float buffer[512];
    memset(buffer, 0.5f, sizeof(buffer));
    
    mod.setParameter(0, 5.0f);
    mod.process(buffer, 512);
    
    assert(buffer[0] > 0.5f);
}
```

#### Integration Testing (Real Hardware)

Test on actual ESP32:

1. Connect audio analyzer to PCM5102A output
2. Play test signal through PCM1808 input
3. Monitor frequency response
4. Check for distortion/clipping

---

<a name="tiếng-việt"></a>

## Phát Triển Firmware (Tiếng Việt)

### Thiết Lập Môi Trường

#### Yêu Cầu Chuẩn Bị

1. **VS Code** + PlatformIO
   ```bash
   code --install-extension platformio.platformio-ide
   ```

2. **Python 3.8+**
   ```bash
   python --version
   ```

3. **Git**
   ```bash
   git --version
   ```

#### Cài Đặt

```bash
# Clone dự án
git clone https://github.com/ohminecraft/esp32-dsp-core.git
cd esp32-dsp-core

# Cài PlatformIO CLI
pip install platformio
```

---

### Xây Dựng

```bash
# Xây dựng
pio run

# Tải lên ESP32
pio run -t upload

# Xóa và xây dựng lại
pio run -t clean
pio run

# Giám sát nối tiếp
pio device monitor -b 115200
```

---

### Quy Trình Phát Triển

1. **Chỉnh Sửa Mã**
   ```bash
   nano src/dsp/your_module.cpp
   ```

2. **Xây Dựng & Tải Lên**
   ```bash
   pio run -t upload
   ```

3. **Kiểm Tra & Gỡ Lỗi**
   ```bash
   pio device monitor -b 115200
   ```

4. **Lặp Lại**
   - Thay đổi mã
   - Xây dựng và tải lên
   - Giám sát đầu ra
   - Điều chỉnh tham số

---

### Gỡ Lỗi

#### Kích Hoạt Debug qua CORE_DEBUG_LEVEL

Kiểm soát đầu ra debug từ `platformio.ini` mà không cần sửa mã nguồn:

**Bước 1**: Chỉnh sửa `platformio.ini` để thay đổi debug level:

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=4    # Thay giá trị này (0-5)
    -O3
    # ... flags khác ...
```

**Bước 2**: Xây dựng lại và tải lên:

```bash
pio run -t upload
```

**Bước 3**: Xem đầu ra debug:

```bash
pio device monitor -b 115200
```

**Mức Debug** (thay đổi `-DCORE_DEBUG_LEVEL`):
- `0` = NONE (không debug, nhị phân nhỏ nhất)
- `1` = ERROR (chỉ lỗi)
- `2` = WARN (cảnh báo + lỗi)
- `3` = INFO (thông tin chung)
- `4` = DEBUG (thông tin chi tiết, khuyên dùng cho phát triển)
- `5` = VERBOSE (rất chi tiết, để debug module)

#### Sử Dụng Debug Macro trong Mã

Debug macro được định nghĩa trong `include/config.h` và tự động được điều khiển bởi `CORE_DEBUG_LEVEL`:

```cpp
#include "config.h"

void YourModule::process(...) {
    // Chỉ biên dịch/chạy nếu CORE_DEBUG_LEVEL >= 4
    DEBUG_LOG("Processing %d samples", samples);
    
    // CORE_DEBUG_LEVEL >= 3
    INFO_LOG("Module enabled");
    
    // CORE_DEBUG_LEVEL >= 2
    WARN_LOG("Gần cắt ngắn: %f", level);
    
    // CORE_DEBUG_LEVEL >= 1
    ERROR_LOG("Tham số không hợp lệ");
}
```

#### Bảng Tham Khảo Mức Debug

| Mức | Mục Đích | Đầu Ra | Kích Thước Nhị Phân |
|-----|----------|--------|-------------------|
| 0 | Sản xuất | Không | Nhỏ nhất |
| 1 | Lỗi nghiêm trọng | Lỗi | Nhỏ |
| 2 | Cảnh báo + lỗi | Cảnh báo/Lỗi | Trung bình |
| 3 | Thông tin chung | Info/Cảnh báo/Lỗi | Trung bình |
| 4 | Phát triển | Debug/Info/... | Lớn |
| 5 | Debug sâu | Verbose/Debug/... | Lớn nhất |

**Khuyên dùng**: Dùng Mức 0 cho sản xuất, Mức 4 để phát triển

---

### Tổ Chức Mã

#### Cấu Trúc Thư Mục

```
src/
├── main.cpp              # Điểm vào
├── audio/
│   ├── audio_input.cpp   # Đầu vào âm thanh
│   └── audio_output.cpp  # Đầu ra âm thanh
├── dsp/
│   ├── dsp_pipeline.cpp  # Pipeline của module, chạy theo chain
│   └── [các module]
├── control/
│   ├── uart_protocol.cpp  # Giao tiếp với APP
│   └── [các bộ điều khiển]
└── utils/
    ├── debug_log.h        # Debug Code
    └── fixed_math.h       # Hàm Toán Fixed
```

#### Include

```cpp
#include <math.h>           // Thư Viện có sẵn
#include <cstring>

#include "config.h"        // Header của Project
#include "dsp_module.h"
#include "biquad.h"

#include "debug_log.h"     // Header của Debug/tiện ích
```

---

### Lỗi Biên Dịch Phổ Biến

| Lỗi | Nguyên Nhân | Cách Sửa |
|-----|-----------|---------|
| `implicit declaration of function` | Thiếu #include | Thêm header cần thiết |
| `undefined reference` | Hàm không triển khai | Triển khai hoặc link |
| `memory exhausted` | Code quá lớn | Bật tối ưu hóa |

---

### Kiểm Tra

#### Kiểm Tra Đơn Vị

```cpp
void test_module() {
    YourModule mod;
    float buffer[512];
    memset(buffer, 0.5f, sizeof(buffer));
    
    mod.setParameter(0, 5.0f);
    mod.process(buffer, 512);
    
    assert(buffer[0] > 0.5f);
}
```
