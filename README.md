# ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

A professional real-time audio digital signal processing (DSP) framework running on ESP32 microcontroller with an Electron-based GUI control application. Inspired by the MVSilicon BP10xx audio processor architecture.

**Status**: ✅ Stable (Some Modules still UNSTABLE) and Production-Ready | **Sample Rate**: 96 kHz | **Modules**: 12 | **Latency**: 2.67ms

---

<a name="english"></a>

---

## 🎯 Overview

ESP32 DSP Core is a sophisticated audio processing platform that combines:
- **Real-time DSP pipeline** with 12 specialized audio modules
- **Professional audio processing** (EQ, compression, excitation, bass enhancement, spatial processing)
- **Dual-core optimization** for real-time performance
- **Binary protocol control** via UART (115200 baud)
- **Desktop GUI application** (Electron) for parameter control and preset management
- **Static memory architecture** for predictable, glitch-free operation

Perfect for:
- Audio equipment prototyping
- Real-time audio effects processing
- Embedded audio enhancement
- Audio signal analysis and manipulation
- IoT audio devices

---

## 🏗️ Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                       ESP32 (Dual-Core)                     │
├─────────────────────────────┬───────────────────────────────┤
│  Core 0 (Control Task)      │  Core 1 (Audio Task)          │
│  ├─ UART Protocol Handler   │  ├─ I2S Input (PCM1808 ADC)  │
│  ├─ Preset Manager (NVS)    │  ├─ DSP Pipeline (12 modules)│
│  ├─ Parameter Controller    │  └─ I2S Output (PCM5102A DAC)│
│  └─ Command Queue           │                               │
└──────────────┬──────────────┴───────────────────────────────┘
               │
        ┌──────▼──────┐
        │   UART      │ 115200 baud
        │   Control   │◄────────────────┐
        └─────────────┘                 │
                                   ┌────┴──────────┐
                                   │ Electron GUI  │
                                   │ Control App   │
                                   └───────────────┘
```

### DSP Pipeline (12 Modules in Signal Chain)

```
INPUT (I2S: 96kHz stereo, 256 samples/frame)
    ↓
[1]  ┌─────────────────┐
     │  Noise Gate     │  Removes background noise below threshold
     └────────┬────────┘
              ↓
[2]  ┌─────────────────┐
     │  Compander      │  Compressor/Expander with dual ratios
     └────────┬────────┘
              ↓
[3]  ┌─────────────────┐
     │  Exciter        │  Adds high-frequency harmonic sparkle
     └────────┬────────┘
              ↓
[4]  ┌─────────────────┐
     │  Virtual Bass   │  Psychoacoustic sub-bass generation
     └────────┬────────┘
              ↓
[5]  ┌─────────────────┐
     │  Bass Classic   │  Resonant bass boost (low-shelf EQ)
     └────────┬────────┘
              ↓
[6]  ┌─────────────────┐
     │ Stereo Widener  │  M/S stereo enhancement & width control
     └────────┬────────┘
              ↓
[7]  ┌─────────────────┐
     │ Dynamic EQ      │  Level-dependent EQ switching (2 EQ curves)
     └────────┬────────┘
              ↓
[8]  ┌─────────────────┐
     │  EQ1 (Main)     │  Parametric EQ - 10 bands configurable
     └────────┬────────┘
              ↓
[9]  ┌─────────────────┐
     │  EQ2 (Tone)     │  Post-EQ / Sound Signature Shaping
     └────────┬────────┘
              ↓
[10] ┌─────────────────┐
     │  DRC            │  Multi-band Dynamic Range Compressor
     └────────┬────────┘
              ↓
[11] ┌─────────────────┐
     │  Volume         │  Master Volume with smooth gain ramping
     └────────┬────────┘
              ↓
[12] ┌─────────────────┐
     │  Soft Clipper   │  Soft clipping for speaker protection
     └────────┬────────┘
              ↓
OUTPUT (I2S: PCM5102A DAC, 96kHz stereo)
```

**Processing Budget**: 2.67ms per frame  
**Implementation**: Float32 in-place processing, no heap allocations

---

## 📦 Hardware Requirements

### Microcontroller
- **ESP32-DevKit** (dual-core Xtensa LX6 @ 240 MHz)
  - 16KB SRAM per audio task
  - Static memory architecture (no dynamic allocation)
  - FreeRTOS with real-time priorities

### Audio I/O
- **PCM1808** - Stereo ADC (input)
  - I2S slave mode on port 1 (I2S_NUM_1)
  - MCLK on GPIO0
  - Stereo line-level inputs
  
- **PCM5102A** - Stereo DAC (output)
  - I2S slave mode on port 0 (I2S_NUM_0)
  - Stereo line-level outputs

### Pin Configuration
```
I2S Output (to PCM5102A DAC):
  GPIO22 ─ DATA (DIN)
  GPIO26 ─ BCK (bit clock)
  GPIO25 ─ WS (word select)

