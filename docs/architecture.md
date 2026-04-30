# System Architecture

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## 🏗️ System Architecture (English)

The ESP32 DSP Core is designed as a **deterministic real-time system**. It leverages the ESP32's dual-core architecture to decouple high-priority audio processing from asynchronous communication tasks.

### 1. High-Level Block Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                       ESP32 (Dual-Core)                     │
├─────────────────────────────┬───────────────────────────────┤
│  Core 0 (System & Control)  │  Core 1 (Real-time Audio)     │
│                             │                               │
│  Priority: 5 (Low)          │  Priority: 24 (CRITICAL)      │
│                             │                               │
│  ┌─────────────────┐        │  ┌──────────────────────────┐ │
│  │ WebServer / WS  │        │  │ I2S Port 1 (Input)       │ │
│  │ (AsyncTCP)      │        │  │ 96kHz / 32-bit Float     │ │
│  └────────┬────────┘        │  └──────────┬───────────────┘ │
│           ↓                 │             ↓                 │
│  ┌─────────────────┐        │  ┌──────────────────────────┐ │
│  │ UART Protocol   │        │  │ DSP Signal Chain         │ │
│  │ (Binary Parser) │        │  │ (9 Modular Blocks)       │ │
│  └────────┬────────┘        │  └──────────┬───────────────┘ │
│           ↓                 │             ↓                 │
│  ┌─────────────────┐        │  ┌──────────────────────────┐ │
│  │ NVS Preset Mgr  │◄───────┼─►│ I2S Port 0 (Output)      │ │
│  │ (8 Slots)       │        │  │ PCM5102A Master          │ │
│  └─────────────────┘        │  └──────────────────────────┘ │
└──────────────┬──────────────┴───────────────────────────────┘
               │
        ┌──────┴──────┐          ┌──────────────────────────┐
        │   Dual      │          │   Control Application    │
        │   Transport │◄────────►│   (Browser / Electron)   │
        └─────────────┘          └──────────────────────────┘
```

### 2. Signal Chain Logic
The DSP pipeline operates in **Core 1** with a strict frame-based approach (256 samples per frame).

`INPUT → Compander → Exciter → Dynamic Bass → Dynamic EQ → EQ1 → EQ2 → DRC → Volume → OUTPUT`

- **In-place Processing**: To save RAM and reduce latency, each module modifies the global audio buffer directly.
- **Zero-Heap Policy**: No memory is allocated or freed during audio processing to prevent non-deterministic behavior (glitches).

### 3. Control & Sync Mechanism
- **Hybrid Control**: The system accepts commands via UART (USB) and WebSocket (WiFi) simultaneously.
- **Atomic Sync (Batching)**: When the UI connects, the ESP32 dumps its entire internal state (all parameters) in a single high-speed binary batch.
- **Heartbeat**: A periodic "Alive" signal ensures the WebSocket connection remains active even during idle periods.

---

<a name="tiếng-việt"></a>

## 🏗️ Kiến Trúc Hệ Thống (Tiếng Việt)

### 1. Phân bổ tài nguyên Song nhân (Dual-Core)
Hệ thống tận dụng tối đa kiến trúc 2 nhân của ESP32 để đảm bảo âm thanh không bao giờ bị giật (glitch):

- **Core 1 (Audio Core)**: Chạy tác vụ xử lý âm thanh với độ ưu tiên cao nhất (Priority 24). Tác vụ này "độc chiếm" CPU để tính toán các thuật toán EQ, Bass, Compression ở tần số 96kHz.
- **Core 0 (System Core)**: Xử lý các tác vụ "chậm" và không đồng bộ như: Nhận lệnh từ WiFi/WebSocket, Phân tích giao thức UART, Lưu trữ Preset vào bộ nhớ Flash (NVS).

### 2. Giao thức Điều khiển Kép
Dự án hỗ trợ chuyển đổi mượt mà giữa 2 phương thức:
- **Serial (USB)**: Dành cho cân chỉnh chuyên sâu tại bàn kỹ thuật.
- **WebSocket (WiFi)**: Dành cho điều khiển không dây qua điện thoại hoặc máy tính từ xa.

### 3. Đặc tính Kỹ thuật Audio
- **Tần số mẫu**: 96.000 Hz.
- **Độ trễ hệ thống**: 2.67 ms (Cực thấp, đáp ứng tiêu chuẩn biểu diễn chuyên nghiệp).
- **Bộ nhớ tĩnh**: 100% bộ đệm âm thanh được cấp phát tĩnh ngay khi khởi động, tránh lỗi phân mảnh RAM (Heap fragmentation).

---
**Last Updated**: April 30, 2026
