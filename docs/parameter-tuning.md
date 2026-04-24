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
| Threshold | -80 to 0 | -40 | dB | Gate opens above this level |
| Attack Time | 0.5 to 100 | 10 | ms | How fast gate opens |
| Release Time | 10 to 1000 | 100 | ms | How fast gate closes |
| Hold Time | 0 to 500 | 50 | ms | Gate stays open this long |

**Tuning Tips**:
```
Quiet room:       Threshold = -50dB, Attack = 5ms, Release = 200ms
Live performance: Threshold = -35dB, Attack = 1ms, Release = 50ms
Studio vocal:     Threshold = -45dB, Attack = 10ms, Release = 100ms
```

---

### [2] Compander

**Purpose**: Dynamic compression/expansion with separate ratios above/below threshold

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -60 to 0 | -20 | dB | Split point for compression/expansion |
| Ratio Above | 1 to 20 | 4 | :1 | Compression ratio above threshold |
| Ratio Below | 0.5 to 1.5 | 0.7 | :1 | Expansion ratio below threshold |
| Attack Time | 1 to 100 | 10 | ms | Compression attack speed |
| Release Time | 10 to 500 | 100 | ms | Compression release speed |
| Makeup Gain | -12 to 12 | 0 | dB | Compensate for compression |

**Tuning Tips**:
```
Gentle compression:  Threshold = -15dB, Ratio = 2:1, Attack = 20ms
Aggressive control:  Threshold = -10dB, Ratio = 8:1, Attack = 2ms
Upward expansion:    Ratio Below = 1.5, pushes quiet parts lower
```

---

### [3] Exciter

**Purpose**: Add high-frequency harmonic content (brightness, air, sparkle)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -12 to 12 | 3 | dB | Output level of exciter |
| Saturation | 0 to 100 | 30 | % | Harmonic distortion amount |
| HPF Frequency | 500 to 15000 | 5000 | Hz | High-pass filter cutoff |
| Mix | 0 to 100 | 50 | % | Blend: 0=off, 100=full |

**Tuning Tips**:
```
Vocal brightening:  Gain = 5dB, Saturation = 40%, HPF = 3kHz
Drum sparkle:       Gain = 3dB, Saturation = 50%, HPF = 8kHz
Restore detail:     Gain = 2dB, Saturation = 20%, HPF = 2kHz
```

---

### [4] Virtual Bass

**Purpose**: Generate perceived sub-bass from harmonics (psychoacoustic bass extension)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | 0 to 12 | 5 | dB | Sub-bass output level |
| Frequency | 40 to 200 | 80 | Hz | Generated bass center frequency |
| Intensity | 0 to 100 | 50 | % | Harmonic generation strength |
| Mix | 0 to 100 | 70 | % | Blend generated vs original bass |

**Tuning Tips**:
```
Small speakers:  Frequency = 80Hz, Intensity = 70%, Gain = 6dB
Club system:     Frequency = 60Hz, Intensity = 50%, Gain = 3dB
Headphones:      Frequency = 100Hz, Intensity = 40%, Gain = 4dB
```

---

### [5] Bass Classic

**Purpose**: Traditional resonant bass boost (low-shelf EQ)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -12 to 12 | 5 | dB | Low-shelf boost amount |
| Frequency | 20 to 200 | 60 | Hz | Bass center frequency |
| Q | 0.5 to 2.0 | 0.7 | - | Shelf steepness (higher = narrower) |

**Tuning Tips**:
```
Deep thump:      Frequency = 40Hz, Gain = 8dB, Q = 0.7
Tight punch:     Frequency = 80Hz, Gain = 6dB, Q = 1.2
Warm presence:   Frequency = 100Hz, Gain = 3dB, Q = 0.5
```

---

### [6] Stereo Widener

**Purpose**: Enhance stereo width using Mid/Side (M/S) processing

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -6 to 12 | 3 | dB | Overall output level |
| Width | 0.5 to 2.0 | 1.2 | x | 1.0 = mono, 2.0 = very wide |
| HPF Frequency | 100 to 5000 | 200 | Hz | High-pass on side channel |

**Tuning Tips**:
```
Subtle enhancement:  Width = 1.1, Gain = 1dB
Wide stereo:         Width = 1.5, Gain = 0dB
Mono reference:      Width = 0.5, Gain = -3dB (for checking mono)
```

---

### [7] Dynamic EQ