I2S Input (from PCM1808 ADC):
  GPIO35 ─ DATA (DOUT)
  GPIO26 ─ BCK (shared)
  GPIO25 ─ WS (shared)
  GPIO0  ─ MCLK (multiplied clock)

Control Interface:
  GPIO16 ─ UART2 RX (from GUI)
  GPIO17 ─ UART2 TX (to GUI)
```

---

## 🚀 Getting Started

### 1. Hardware Setup

1. Connect PCM1808 ADC to ESP32 I2S port 1
2. Connect PCM5102A DAC to ESP32 I2S port 0
3. Share BCK/WS lines between ADC and DAC
4. Connect GPIO0 to MCLK (with appropriate pulldown for boot)
5. Connect UART2 (GPIO16/17) to USB-UART adapter

### 2. Firmware Installation

**Prerequisites**: PlatformIO (VS Code extension or CLI)

```bash
# Clone repository
git clone https://github.com/yourname/esp32-dsp-core.git
cd esp32-dsp-core

# Build and upload
pio run -t upload

# Monitor serial output
pio device monitor -b 115200
```

**Configuration**: Edit `include/config.h` for sample rate, frame size, and module settings

### 3. Control Application Setup

```bash
cd dsp-control-app

# Install dependencies
npm install

# Run development version
npm start

