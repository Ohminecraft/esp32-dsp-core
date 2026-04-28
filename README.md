# ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

A professional real-time audio digital signal processing (DSP) framework running on ESP32 microcontroller with an Electron-based GUI control application. Inspired by the MVSilicon BP10xx audio processor architecture.

**Status**: ✅ Stable and Production-Ready | **Sample Rate**: 96 kHz | **Modules**: 9 | **Latency**: 2.67ms

---

<a name="english"></a>

## 🎯 Overview

ESP32 DSP Core is a sophisticated audio processing platform that combines:
- **Real-time DSP pipeline** with 9 specialized audio modules
- **Professional audio processing** (EQ, compression, excitation, dynamic bass, spatial processing)
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
│  ├─ Preset Manager (NVS)    │  ├─ DSP Pipeline (9 modules) │
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

### DSP Pipeline (9 Modules in Signal Chain)

```
INPUT (I2S: 96kHz stereo, 256 samples/frame)
    ↓
[1]  ┌──────────────────┐
     │  Compander       │  Compressor/Expander with dual ratios
     └────────┬─────────┘
              ↓
[2]  ┌──────────────────┐
     │  Exciter         │  Adds high-frequency harmonic sparkle
     └────────┬─────────┘
              ↓
[3]  ┌──────────────────┐
     │  Dynamic Bass    │  3-zone adaptive bass with clip protection
     └────────┬─────────┘
              ↓
[4]  ┌──────────────────┐
     │  Stereo Widener  │  M/S stereo enhancement
     └────────┬─────────┘
              ↓
[5]  ┌──────────────────┐
     │  Dynamic EQ      │  Level-dependent dual-EQ system
     └────────┬─────────┘
              ↓
[6]  ┌──────────────────┐
     │  EQ1 (Main)      │  Parametric EQ - 10 bands
     └────────┬─────────┘
              ↓
[7]  ┌──────────────────┐
     │  EQ2 (Tone)      │  Post-EQ / Sound Signature
     └────────┬─────────┘
              ↓
[8]  ┌──────────────────┐
     │  DRC             │  Multi-band Dynamic Range Compressor
     └────────┬─────────┘
              ↓
[9]  ┌──────────────────┐
     │  Volume          │  Master Volume with smooth ramping
     └────────┬─────────┘
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
  - Static memory architecture
  - FreeRTOS with real-time priorities

### Audio I/O
- **PCM1808** - Stereo ADC (input)
  - I2S slave mode on port 1 (I2S_NUM_1)
  - MCLK on GPIO0
  
- **PCM5102A** - Stereo DAC (output)
  - I2S slave mode on port 0 (I2S_NUM_0)

---

## 🎚️ DSP Modules (9 Total)

| # | Module | Stability | Purpose |
|---|--------|-----------|---------|
| 1 | **Compander** | Unstable | Dynamic compression/expansion |
| 2 | **Exciter** | Stable | Add clarity and presence |
| 3 | **Dynamic Bass** | Stable | 3-zone adaptive bass extension |
| 4 | **Stereo Widener** | Stable | Enhance stereo field (M/S) |
| 5 | **Dynamic EQ** | Stable | Level-dependent dual-EQ |
| 6 | **EQ1** | Config-Dependent | Main parametric EQ (10-band) |
| 7 | **EQ2** | Config-Dependent | Post-EQ tone shaping (10-band) |
| 8 | **DRC** | Unstable | Multi-band compression/limiting |
| 9 | **Volume** | Stable | Master volume control |

---

## ⚙️ Audio Specifications

| Parameter | Value |
|-----------|-------|
| **Sample Rate** | 96 kHz |
| **Channels** | 2 (Stereo) |
| **Bit Depth** | 32-bit Float |
| **Frame Size** | 256 samples (2.67ms latency) |
| **Format** | Interleaved (L, R, L, R, ...) |
| **Buffer Strategy** | Static allocation, in-place processing |

---

## 📚 References

- **ESP32 Documentation**: https://docs.espressif.com/projects/esp-idf
- **I2S Protocol**: https://en.wikipedia.org/wiki/I²S
- **Audio DSP**: Robert Bristow-Johnson's Audio EQ Cookbook
- **Real-time Systems**: FreeRTOS Documentation

---

<a name="tiếng-việt"></a>

## Tiếng Việt

ESP32 DSP Core - Framework xử lý tín hiệu âm thanh chuyên nghiệp thực thi real-time trên ESP32.

**Trạng thái**: ✅ Ổn định & Sẵn sàng sản xuất | **Tần số mẫu**: 96 kHz | **Modules**: 9 | **Độ trễ**: 2.67ms

### 🎯 Giới Thiệu

ESP32 DSP Core kết hợp:
- **Pipeline DSP** với 9 module xử lý âm thanh chuyên biệt
- **Xử lý âm thanh chuyên nghiệp** (EQ, nén, kích thích, bass động, xử lý không gian)
- **Tối ưu hóa song nhân** cho hiệu suất real-time
- **Điều khiển giao thức nhị phân** qua UART (115200 baud)
- **Ứng dụng GUI desktop** (Electron) để điều khiển tham số
- **Kiến trúc bộ nhớ tĩnh** cho hoạt động dự đoán được

### 📦 Yêu Cầu Phần Cứng

- **ESP32-DevKit** (Xtensa LX6 song nhân @ 240 MHz)
- **PCM1808** - ADC Stereo (đầu vào)
- **PCM5102A** - DAC Stereo (đầu ra)

### 🎚️ 9 Module DSP

| # | Module | Ổn định | Mục Đích |
|---|--------|---------|---------|
| 1 | **Compander** | Chưa | Nén/mở rộng động |
| 2 | **Exciter** | Ổn định | Thêm sắc nét |
| 3 | **Dynamic Bass** | Ổn định | Bass động 3-zone |
| 4 | **Stereo Widener** | Ổn định | Mở rộng trường stereo |
| 5 | **Dynamic EQ** | Ổn định | EQ thích ứng theo mức |
| 6 | **EQ1** | Tùy cấu hình | EQ chính 10 dải |
| 7 | **EQ2** | Tùy cấu hình | EQ hậu xử lý |
| 8 | **DRC** | Chưa | Nén động đa dải |
| 9 | **Volume** | Ổn định | Âm lượng chính |

---

**Last Updated**: April 2026  
**Status**: Production Ready ✅
