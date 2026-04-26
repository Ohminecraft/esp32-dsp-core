# System Architecture

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## System Architecture (English)

### High-Level Block Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    PHYSICAL LAYER                           │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Audio Input (PCM1808)    Audio Output (PCM5102A)           │
│       ↓                          ↑                          │
│  ┌─────────┐  I2S Port 1    ┌──────────┐  I2S Port 0        │
│  │ ADC     │ ←──────────────│ I2S Out  │ ─────────→ DAC     │
│  │ PCM1808 │  GPIO16/17 TX  │ UART2    │ GPIO26/25 WS/BCK   │
│  └─────────┘  GPIO16 RX     │ Serial   │ GPIO22 DATA        │
│       ↓                     └──────────┘        ↓           │
│  GPIO0 MCLK                       ↓             DAC Out     │
│  GPIO35 DATA                  Serial Port       GPIO17 TX   │
│  GPIO26 BCK (shared)             to PC          │           │
│  GPIO25 WS (shared)         ┌──────────┐        │           │
│                             │   USB    │        │           │
│                             │ Adapter  │        │           │
│                             └────┬─────┘        │           │
│                                  │              │           │
└──────────────────────────────────┼──────────────┼───────────┘
                                   │              │
                   ┌───────────────┴──────────────┴────┐
                   │                                   │
            PC / Laptop                                │
            ┌─────────────────┐                        │
            │  Electron GUI   │                   (USB cable)
            │  Control App    │                        │
            │                 │◄───────────────────────┘
            │ ├─ Serial Port  │
            │ ├─ Parameter    │
            │ │  Control      │
            │ ├─ EQ Designer  │
            │ └─ Preset Mgr   │
            └─────────────────┘
```

---

### ESP32 Internal Architecture

```
┌───────────────────────────────────────────────────────────┐
│  ESP32 Dual-Core (240 MHz Xtensa LX6)                    │
├─────────────────────┬───────────────────────────────────┤
│  Core 0             │  Core 1                           │
│  (Control Task)     │  (Audio Task)                     │
│                     │                                   │
│  Priority: 5        │  Priority: 23 (CRITICAL)         │
│  Stack: 4KB         │  Stack: 16KB                     │
│                     │                                   │
│  ┌─────────────────┐│ ┌──────────────────────────┐     │
│  │ UART Handler    ││ │ I2S Driver (Port 1)      │     │
│  │ (115200 baud)   ││ │ ├─ ADC Read              │     │
│  └────────┬────────┘│ │ └─ GPIO0 MCLK Sync       │     │
│           ↓         │ └────────┬─────────────────┘     │
│  ┌─────────────────┐│          ↓                        │
│  │ Parameter       ││ ┌──────────────────────────┐     │
│  │ Controller      ││ │ DSP Pipeline             │     │
│  │ (dispatch cmds) ││ │ ├─ 12 Module Chain       │     │
│  └────────┬────────┘│ │ ├─ In-place Processing  │     │
│           ↓         │ │ └─ 256 sample buffer    │     │
│  ┌─────────────────┐│ └────────┬─────────────────┘     │
│  │ Preset Manager  ││          ↓                        │
│  │ (NVS storage)   ││ ┌──────────────────────────┐     │
│  │ (8 slots)       ││ │ I2S Driver (Port 0)      │     │
│  └─────────────────┘│ │ ├─ DAC Write             │     │
│                     │ │ └─ GPIO26/25 Sync        │     │
│  Command Queue      │ └──────────────────────────┘     │
│  (Ring Buffer)      │                                   │
│                     │  Memory Model:                    │
│  FreeRTOS Task      │  ├─ IRAM: Hot paths              │
│  Scheduler          │  ├─ DRAM: DSP buffers (static)   │
│                     │  └─ Stack: Local variables       │
└─────────────────────┴───────────────────────────────────┘
        │ Shared (mutexes, flags)
        │ ├─ enabled_flags[12]     ← Module on/off
        │ ├─ parameter_queue       ← New params
        │ └─ status_register       ← CPU%, heap
```

---

### DSP Pipeline Flow

```
Audio Frame (256 stereo samples)
     ↓
[1]  NoiseGate          ┐
[2]  Compander          │
[3]  Exciter            │  Serial
[4]  VirtualBass        │  Processing Chain 
[5]  DynamicEQ          │   (each module modifies
[6]  EQ1 (Main)         │   buffer
[7]  EQ2 (Tone)         │   in-place)
[8] DRC                │
[9] Volume             │
[10] SoftClipper        ┘
     ↓
Output Frame (256 stereo samples @ DAC)
```

**Processing Model**:
- Single float32 buffer (512 floats for stereo)
- Each module processes and modifies in-place
- No intermediate buffers (memory efficient)
- All processing completes in ~2ms (2.67ms budget)

---

### Control Flow

#### PC → ESP32 (Parameter Change)

```
GUI Slider Move
      ↓
JavaScript Event Handler
      ↓
Encode UART Frame (binary)
      ↓
Send via USB Serial
      ↓
ESP32 UART Interrupt (Core 0)
      ↓
Parse Frame → Validate CRC
      ↓
Enqueue Command in Ring Buffer
      ↓
FreeRTOS Task Wakes Control Task
      ↓
Execute Command on Core 0:
  ├─ SET_PARAM: module.setParameter(index, value)
  ├─ ENABLE_MODULE: module.enabled = true
  ├─ DISABLE_MODULE: module.enabled = false
  └─ SAVE/LOAD_PRESET: preset_manager operations
      ↓
Core 1 Audio Task Picks Up Changes on Next Frame
      ↓