# Build standalone app
npm run make
```

**Requirements**: Node.js 14+ | Electron 28

---

## 🎚️ DSP Modules (12 Total)

| # | Module | Parameters | Stability | Use Case |
|---|--------|-----------|-----------|----------|
| 1 | **NoiseGate** | Threshold, Attack, Release | Unstable | Remove background hiss/noise |
| 2 | **Compander** | Threshold, Ratio↑, Ratio↓, Attack, Release | Unstable | Dynamic compression/expansion |
| 3 | **Exciter** | Gain, Saturation, HPF Freq | Unstable+ | Add clarity and presence |
| 4 | **VirtualBass** | Gain, Frequency, Intensity | Unstable+ | Generate phantom bass via harmonics |
| 5 | **BassClassic** | Gain, Center Freq | Stable | Warm bass boost (low-shelf) |
| 6 | **StereoWidener** | Gain, Width | Stable | Enhance stereo field (M/S) |
| 7 | **DynamicEQ** | Low EQ / High EQ settings | Stable | Switch between 2 EQs by signal level |
| 8 | **EQ1** | 10 bands (Freq, Gain, Q) | Config-Dependent | Main parametric EQ |
| 9 | **EQ2** | 10 bands (Freq, Gain, Q) | Config-Dependent | Post-EQ tone shaping |
| 10 | **DRC** | Up to 4 bands, Gain, Ratio | Unstable | Multi-band compression/limiting |
| 11 | **Volume** | Gain, Ramp Time | Stable | Master volume control |
| 12 | **SoftClipper** | Threshold, Knee | Stable | Protect speakers, add warmth |

### Module Configuration Format

Each module configuration in presets includes:
```cpp
struct ModuleState {
    bool enabled;           // Enable/disable module
    float parameters[N];    // Module-specific parameters
};
```

---

## 🔌 Control Protocol

### UART Frame Format (Binary)

```
[SYNC: 0xAA55] [CMD] [MODULE_ID] [LEN: 2B] [DATA: N bytes] [CRC8]
```

**Baud Rate**: 115200  
**Error Checking**: CRC8 (8-bit cyclic redundancy check)

### Command Types

| Command | Opcode | Direction | Purpose |
|---------|--------|-----------|---------|
| SET_PARAM | 0x01 | PC→ESP | Set module parameter |
| GET_PARAM | 0x02 | ESP→PC | Query module state |
| ENABLE_MODULE | 0x03 | PC→ESP | Enable module |
| DISABLE_MODULE | 0x04 | PC→ESP | Disable module |
| SET_EQ_BAND | 0x05 | PC→ESP | Configure EQ band |
| SAVE_PRESET | 0x06 | PC→ESP | Save preset to NVS |
| LOAD_PRESET | 0x07 | PC→ESP | Load preset from NVS |
| GET_STATUS | 0x08 | ESP→PC | Query system status (CPU, heap) |
| RESET | 0x0F | PC→ESP | Reset DSP pipeline |

---

## 💾 Preset Management

- **Storage**: NVS (Non-Volatile Storage) - 8 preset slots
- **Auto-Load**: Preset slot 0 loads automatically on ESP32 boot
- **Content**: Full DSP state (module enables, parameters, EQ curves)
- **Control**: Save/load via GUI or UART commands

**Preset Structure**:
```cpp
struct Preset {
    bool module_enabled[12];        // Module on/off
    float module_params[12][10];    // Parameters per module
    uint8_t crc;                    // Validation checksum
};
```

---

## ⚙️ Audio Specifications

| Parameter | Value |
|-----------|-------|
| **Sample Rate** | 96 kHz |
| **Channels** | 2 (Stereo) |
| **Bit Depth** | 32-bit Float |
| **Frame Size** | 256 samples (2.67ms latency) |
| **Format** | Interleaved (L, R, L, R, ...) |
| **I2S Port 0** | Output (DAC) |
| **I2S Port 1** | Input (ADC) |
| **Buffer Strategy** | Static allocation, in-place processing |

---

## 📊 Performance Metrics

| Metric | Value |
|--------|-------|
| **CPU Core Affinity** | Core 1 (audio) + Core 0 (control) |
| **Audio Task Priority** | 23/25 (critical real-time) |
| **Processing Time** | ~1.5-2.0ms per frame |
| **Headroom** | 0.67-1.17ms spare per 2.67ms frame |
| **RAM Usage** | ~20KB static (no heap) |
| **Jitter** | < 100µs (deterministic) |
| **Thermal Stability** | Tested up to 60°C |

---

## 🔧 Development

### Project Structure

```
esp32-dsp-core/
├── src/
│   ├── main.cpp                  # Entry point, FreeRTOS tasks
│   ├── audio/
│   │   ├── audio_input.cpp/.h    # I2S ADC input handler
│   │   └── audio_output.cpp/.h   # I2S DAC output handler
│   ├── dsp/
│   │   ├── dsp_pipeline.cpp/.h   # Module orchestrator
│   │   ├── dsp_module.h          # Base class for all modules
│   │   ├── noise_gate.cpp/.h     # [1] Noise gate
│   │   ├── compander.cpp/.h      # [2] Compander
│   │   ├── exciter.cpp/.h        # [3] Exciter
│   │   ├── virtual_bass.cpp/.h   # [4] Virtual bass
│   │   ├── bass_classic.cpp/.h   # [5] Bass classic
│   │   ├── stereo_widener.cpp/.h # [6] Stereo widener
│   │   ├── eq.cpp/.h             # [8,9] Parametric EQ
│   │   ├── dynamic_eq.cpp/.h     # [7] Dynamic EQ
│   │   ├── drc.cpp/.h            # [10] Multi-band DRC
│   │   ├── volume.cpp/.h         # [11] Master volume
│   │   ├── soft_clip.cpp/.h      # [12] Soft clipper
│   │   └── biquad.cpp/.h         # Base biquad filter
│   ├── control/
│   │   ├── uart_protocol.cpp/.h  # UART command parser
│   │   ├── param_controller.cpp/.h # Parameter dispatcher
│   │   └── preset_manager.cpp/.h # NVS preset storage
│   └── utils/
│       ├── debug_log.h           # Debug macros
│       └── fixed_math.h          # DSP math utilities
├── include/
│   ├── config.h                  # System-wide configuration
│   ├── dsp_types.h               # Type definitions
│   └── pin_config.h              # GPIO pin assignments
├── dsp-control-app/              # Electron GUI application
│   ├── main.js                   # Electron main process
│   ├── preload.js                # IPC bridge
│   ├── index.html                # UI layout
│   ├── css/                      # Styling
│   ├── js/
│   │   ├── app.js                # Main application logic
│   │   ├── protocol.js           # UART protocol encoder/decoder
│   │   ├── serial.js             # Serial port manager
│   │   ├── eq-designer.js        # EQ curve designer
│   │   ├── eq-graph.js           # EQ visualization
│   │   └── store.js              # State management
│   └── package.json              # Dependencies
├── platformio.ini                # PlatformIO configuration
├── CMakeLists.txt                # CMake build (ESP-IDF)
└── README.md                     # This file
```

### Building

**Via PlatformIO**:
```bash
pio run                  # Build
pio run -t upload        # Upload to ESP32
pio device monitor       # Serial monitor
```

**Compilation Flags**:
```
-O3                      # Full optimization
-funroll-loops           # Loop unrolling
-fstrict-aliasing        # Strict aliasing for perf
-DNDEBUG                 # Disable debug asserts
```

### Debugging

**Debug Output**: Use `DEBUG_LOG()` macros (compile-time conditional)

```cpp
// Enable debug in include/config.h:
#define DEBUG_DSP_PIPELINE  1
#define DEBUG_UART          1