**Purpose**: Switch between two EQ curves based on signal level (quiet = different tone than loud)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Crossover Level | -40 to 0 | -20 | dB | Level threshold to switch EQs |
| Low EQ Curve | - | See [8] | - | EQ applied to quiet signals |
| High EQ Curve | - | See [9] | - | EQ applied to loud signals |
| Attack Time | 5 to 500 | 50 | ms | EQ curve switch speed |

**Tuning Tips**:
```
Broadcast sound: Low = bright, High = warm (preserves detail at loud levels)
Music production: Low = flat, High = shaped (different mix at reference level)
Live event:      Low = presence boost, High = full featured
```

---

### [8] EQ1 (Main Parametric EQ)

**Purpose**: Primary 10-band parametric equalizer

| Per Band | Range | Default | Unit | Notes |
|----------|-------|---------|------|-------|
| Frequency | 20 to 20000 | [varies] | Hz | Band center frequency |
| Gain | -12 to 12 | 0 | dB | Boost/cut amount |
| Q | 0.1 to 10.0 | 1.0 | - | Bandwidth (0.1=wide, 10=narrow) |

**Max Bands**: 10 simultaneously

**Tuning Tips**:
```
Standard 3-band:
  Band 1: 100Hz, Q=0.7    → Bass tone
  Band 2: 1kHz, Q=1.0     → Midrange presence
  Band 3: 8kHz, Q=0.7     → Treble air

5-band shaping:
  Band 1: 60Hz (+5dB)     → Sub-bass
  Band 2: 250Hz (-3dB)    → Mud removal
  Band 3: 1kHz (+2dB)     → Vocal presence
  Band 4: 3kHz (-2dB)     → Harshness cut
  Band 5: 10kHz (+3dB)    → Air and detail
```

---

### [9] EQ2 (Post EQ / Tone Shaping)

**Purpose**: Secondary 10-band EQ for sound signature shaping

Same parameters as EQ1. Typically used for:
- Fine-tuning after EQ1
- Tone shaping per source (radio, podcast, etc.)
- Compensation for room acoustics

---

### [10] DRC (Dynamic Range Compressor)

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

**Tuning Tips**:
```
Gentle safety net (1 band):
  Threshold = -10dB, Ratio = 2:1, Attack = 20ms, Release = 100ms

Multi-band control (4 bands):
  Band 1: 150Hz,  Ratio = 4:1   → Subbass control
  Band 2: 800Hz,  Ratio = 3:1   → Midrange control
  Band 3: 3kHz,   Ratio = 2:1   → Presence control
  Band 4: 10kHz,  Ratio = 1.5:1 → Treble control
```

---

### [11] Volume

**Purpose**: Master volume control with smooth gain ramping (prevents clicks/pops)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -80 to 12 | 0 | dB | Output volume level |
| Ramp Time | 0 to 500 | 10 | ms | Smooth transition time |

**Tuning Tips**:
```
Normal listening:    Gain = -6dB (leaves headroom)
Loudness reference:  Gain = -18dB (LUFS measurement ref)
Safety headroom:     Gain = -12dB (3dB before clipping)
```

---

### [12] Soft Clipper

**Purpose**: Soft clipping limiter (prevents speaker damage, adds harmonic warmth)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -12 to 0 | 0 | dB | Clipping starts above this |
| Knee | 0 to 12 | 3 | dB | Soft knee width (smooth transition) |
| Saturation | 0 to 100 | 30 | % | Amount of harmonic distortion |

**Tuning Tips**:
```
Transparent protection:  Threshold = 0dB, Knee = 6dB, Saturation = 5%
Warm vintage tone:       Threshold = -3dB, Knee = 2dB, Saturation = 40%
Aggressive limiting:     Threshold = -6dB, Knee = 1dB, Saturation = 60%
```

---

### Quick Parameter Templates

#### Template 1: "Clarity Boost"
```
NoiseGate: -45dB threshold
Exciter: +5dB gain, 40% saturation, 4kHz HPF
EQ1: +3dB @ 2kHz (Q=1.2)
Volume: -6dB
```

#### Template 2: "Bass Enhancement"
```
VirtualBass: +6dB, 80Hz center
BassClassic: +5dB, 60Hz center
StereoWidener: 1.2x width
Volume: -3dB (compensate)
```

#### Template 3: "Broadcast Warm"
```
Compander: -15dB threshold, 3:1 ratio
BassMassic: +3dB, 50Hz center
EQ1: Small boost at 500Hz (presence)
SoftClipper: 0dB threshold with 3dB knee
Volume: -12dB reference
```

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
