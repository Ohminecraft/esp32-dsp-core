# Parameter Tuning Guide - ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Parameter Tuning Reference (English)

Complete reference for all parameters across 12 DSP modules.

---

### [1] Noise Gate

**Purpose**: Silence audio below threshold (removes background noise)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -60 to 0 | -40 | dB | Gate opens above this level |
| Attack Time | 0.5 to 100 | 10 | ms | How fast gate opens |
| Release Time | 10 to 1000 | 100 | ms | How fast gate closes |
| Hold Time | 0 to 500 | 50 | ms | Gate stays open this long |

---

### [2] Compander

**Purpose**: Dynamic compression/expansion with separate ratios above/below threshold

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -60 to 0 | -20 | dB | Split point for compression/expansion |
| Ratio Above | 1 to 10 | 1 | :1 | Compression ratio above threshold |
| Ratio Below | 0.5 to 10 | 1 | :1 | Expansion ratio below threshold |
| Attack Time | 1 to 2000 | 10 | ms | Compression attack speed |
| Release Time | 10 to 2000 | 100 | ms | Compression release speed |
| Makeup Gain (WIP) | -12 to 12 | 0 | dB | Compensate for compression |

---

### [3] Exciter

**Purpose**: Add high-frequency harmonic content (brightness, air, sparkle)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Cutoff Frequency | 300 to 10000 | 3000 | hz | Cutoff HighShelf for Processing |
| Dry | 0 to 100 | 0 | % | Mid Level |
| Wet | 0 to 100 | 0 | % | High Level |

---

### [4] Virtual Bass

**Purpose**: Generate perceived sub-bass from harmonics (psychoacoustic bass extension)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Frequency | 30 to 300 | 60 | Hz | Generated bass center frequency |
| Intensity | 0 to 100 | 0 | % | Harmonic generation strength |
| Enhanced | on/off | off | bool | Enhanced Sub-bass strength |

---

### [5] Bass Classic

**Purpose**: Traditional resonant bass boost (low-shelf EQ)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Frequency | 30 to 300 | 100 | Hz | Bass center frequency |
| Intensity | 0 to 100 | 0 | % | Bass Boosted by Intensity |

---

### [6] Stereo Widener (WIP For Now)

**Purpose**: Enhance stereo width using Mid/Side (M/S) processing

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -6 to 12 | 3 | dB | Overall output level |
| Width | 0.5 to 2.0 | 1.2 | x | 1.0 = mono, 2.0 = very wide |
| HPF Frequency | 100 to 5000 | 200 | Hz | High-pass on side channel |

---

### [7] Dynamic EQ

**Purpose**: Switch between two EQ curves based on signal level (quiet = different tone than loud)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Low Threshold Level | -60 to 0 | -40 | dB | Level threshold to switch EQ LOW |
| Normal Threshold Level | -60 to 0 | -20 | dB | Level threshold to back normal (No EQs using) |
| High Threshold Level | -60 to 0 | -6 | dB | Level threshold to switch EQ HIGH |
| Attack Time | 1 to 2000 | 10 | ms | EQ curve switch speed |
| Release Time | 10 to 2000 | 100 | ms | EQ curve back to normal |

Dynamic EQ LOW & Dynamic EQ HIGH: see [8]

**Tips**
Low Threshold = Normal Threshold: no "no-processing" gap at bottom → direct LOW↔HIGH
High Threshold = Normal Threshold: no "no-processing" gap at top → direct LOW↔HIGH  

---

### [8] EQ1 (Main Parametric EQ)

**Purpose**: Primary 10-band parametric equalizer

| Per Band | Range | Default | Unit | Notes |
|----------|-------|---------|------|-------|
| Frequency | 20 to 20000 | [varies] | Hz | Band center frequency |
| Gain | -12 to 12 | 0 | dB | Boost/cut amount |
| Q | 0.1 to 10.0 | 1.0 | - | Bandwidth (0.1=wide, 10=narrow) |

**Max Bands**: 10 simultaneously

---

### [9] EQ2 (Post EQ / Tone Shaping)

**Purpose**: Secondary 10-band EQ for sound signature shaping

Same parameters as EQ1. Typically used for:
- Fine-tuning after EQ1
- Tone shaping per source (radio, podcast, etc.)
- Compensation for room acoustics

---

### [10] DRC (Dynamic Range Compressor) (WIP For now)

**Purpose**: Multi-band dynamic range compression/limiting (protects against clipping)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Num Bands | 1 to 4 | 1 | - | Number of frequency bands |
| Band [1-4] Frequency | 100 to 10000 | [varies] | Hz | Band split frequency |
| Band [1-4] Threshold | -60 to 0 | -20 | dB | Compression starts above this |
| Band [1-4] Ratio | 1 to 20 | 4 | :1 | Compression ratio |
| Band [1-4] Attack | 1 to 100 | 5 | ms | Attack speed |
| Band [1-4] Release | 10 to 1000 | 50 | ms | Release speed |
| Makeup Gain | -12 to 12 | 0 | dB | Compensate for compression |