// Use in code:
DEBUG_LOG("Pipeline processed %d samples\n", count);
```

**Serial Monitor**:
```bash
pio device monitor -b 115200
```

---

## 📝 Usage Examples

### Firmware: Enable/Disable Module

```cpp
// In control_task (Core 0)
param_controller.setModuleEnabled(DSP_MODULE_EXCITER, true);
```

### GUI: Send UART Command

```javascript
// In dsp-control-app/js/protocol.js
const cmd = {
    opcode: 0x01,           // SET_PARAM
    moduleId: 7,            // DynamicEQ
    paramIndex: 2,          // Parameter 2
    value: 45.5             // New value
};
serial.send(encodeCommand(cmd));
```

### Preset: Save Current State

```cpp
// In preset_manager.cpp
preset_manager.savePreset(0);  // Save to slot 0
preset_manager.loadPreset(0);  // Load from slot 0 (auto on boot)
```

---

## 🐛 Known Issues & Limitations

| Issue | Status | Workaround |
|-------|--------|-----------|
| Module ordering is fixed | ✓ By Design | Recompile to change order |
| Maximum 10 EQ bands | ✓ Hardware Limited | Use multiple EQ stages |
| No dynamic module insertion | ✓ By Design | Enable/disable modules only |
| GPIO0 MCLK conflicts with boot mode | ✓ Known | Use pulldown resistor for stability |
| NVS wear with frequent saves | ✓ Normal | Limit preset saves to ~10k cycles |

---

## 🤝 Contributing

Contributions welcome! Areas for improvement:

- [ ] Algorithmic optimizations (SIMD, fixed-point conversion)
- [ ] Additional DSP modules (reverb, delay, chorus)
- [ ] Mobile control app (iOS/Android)
- [ ] Real-time frequency analysis display
- [ ] Spectrum analyzer in GUI
- [ ] Recording and playback features
- [ ] A/B comparison of presets
- [ ] Machine learning-based preset suggestions

**Contribution Process**:
1. Fork repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open Pull Request

---

## 📄 License

This project is licensed under the **MIT License** - see LICENSE file for details.

**Third-Party Credits**:
- DSP algorithms inspired by MVSilicon BP10xx processor
- Dynamic EQ implementation ported from Shanghai Mountain View Silicon reference design
- Biquad filter design based on RBJ audio cookbook

---

## 📚 References

- **ESP32 Documentation**: https://docs.espressif.com/projects/esp-idf
- **I2S Protocol**: https://en.wikipedia.org/wiki/I²S
- **Audio DSP**: Robert Bristow-Johnson's Audio EQ Cookbook
- **Real-time Systems**: FreeRTOS Documentation
- **Control Protocol**: UART Binary Protocol Specification (in `docs/protocol.md`)

---

## 👤 Author

**Ohminecraft** - [GitHub Profile](https://github.com/ohminecraft)

## 🔗 Repository

**GitHub**: https://github.com/ohminecraft/esp32-dsp-core

---

## 📞 Support

- **Issues**: Use GitHub Issues for bug reports and feature requests
- **Discussions**: GitHub Discussions for questions and community help
- **Documentation**: See `docs/` directory for detailed guides

---

## 🚀 Changelog

### v1.0 - Current Release
- ✅ 12 DSP modules in optimized pipeline
- ✅ Real-time dual-core processing
- ✅ Binary UART protocol control
- ✅ Electron GUI with preset management
- ✅ 96 kHz, 32-bit float processing
- ✅ NVS preset storage (8 slots)
- ✅ CPU/heap monitoring

**Recent Updates**:
- Optimized DSP core (Q31 → float32 conversion)
- Added CPU usage and heap monitoring to GUI
- Fixed preset loading and UI redraw issues
- Increased MCLK multiplier from 256 to 512
- Optimized Dynamic EQ algorithm
- Fixed gain control above 0dB

---

**Last Updated**: April 2026  
**Status**: Production Ready ✅

---

<a name="tiếng-việt"></a>

# ESP32 DSP Core - Phiên Bản Tiếng Việt

> **Ngôn ngữ**: [English](#english) | [Tiếng Việt](#tiếng-việt)

Một framework xử lý tín hiệu âm thanh (DSP) chuyên nghiệp thực thi real-time chạy trên vi điều khiển ESP32 kèm theo ứng dụng GUI điều khiển dựa trên Electron. Lấy cảm hứng từ kiến trúc bộ xử lý âm thanh MVSilicon BP10xx.

**Trạng thái**: ✅ Ổn định (Vẫn có vài Module KHÔNG được ổn định) & Sẵn sàng sản xuất | **Tần số mẫu**: 96 kHz | **Modules**: 12 | **Độ trễ**: 2.67ms

---

## 🎯 Giới Thiệu

ESP32 DSP Core là một nền tảng xử lý âm thanh tinh vi kết hợp:
- **Pipeline DSP thực thi real-time** với 12 module xử lý âm thanh chuyên biệt
- **Xử lý âm thanh chuyên nghiệp** (EQ, nén âm, kích thích, tăng bass, xử lý không gian)
- **Tối ưu hóa song nhân** để hiệu suất real-time
- **Điều khiển giao thức nhị phân** qua UART (115200 baud)
- **Ứng dụng GUI desktop** (Electron) để điều khiển tham số và quản lý preset
- **Kiến trúc bộ nhớ tĩnh** để hoạt động dự đoán được, không bị trục trặc

Phù hợp với:
- Tạo mẫu thiết bị âm thanh
- Xử lý hiệu ứng âm thanh real-time
- Nâng cao âm thanh nhúng
- Phân tích và xử lý tín hiệu âm thanh
- Thiết bị âm thanh IoT

---

## 🏗️ Kiến Trúc

### Tổng Quan Hệ Thống

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 (Song nhân)                        │
├─────────────────────────────┬───────────────────────────────┤
│  Nhân 0 (Tác vụ điều khiển) │  Nhân 1 (Tác vụ âm thanh)    │
│  ├─ Trình phân tích UART    │  ├─ Đầu vào I2S (PCM1808)   │
│  ├─ Quản lý Preset (NVS)    │  ├─ Pipeline DSP (12 module) │
│  ├─ Bộ điều khiển tham số   │  └─ Đầu ra I2S (PCM5102A)   │
│  └─ Hàng đợi lệnh           │                               │
└──────────────┬──────────────┴───────────────────────────────┘
               │
        ┌──────▼──────┐
        │   UART      │ 115200 baud
        │  Điều khiển │◄────────────────┐
        └─────────────┘                 │
                              ┌────┴──────────────┐
                              │  Ứng dụng GUI     │
                              │   Electron        │
                              └───────────────────┘
```