Module Parameters Updated (no glitches)
```

---

### Memory Layout

```
┌─ ESP32 RAM (520 KB total) ─────────────────┐
│                                             │
│  IRAM (96 KB) - Fast access                │
│  ├─ ISRs and hot paths                     │
│  ├─ FreeRTOS kernel                        │
│  └─ Critical sections                      │
│                                             │
│  DRAM (424 KB) - Slower but sufficient     │
│  ├─ DSP Buffer (512 floats = 2KB)          │
│  ├─ Biquad States (≈ 100 floats × 35 = 1.4KB) │
│  ├─ Module Parameters (≈ 2KB)              │
│  ├─ Preset Storage (8 slots × 4KB = 32KB) │
│  ├─ Command Queue (256B)                   │
│  ├─ FreeRTOS Tasks Stack (20KB)            │
│  └─ Free Space (≈ 366 KB available)        │
│                                             │
│  NVS (Non-Volatile Storage - Flash)        │
│  └─ Preset Slots (8 × 1KB each)            │
│                                             │
└─────────────────────────────────────────────┘
```

**Key Points**:
- No dynamic heap allocation (predictable)
- All buffers pre-allocated at startup
- Stack for temporary variables only
- IRAM optimized for real-time performance

---

### Timing Analysis

```
Frame Timing (256 samples @ 96 kHz):
├─ Total Frame Time: 256 ÷ 96000 = 2.67 ms
│
├─ I2S Input: 0.5 ms (fetch 256 samples)
├─ DSP Pipeline: 2.0 ms (12 modules process in-place)
├─ I2S Output: 0.5 ms (write 256 samples)
│
└─ Spare Time: 0 ms (tight but deterministic!)

Critical: All DSP must complete before next frame interrupt
Jitter: < 100 µs (FreeRTOS priority ensures this)
```

---

### Module Interface (DSPModule Base Class)

```cpp
class DSPModule {
protected:
    bool enabled;
    const char* name;

public:
    virtual void process(float32_t* buffer, uint32_t samples) = 0;
    virtual void setParameter(uint8_t index, float value) = 0;
    virtual float getParameter(uint8_t index) const = 0;
    virtual void reset() = 0;
    
    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() const { return enabled; }
};
```

**Contract**:
- `process()`: Modify buffer in-place, 100% real-time safe
- `setParameter()`: Update state (called from Core 0, not Core 1)
- `getParameter()`: Read state for GUI display
- `reset()`: Clear internal state (filters, envelopes, etc.)

---

### Error Handling Strategy

**Philosophy**: Fail-safe design for real-time audio

```cpp
// ✓ GOOD: Clamp invalid parameters
setParameter(index, value) {
    param = fmax(MIN, fmin(MAX, value));  // Clamp
}

// ✗ BAD: Crash on invalid data
setParameter(index, value) {
    if (value < MIN || value > MAX) throw exception();
}

// ✓ GOOD: Bypass broken modules
process(...) {
    if (!enabled) return;  // Just skip
    // ...
}

// ✗ BAD: Crash on error
process(...) {
    if (buffer == nullptr) crash();
}
```

---

<a name="tiếng-việt"></a>

## Kiến Trúc Hệ Thống (Tiếng Việt)

### Sơ Đồ Khối Cấp Cao

```
┌────────────────────────────────────┐
│  Tầng Vật Lý                       │
├────────────────────────────────────┤
│ ADC (PCM1808) → I2S Port 1         │
│ I2S Port 0 → DAC (PCM5102A)        │
│ GPIO16/17 → UART2 (Serial)         │
│ PC/Laptop → USB Cable              │
└─────────────┬──────────────────────┘
              │
         ┌────▼──────┐
         │   ESP32   │
         │  Dual-Core│
         │   Core 0  │ Core 1
         │ Control   │ Audio
         └────┬──────┘
              │
         ┌────▼─────────┐
         │ Electron GUI │
         │ Application  │
         └──────────────┘
```

### Hoạt Động Nội Bộ ESP32

```
Core 0 (Điều Khiển)      Core 1 (Âm Thanh)
├─ UART Handler          ├─ I2S Input
├─ Parameter Control     ├─ DSP Pipeline (12 module)
├─ Preset Manager        └─ I2S Output
└─ Command Queue
```

**Ưu Tiên**:
- Core 0: Ưu tiên 5 (thấp)
- Core 1: Ưu tiên 23 (rất cao, thực thi real-time)

### Luồng DSP

```
Frame (256 mẫu stereo)
     ↓
[1-12] 12 Module xử lý
     ↓
Output Frame
```

Mỗi module xử lý buffer tại chỗ (in-place) → Tiết kiệm bộ nhớ

---

### Bộ Nhớ

```
IRAM (96 KB): Code nóng, ISR
DRAM (424 KB): Buffer DSP, Preset, Stack
NVS (Flash): 8 Preset slot
```

**Chính sách**: Không cấp phát động → Dự đoán được

---

### Phân Tích Thời Gian

```
Mỗi Frame: 2.67 ms
├─ I2S Input: 0.5 ms
├─ DSP: 2.0 ms (tight!)
└─ I2S Output: 0.5 ms

Jitter: < 100 µs
```

---

### Chiến Lược Xử Lý Lỗi

**Triết lý**: Thiết kế an toàn cho âm thanh real-time

```cpp
// ✓ TỐT: Giới hạn tham số
param = fmax(MIN, fmin(MAX, value));

// ✓ TỐT: Bỏ qua module tắt
if (!enabled) return;
```

Không bao giờ ném exception trong real-time audio.
