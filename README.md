<h1 align="center">ESP32 DSP Core</h1>

<p align="center">
  High Performance DSP Engine for ESP32
</p>

<p align="center">
  <img src="https://img.shields.io/badge/-Arduino-00979D?style=for-the-badge&logo=Arduino&logoColor=white">

  <img src="https://img.shields.io/badge/espressif-E7352C.svg?style=for-the-badge&logo=espressif&logoColor=white">

  <img src="https://img.shields.io/badge/PlatformIO-Compatible-orange?style=for-the-badge&logo=platformio&logoColor=white">
</p>

<p align="center">
  <img src="https://img.shields.io/github/license/Ohminecraft/esp32-dsp-core?style=for-the-badge">

  <img src="https://img.shields.io/github/stars/Ohminecraft/esp32-dsp-core?style=for-the-badge">
</p>

<p align="center">
  <a href="https://github.com/Ohminecraft/esp32-dsp-core/actions/workflows/build.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/Ohminecraft/esp32-dsp-core/build.yml?style=for-the-badge&label=Build%20Passing">
  </a>

  <a href="https://github.com/Ohminecraft/esp32-dsp-core/releases">
    <img src="https://img.shields.io/github/v/release/Ohminecraft/esp32-dsp-core?style=for-the-badge&label=Build%20Release">
  </a>
</p>

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

A high-performance real-time audio digital signal processing (DSP) framework for ESP32/ESP32-S3. This project provides a modular signal chain, professional-grade audio algorithms, and a dual-transport control interface (USB Serial & WiFi WebSocket).

**Max Sample Rate Support**: 96 kHz | **Latency**: 2.67ms | **Transport**: UART + WebSocket

---

<a name="english"></a>

## 🎯 Overview

ESP32 DSP Core is a professional audio platform featuring:
- **Dual-Core Processing**: Core 1 handles the 96kHz audio pipeline; Core 0 handles system control and networking.
- **Advanced DSP Pipeline**: 11+ modules including Pre Gain, Compander, Exciter, Dynamic Bass, Dynamic EQ, Dual Parametric EQ, Left/Right Independent EQ, Multi-band DRC, and Post Gain.
- **Hybrid Connectivity**: Seamlessly switch between **High-speed UART** (USB) and **Low-latency WebSocket** (WiFi).
- **Sub-500ms Sync**: Proprietary frame batching protocol for near-instant state synchronization over WiFi.
- **Auto Clock Sync**: Built-in PCNT-based frequency monitor for real-time sample rate tracking and I2S re-initialization.
- **Static Memory Architecture**: Zero heap allocations in the audio path for rock-solid stability. Hardened math utilities with NaN/Inf protection.

---

## 🏗️ Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                       ESP32 (Dual-Core)                     │
├─────────────────────────────┬───────────────────────────────┤
│  Core 0 (Control & Web)     │  Core 1 (Audio DSP)           │
│  ├─ WebSocket Server        │  ├─ I2S Input (96kHz)         │
│  ├─ UART Protocol Handler   │  ├─ DSP Pipeline (12 modules) │
│  ├─ Clock Monitor (PCNT)    │  ├─ Sample Rate Auto-Sync     │
│  └─ NVS Preset Manager      │  └─ I2S Output (Master)       │
└──────────────┬──────────────┴───────────────────────────────┘
               │
        ┌──────┴──────┐          ┌──────────────────────────┐
        │   WiFi      │          │   Control Application    │
        │   WebSocket │◄────────►│   (Browser / Electron)   │
        └─────────────┘          └──────────────────────────┘
               ▲                          ▲
               └────────── UART ──────────┘
```

### Signal Chain
`INPUT → Pre Gain → Compander → Exciter → Dynamic Bass → Dynamic EQ → ISF EQ 1 → ISF EQ 2 → EQ1 → EQ2 → Left/Right EQ → Multi-band DRC → Post Gain → OUTPUT`

---

## 🚀 Key Features

### ⚡ High-Speed Synchronization
Unlike traditional protocols, ESP32 DSP Core uses **Frame Batching**. Instead of sending 150+ individual packets, the system bundles the entire DSP state into a single 2KB static buffer.
- **Serial Sync**: ~100ms
- **WiFi Sync**: <500ms (even on crowded networks)

### 📱 Mobile-Ready Web UI
Serve the control interface directly from the ESP32 SPIFFS.
- **Auto-Reconnect**: Handles mobile sleep/wake cycles gracefully.
- **Responsive Design**: Modern glassmorphism UI optimized for touch.

---

## ⚙️ Audio Specifications

| Parameter | Value |
|-----------|-------|
| **Sample Rate** | 96 kHz (Standard) / Auto-Scaling |
| **Channels** | 2 (Stereo) |
| **Bit Depth** | 32-bit Float Processing |
| **Latency** | 2.67ms (512 samples (for ESP32-S3), 256 samples (for ESP32)) @ 96kHz) |
| **Hardware** | ESP32-S3 (N16R8 recommended) / PCM1808 ADC or Bluetooth like QCC5125 / PCM5102A DAC |

---

<a name="tiếng-việt"></a>

## Tiếng Việt

ESP32 DSP Core là một framework xử lý âm thanh kỹ thuật số (DSP) hiệu năng cao, chạy real-time trên dòng chip ESP32/ESP32-S3. Dự án cung cấp chuỗi xử lý modular, các thuật toán âm thanh chuyên nghiệp và giao diện điều khiển kép (USB Serial & WiFi WebSocket).

**Tần số mẫu tối đa**: 96 kHz (Tự động đồng bộ) | **Độ trễ**: 2.67ms | **Kết nối**: UART + WebSocket

### 🎯 Điểm nổi bật
- **Xử lý Song nhân**: Core 1 dành riêng cho audio; Core 0 xử lý kết nối và hệ thống.
- **Đồng bộ siêu tốc**: Giao thức **Frame Batching** giúp đồng bộ toàn bộ trạng thái DSP qua WiFi trong chưa đầy 0.5 giây.
- **Smart Workflow**: Script tự động hóa PlatformIO giúp đồng bộ file Web UI và chỉ nạp SPIFFS khi có thay đổi.
- **Auto Clock Sync**: Tự động nhận diện và điều chỉnh I2S theo tần số mẫu thực tế từ phần cứng (ví dụ khi Bluetooth đổi Sample Rate).
- **Giao diện hiện đại**: UI Glassmorphism, tối ưu cho cả máy tính (Electron) và điện thoại (Browser).

### 🏗️ Chuỗi xử lý (Signal Chain)
`INPUT → Pre Gain → Compander → Exciter → Dynamic Bass → Dynamic EQ → EQ1 → EQ2 → Left/Right EQ → Multi-band DRC → Post Gain → OUTPUT`

---

**Last Updated**: May 5, 2026  
**Status**: In-Experimental ✅