### Pipeline DSP (12 Modules trong Chuỗi Tín Hiệu)

```
ĐẦU VÀO (I2S: 96kHz stereo, 256 mẫu/frame)
    ↓
[1]  ┌─────────────────┐
     │  Cổng Nhiễu     │  Loại bỏ nhiễu nền dưới ngưỡng
     └────────┬────────┘
              ↓
[2]  ┌─────────────────┐
     │  Compander      │  Bộ nén/mở rộng với tỷ lệ kép
     └────────┬────────┘
              ↓
[3]  ┌─────────────────┐
     │  Kích thích     │  Thêm tần số cao kích thích
     └────────┬────────┘
              ↓
[4]  ┌─────────────────┐
     │  Bass Ảo        │  Sinh bass dưới từ điều hòa âm
     └────────┬────────┘
              ↓
[5]  ┌─────────────────┐
     │  Bass Cổ điển   │  Tăng bass cộng hưởng (EQ thấp)
     └────────┬────────┘
              ↓
[6]  ┌─────────────────┐
     │ Mở rộng Stereo  │  Nâng cao trường stereo (M/S)
     └────────┬────────┘
              ↓
[7]  ┌─────────────────┐
     │  EQ Động        │  Chuyển đổi EQ dựa trên mức tín hiệu
     └────────┬────────┘
              ↓
[8]  ┌─────────────────┐
     │  EQ1 (Chính)    │  EQ thông số - 10 dải có thể cấu hình
     └────────┬────────┘
              ↓
[9]  ┌─────────────────┐
     │  EQ2 (Tần)      │  EQ sau / Định hình âm thanh
     └────────┬────────┘
              ↓
[10] ┌─────────────────┐
     │  DRC            │  Bộ nén tầm động đa dải
     └────────┬────────┘
              ↓
[11] ┌─────────────────┐
     │  Âm lượng        │  Điều khiển âm lượng chính
     └────────┬────────┘
              ↓
[12] ┌─────────────────┐
     │  Kẹp Mềm        │  Kẹp mềm bảo vệ loa
     └────────┬────────┘
              ↓
ĐẦU RA (I2S: PCM5102A DAC, 96kHz stereo)
```

**Ngân sách xử lý**: 2.67ms mỗi frame

---

## 📦 Yêu Cầu Phần Cứng

