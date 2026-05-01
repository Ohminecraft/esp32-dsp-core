# Parameter Tuning Guide - ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Parameter Tuning Reference (English)

Complete reference for all parameters across 11 DSP modules.

---

### [1] Pre Gain

**Purpose**: Input volume stage to adjust levels before passing through the effect chain.

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -60 to 18 | 0 | dB | Input adjustment level |

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

---

### [3] Exciter

**Purpose**: Add high-frequency harmonic content (brightness, air, sparkle)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Cutoff Frequency | 300 to 10000 | 3000 | Hz | Crossover frequency for Exciter |
| Dry | 0 to 100 | 100 | % | Mid-range level (100% = No cut) |
| Wet | 0 to 100 | 30 | % | Treble harmonic level |

---

### [4] Dynamic Bass

**Purpose**: 3-zone adaptive bass extension with automatic gain fading for speaker protection.

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Cutoff Frequency | 30 to 300 | 80 | Hz | Bass band crossover frequency |
| Gain Boost | 0 to 20 | 6.00 | dB | Bass boost strength (0.01 dB steps) |
| Enhanced | on/off | off | bool | Peaking punch around cutoff |
| Boost Full Threshold | -60 to 0 | -24.00 | dB | Level for full boost |
| Neutral Threshold | -60 to 0 | -16.00 | dB | Transition midpoint |
| Clip Full Threshold | -60 to 0 | -8.00 | dB | Level where boost is fully removed |
| Clip Attack | 1 to 2000 | 600 | ms | Smoothing attack for zone switching |
| Clip Release | 1 to 2000 | 200 | ms | Smoothing release for zone switching |

---

### [5] Dynamic EQ

**Purpose**: Switch between two EQ curves based on signal level

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Low Threshold Level | -60 to 0 | -40 | dB | Level threshold to switch EQ LOW |
| Normal Threshold Level | -60 to 0 | -20 | dB | No-processing zone threshold |
| High Threshold Level | -60 to 0 | -6 | dB | Level threshold to switch EQ HIGH |
| Attack Time | 1 to 2000 | 10 | ms | EQ curve switch speed |
| Release Time | 10 to 2000 | 100 | ms | EQ curve back to normal |

---

### [6] EQ1 (Main Parametric EQ)

**Purpose**: Primary 10-band parametric equalizer

| Per Band | Range | Default | Unit | Notes |
|----------|-------|---------|------|-------|
| Frequency | 20 to 20000 | [varies] | Hz | Band center frequency |
| Gain | -12 to 12 | 0 | dB | Boost/cut amount |
| Q | 0.1 to 10.0 | 1.0 | - | Bandwidth (0.1=wide, 10=narrow) |

---

### [7] EQ2 (Post EQ / Tone Shaping)

**Purpose**: Secondary 10-band EQ for sound signature shaping

Same parameters as EQ1. Typically used for room and speaker acoustics compensation.

---

### [8] Left/Right EQ

**Purpose**: Fully independent 10-band parametric EQ curves for Left and Right audio channels. Useful for 1.1 setups, asymmetric spaces, or correcting channel imbalance.

---

### [9] DRC (Dynamic Range Compressor)

**Purpose**: Multi-band dynamic range compression/limiting

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Band Threshold | -60 to 0 | -20 | dB | Compression starts above this |
| Band Ratio | 1 to 20 | 4 | :1 | Compression ratio |
| Band Attack | 1 to 100 | 5 | ms | Attack speed |
| Band Release | 10 to 1000 | 50 | ms | Release speed |

---

### [10] Post Gain

**Purpose**: Master output volume control with smooth gain ramping at the end of the DSP signal chain.

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -60 to 18 | 0 | dB | Output volume level |

---

<a name="tiếng-việt"></a>

## Hướng Dẫn Chỉnh Tham Số (Tiếng Việt)

Tham chiếu đầy đủ cho 11 module DSP.

---

### [1] Pre Gain

**Mục đích**: Chỉnh âm lượng đầu vào trước khi qua các hiệu ứng.

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Gain | -60 đến 18 | 0 | dB | Mức tăng/giảm âm lượng đầu vào |

---

### [2] Compander

**Mục đích**: Nén/mở rộng động với tỷ lệ riêng biệt.

---

### [3] Exciter

**Mục đích**: Thêm nội dung điều hòa tần số cao giúp âm thanh sáng hơn.

---

### [4] Dynamic Bass

**Mục đích**: Bass động 3-zone thích ứng với năng lượng để bảo vệ loa.

---

### [5] Dynamic EQ

**Mục đích**: Tự động chuyển đổi giữa 2 đường cong EQ tùy vào âm lượng.

---

### [6] EQ1 & [7] EQ2

**Mục đích**: 2 bộ EQ tham số 10 dải hoàn toàn riêng biệt.

---

### [8] Left/Right EQ

**Mục đích**: Chỉnh EQ riêng lẻ cho 2 kênh Left & Right (phù hợp cho loa 1.1 hoặc không gian không đối xứng).

---

### [9] DRC (Bộ Nén Tầm Động)

**Mục đích**: Nén/giới hạn tầm động tín hiệu.

---

### [10] Post Gain

**Mục đích**: Điều chỉnh âm lượng tổng thể đầu ra (Master Volume).

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Gain | -60 đến 18 | 0 | dB | Âm lượng đầu ra |