---

### [11] Volume

**Purpose**: Master volume control with smooth gain ramping (prevents clicks/pops)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -60 to 18 | 0 | dB | Output volume level |

---

### [12] Soft Clipper

**Purpose**: Soft clipping limiter (prevents speaker damage, adds harmonic warmth)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -60 to 0 | 0 | dB | Clipping starts above this |


---

---

<a name="tiếng-việt"></a>

## Hướng Dẫn Chỉnh Tham Số (Tiếng Việt)

Tham chiếu hoàn chỉnh cho tất cả tham số trên 12 module DSP.

---

### [1] Cổng Nhiễu

**Mục đích**: Làm câm âm thanh dưới ngưỡng (loại bỏ nhiễu nền)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Ngưỡng | -80 đến 0 | -40 | dB | Cổng mở ở mức này |
| Thời Gian Attack | 0.5 đến 100 | 10 | ms | Tốc độ cổng mở |
| Thời Gian Release | 10 đến 1000 | 100 | ms | Tốc độ cổng đóng |

**Mẹo Chỉnh Tham Số**:
```
Phòng yên tĩnh:      Ngưỡng = -50dB, Attack = 5ms, Release = 200ms
Biểu diễn trực tiếp: Ngưỡng = -35dB, Attack = 1ms, Release = 50ms
Studio vocals:       Ngưỡng = -45dB, Attack = 10ms, Release = 100ms
```

---

### [2] Compander

**Mục đích**: Nén/mở rộng động với tỷ lệ riêng biệt

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị |
|---------|---------|----------|--------|
| Ngưỡng | -60 đến 0 | -20 | dB |
| Tỷ Lệ Trên | 1 đến 20 | 4 | :1 |
| Tỷ Lệ Dưới | 0.5 đến 1.5 | 0.7 | :1 |
| Thời Gian Attack | 1 đến 100 | 10 | ms |
| Thời Gian Release | 10 đến 500 | 100 | ms |

---

### [3] Kích Thích

**Mục đích**: Thêm nội dung điều hòa tần số cao (sáng, không khí, lấp lánh)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị |
|---------|---------|----------|--------|
| Đạt Được | -12 đến 12 | 3 | dB |
| Độ Bão Hòa | 0 đến 100 | 30 | % |
| Tần Số HPF | 500 đến 15000 | 5000 | Hz |

**Ứng Dụng**:
```
Vocal sáng: Đạt = 5dB, Bão Hòa = 40%, HPF = 3kHz
Drum lấp lánh: Đạt = 3dB, Bão Hòa = 50%, HPF = 8kHz
```

---

### [4] Bass Ảo

**Mục đích**: Sinh bass dưới từ điều hòa (mở rộng bass tâm lý)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị |
|---------|---------|----------|--------|
| Đạt Được | 0 đến 12 | 5 | dB |
| Tần Số | 40 đến 200 | 80 | Hz |
| Cường Độ | 0 đến 100 | 50 | % |

---

### [5] Bass Cổ Điển

**Mục đích**: Tăng bass cộng hưởng truyền thống

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị |
|---------|---------|----------|--------|
| Đạt Được | -12 đến 12 | 5 | dB |
| Tần Số | 20 đến 200 | 60 | Hz |
| Q | 0.5 đến 2.0 | 0.7 | - |

**Ứng Dụng**:
```
Deep thump: Tần Số = 40Hz, Đạt = 8dB
Tight punch: Tần Số = 80Hz, Đạt = 6dB
Ấm áp: Tần Số = 100Hz, Đạt = 3dB
```

---

### [6-12] EQ và Các Module Khác

Tương tự tiếng Anh ở trên, với dịch các tham số thành tiếng Việt.

**EQ1 (EQ Chính)**: 10 dải thông số
- Tần Số: 20 - 20000 Hz
- Đạt Được: -12 đến 12 dB
- Q: 0.1 đến 10.0

**DRC (Bộ Nén)**: Nén động đa dải (1-4 dải)
**Âm Lượng**: Điều khiển âm lượng chính
**Kẹp Mềm**: Kẹp mềm bảo vệ loa

---

### Mẫu Tham Số Nhanh

#### Mẫu 1: "Tăng Sắc Nét"
```
Cổng Nhiễu: -45dB
Kích Thích: +5dB, 4kHz HPF
EQ1: +3dB @ 2kHz
Âm Lượng: -6dB
```

#### Mẫu 2: "Tăng Bass"
```
Bass Ảo: +6dB, 80Hz
Bass Cổ Điển: +5dB, 60Hz
Mở Rộng Stereo: 1.2x
Âm Lượng: -3dB
```

#### Mẫu 3: "Phát Sóng Ấm Áp"
```
Compander: -15dB, 3:1 ratio
Bass Cổ Điển: +3dB, 50Hz
EQ1: +2dB @ 500Hz
Kẹp Mềm: 0dB threshold
```