### Vi Điều Khiển
- **ESP32-DevKit** (Xtensa LX6 song nhân @ 240 MHz)
  - 16KB SRAM mỗi tác vụ âm thanh
  - Kiến trúc bộ nhớ tĩnh (không cấp phát động)
  - FreeRTOS với ưu tiên real-time

### Đầu Vào/Ra Âm Thanh
- **PCM1808** - ADC Stereo (đầu vào)
  - Chế độ I2S slave trên cổng 1
  - MCLK trên GPIO0
  - Đầu vào mức dòng stereo
  
- **PCM5102A** - DAC Stereo (đầu ra)
  - Chế độ I2S slave trên cổng 0
  - Đầu ra mức dòng stereo

### Cấu Hình Chân

```
Đầu ra I2S (tới PCM5102A DAC):
  GPIO22 ─ DATA (DIN)
  GPIO26 ─ BCK (xung bit)
  GPIO25 ─ WS (chọn từ)

Đầu vào I2S (từ PCM1808 ADC):
  GPIO35 ─ DATA (DOUT)
  GPIO26 ─ BCK (chung)
  GPIO25 ─ WS (chung)
  GPIO0  ─ MCLK (xung nhân)

Giao diện điều khiển:
  GPIO16 ─ UART2 RX (từ GUI)
  GPIO17 ─ UART2 TX (tới GUI)
```

---

## 🚀 Bắt Đầu Nhanh

### 1. Thiết Lập Phần Cứng

1. Kết nối PCM1808 ADC tới cổng I2S 1 của ESP32
2. Kết nối PCM5102A DAC tới cổng I2S 0 của ESP32
3. Chia sẻ các dòng BCK/WS giữa ADC và DAC
4. Kết nối GPIO0 tới MCLK (với pulldown phù hợp cho boot)
5. Kết nối UART2 (GPIO16/17) tới bộ chuyển đổi USB-UART

### 2. Cài Đặt Firmware

**Điều kiện tiên quyết**: PlatformIO (tiện ích VS Code hoặc CLI)

```bash
# Clone kho lưu trữ
git clone https://github.com/yourname/esp32-dsp-core.git
cd esp32-dsp-core

# Xây dựng và tải lên
pio run -t upload

# Giám sát đầu ra nối tiếp
pio device monitor -b 115200
```

### 3. Thiết Lập Ứng Dụng Điều Khiển

```bash
cd dsp-control-app

# Cài đặt phụ thuộc
npm install

# Chạy phiên bản phát triển
npm start

# Xây dựng ứng dụng độc lập
npm run make
```

---

## 🎚️ 12 Module DSP

| # | Module | Tham số | Ổn định | Công dụng |
|---|--------|--------|--------|----------|
| 1 | **Cổng Nhiễu** | Ngưỡng, Attack, Release | Chưa ổn định | Loại bỏ hiss/nhiễu nền |
| 2 | **Compander** | Ngưỡng, Tỷ lệ↑, Tỷ lệ↓ | Chưa ổn định | Nén/mở rộng động |
| 3 | **Kích thích** | Đạt được, Độ bão hòa | Chưa ổn định+ | Thêm sắc nét và hiện diện |
| 4 | **Bass Ảo** | Đạt được, Tần số | Chưa ổn định+ | Sinh bass ma từ điều hòa |
| 5 | **Bass Cổ điển** | Đạt được, Tần số trung tâm | Ổn định | Tăng bass ấm (low-shelf) |
| 6 | **Mở rộng Stereo** | Đạt được, Độ rộng | Ổn định | Nâng cao trường stereo (M/S) |
| 7 | **EQ Động** | Cài đặt EQ thấp/cao | Ổn định | Chuyển đổi 2 EQ theo mức tín hiệu |
| 8 | **EQ1** | 10 dải (Tần, Đạt, Q) | Tùy cấu hình | EQ thông số chính |
| 9 | **EQ2** | 10 dải (Tần, Đạt, Q) | Tùy cấu hình | EQ hậu xử lý / định hình âm |
| 10 | **DRC** | Tới 4 dải, Đạt, Tỷ lệ | Chưa ổn định | Nén/hạn chế đa dải |
| 11 | **Âm lượng** | Đạt được, Thời gian ramp | Ổn định | Điều khiển âm lượng chính |
| 12 | **Kẹp Mềm** | Ngưỡng, Knee | Ổn định | Bảo vệ loa, thêm ấm |

---

## 🔌 Giao Thức Điều Khiển

### Định Dạng Frame UART (Nhị Phân)

```
[SYNC: 0xAA55] [CMD] [MODULE_ID] [LEN: 2B] [DATA: N bytes] [CRC8]
```

**Tốc độ baud**: 115200  
**Kiểm tra lỗi**: CRC8

### Các Loại Lệnh

| Lệnh | Mã | Hướng | Mục đích |
|------|----|----|---------|
| SET_PARAM | 0x01 | PC→ESP | Đặt tham số module |
| GET_PARAM | 0x02 | ESP→PC | Truy vấn trạng thái module |
| ENABLE_MODULE | 0x03 | PC→ESP | Bật module |
| DISABLE_MODULE | 0x04 | PC→ESP | Tắt module |
| SET_EQ_BAND | 0x05 | PC→ESP | Cấu hình dải EQ |
| SAVE_PRESET | 0x06 | PC→ESP | Lưu preset vào NVS |
| LOAD_PRESET | 0x07 | PC→ESP | Tải preset từ NVS |
| GET_STATUS | 0x08 | ESP→PC | Truy vấn trạng thái hệ thống (CPU, heap) |
| RESET | 0x0F | PC→ESP | Đặt lại pipeline DSP |

---

## 💾 Quản Lý Preset

- **Lưu trữ**: NVS (Non-Volatile Storage) - 8 slot preset
- **Tự động tải**: Slot preset 0 tải tự động khi khởi động ESP32
- **Nội dung**: Trạng thái DSP đầy đủ (các module bật tắt, tham số, đường cong EQ)
- **Điều khiển**: Lưu/tải qua GUI hoặc lệnh UART

---

## ⚙️ Thông Số Kỹ Thuật Âm Thanh

| Tham số | Giá trị |
|--------|--------|
| **Tần số mẫu** | 96 kHz |
| **Kênh** | 2 (Stereo) |
| **Độ sâu bit** | 32-bit Float |
| **Kích thước Frame** | 256 mẫu (2.67ms độ trễ) |
| **Định dạng** | Xen kẽ (L, R, L, R, ...) |
| **Cổng I2S 0** | Đầu ra (DAC) |
| **Cổng I2S 1** | Đầu vào (ADC) |

---

## 📊 Các Chỉ Số Hiệu Suất

| Chỉ số | Giá trị |
|--------|--------|
| **Ưu tiên tác vụ âm thanh** | 23/25 (thực thi real-time) |
| **Thời gian xử lý** | ~1.5-2.0ms mỗi frame |
| **Dự phòng** | 0.67-1.17ms dự phòng mỗi 2.67ms frame |
| **Sử dụng RAM** | ~20KB tĩnh (không heap) |
| **Jitter** | < 100µs (xác định) |

---

## 🔧 Phát Triển

### Cấu Trúc Dự Án

```
esp32-dsp-core/
├── src/
│   ├── main.cpp                 # Điểm vào, tác vụ FreeRTOS
│   ├── audio/
│   │   ├── audio_input.cpp/.h   # Trình xử lý I2S ADC
│   │   └── audio_output.cpp/.h  # Trình xử lý I2S DAC
│   ├── dsp/
│   │   ├── dsp_pipeline.cpp/.h  # Trình điều phối module
│   │   ├── dsp_module.h         # Lớp cơ sở cho tất cả module
│   │   ├── noise_gate.cpp/.h    # [1] Cổng nhiễu
│   │   ├── compander.cpp/.h     # [2] Compander
│   │   ├── exciter.cpp/.h       # [3] Kích thích
│   │   ├── virtual_bass.cpp/.h  # [4] Bass ảo
│   │   ├── bass_classic.cpp/.h  # [5] Bass cổ điển
│   │   ├── stereo_widener.cpp/.h # [6] Mở rộng Stereo
│   │   ├── eq.cpp/.h            # [8,9] EQ thông số
│   │   ├── dynamic_eq.cpp/.h    # [7] EQ động
│   │   ├── drc.cpp/.h           # [10] DRC đa dải
│   │   ├── volume.cpp/.h        # [11] Âm lượng chính
│   │   ├── soft_clip.cpp/.h     # [12] Kẹp mềm
│   │   └── biquad.cpp/.h        # Bộ lọc Biquad cơ sở
│   ├── control/
│   │   ├── uart_protocol.cpp/.h # Trình phân tích lệnh UART
│   │   ├── param_controller.cpp/.h # Bộ điều phối tham số
│   │   └── preset_manager.cpp/.h # Lưu trữ preset NVS
│   └── utils/
│       ├── debug_log.h          # Macro gỡ lỗi
│       └── fixed_math.h         # Tiện ích toán học DSP
├── include/
│   ├── config.h                 # Cấu hình toàn hệ thống
│   ├── dsp_types.h              # Định nghĩa kiểu
│   └── pin_config.h             # Gán chân GPIO
├── dsp-control-app/             # Ứng dụng GUI Electron
└── platformio.ini               # Cấu hình PlatformIO
```

### Xây Dựng

**Qua PlatformIO**:
```bash
pio run                  # Xây dựng
pio run -t upload        # Tải lên ESP32
pio device monitor       # Giám sát nối tiếp
```

---

## 📝 Ví Dụ Sử Dụng

### Firmware: Bật/Tắt Module

```cpp
// Trong control_task (Nhân 0)
param_controller.setModuleEnabled(DSP_MODULE_EXCITER, true);
```

### GUI: Gửi Lệnh UART

```javascript
// Trong dsp-control-app/js/protocol.js
const cmd = {
    opcode: 0x01,           // SET_PARAM
    moduleId: 7,            // DynamicEQ
    paramIndex: 2,          // Tham số 2
    value: 45.5             // Giá trị mới
};
serial.send(encodeCommand(cmd));
```

---

## 🐛 Vấn Đề Đã Biết & Hạn Chế

| Vấn đề | Trạng thái | Giải pháp |
|--------|-----------|----------|
| Thứ tự module được cố định | ✓ Thiết kế | Biên dịch lại để thay đổi |
| Tối đa 10 dải EQ | ✓ Giới hạn | Sử dụng nhiều giai đoạn EQ |
| Không chèn module động | ✓ Thiết kế | Chỉ bật/tắt module |
| GPIO0 MCLK xung đột boot | ✓ Đã biết | Sử dụng điện trở pulldown |

---

## 🤝 Đóng Góp

Hoan nghênh những đóng góp! Các lĩnh vực cải thiện:

- [ ] Tối ưu hóa thuật toán (SIMD, chuyển đổi điểm cố định)
- [ ] Các module DSP bổ sung (reverb, delay, chorus)
- [ ] Ứng dụng điều khiển di động (iOS/Android)
- [ ] Hiển thị phân tích tần số real-time
- [ ] Bộ phân tích phổ trong GUI
- [ ] Tính năng ghi và phát lại
- [ ] So sánh preset A/B
- [ ] Gợi ý preset dựa trên ML

---

## 📄 Giấy Phép

Dự án này được cấp phép theo **Giấy phép MIT**.

**Tín dụng bên thứ ba**:
- Các thuật toán DSP lấy cảm hứng từ bộ xử lý MVSilicon BP10xx
- Hiện thực EQ động được chuyển từ thiết kế tham chiếu MVSilicon
- Thiết kế bộ lọc Biquad dựa trên sách nấu nước audio RBJ

---

## 📚 Tài Liệu Tham Khảo

- **Tài liệu ESP32**: https://docs.espressif.com/projects/esp-idf
- **Giao thức I2S**: https://en.wikipedia.org/wiki/I²S
- **Audio DSP**: Robert Bristow-Johnson's Audio EQ Cookbook
- **Hệ thống Thời gian thực**: Tài liệu FreeRTOS

---

## 👤 Tác Giả

**Ohminecraft** - [Hồ sơ GitHub](https://github.com/ohminecraft)

## 🔗 Kho Lưu Trữ

**GitHub**: https://github.com/ohminecraft/esp32-dsp-core

---

## 📞 Hỗ Trợ

- **Vấn đề**: Sử dụng GitHub Issues để báo cáo lỗi và yêu cầu tính năng
- **Thảo luận**: GitHub Discussions để đặt câu hỏi và tìm kiếm trợ giúp cộng đồng
- **Tài liệu**: Xem thư mục `docs/` để có hướng dẫn chi tiết

---

## 🚀 Nhật ký Thay Đổi

### v1.0 - Bản Phát Hành Hiện Tại
- ✅ 12 module DSP trong pipeline tối ưu
- ✅ Xử lý song nhân real-time
- ✅ Điều khiển giao thức UART nhị phân
- ✅ GUI Electron với quản lý preset
- ✅ Xử lý 96 kHz, 32-bit float
- ✅ Lưu trữ preset NVS (8 slot)
- ✅ Giám sát CPU/heap

**Cập nhật gần đây**:
- Tối ưu hóa DSP core (Q31 → float32)
- Thêm giám sát sử dụng CPU và heap vào GUI
- Sửa vấn đề tải preset và làm mới UI
- Tăng bộ nhân MCLK từ 256 lên 512
- Tối ưu hóa thuật toán Dynamic EQ
- Sửa điều khiển đạt được trên 0dB

---

**Cập nhật lần cuối**: Tháng 4 năm 2026  
**Trạng thái**: Sẵn sàng sản xuất ✅
