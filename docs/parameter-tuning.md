# Parameter Tuning Guide - ESP32 DSP Core (9 Modules)

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Parameter Tuning Reference (English)

Complete reference for all parameters across 9 DSP modules.

---

### [1] Compander

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

### [2] Exciter

**Purpose**: Add high-frequency harmonic content (brightness, air, sparkle)

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Cutoff Frequency | 300 to 10000 | 3000 | Hz | Crossover frequency for Exciter |
| Dry | 0 to 100 | 100 | % | Mid-range level (100% = No cut) |
| Wet | 0 to 100 | 30 | % | Treble harmonic level |

---

### [3] Dynamic Bass

**Purpose**: 3-zone adaptive bass extension with automatic gain fading for speaker protection.

**How it Works**:
- Measures RMS energy in bass band (LP filter @ Cutoff default)
- 3-zone state machine based on energy level:
  1. **Low energy zone** (≤ Boost threshold): Full `Gain Boost` applied.
  2. **Transition zone** (Boost → Neutral): Gain smoothly fades down to 0dB.
  3. **Protection zone** (Neutral → Clip): Gain fades out entirely and sub-limiter protection kicks in.

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Cutoff Frequency | 30 to 300 | 80 | Hz | Bass band crossover frequency |
| Gain Boost | 0 to 20 | 6.00 | dB | Bass boost strength (0.01 dB steps) |
| Enhanced | on/off | off | bool | Peaking punch around cutoff |
| Boost Full Threshold | -60 to 0 | -24.00 | dB | Level for full boost (0.01 dB steps) |
| Neutral Threshold | -60 to 0 | -16.00 | dB | Transition midpoint (0.01 dB steps) |
| Clip Full Threshold | -60 to 0 | -8.00 | dB | Level where boost is fully removed |
| Clip Attack | 1 to 2000 | 600 | ms | Smoothing attack for zone switching |
| Clip Release | 1 to 2000 | 200 | ms | Smoothing release for zone switching |

**Tuning Tips**:
```
Subtle bass boost:      Intensity = 20%, Cutoff = 80Hz
Deep bass presence:     Intensity = 60%, Cutoff = 60Hz, Enhanced = on
Bass protection (loud): Intensity = 80%, Clip Threshold = -1000
Headphone optimization: Cutoff = 100Hz, Intensity = 70%
```

---

### [4] Stereo Widener

**Purpose**: Enhance stereo width using Mid/Side (M/S) processing

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -6 to 12 | 3 | dB | Overall output level |
| Width | 0.5 to 2.0 | 1.2 | x | 1.0 = mono, 2.0 = very wide |
| HPF Frequency | 100 to 5000 | 200 | Hz | High-pass on side channel |

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

**Tips**
- Low Threshold = Normal Threshold: no gap at bottom → direct LOW↔HIGH
- High Threshold = Normal Threshold: no gap at top → direct LOW↔HIGH

---

### [6] EQ1 (Main Parametric EQ)

**Purpose**: Primary 10-band parametric equalizer

| Per Band | Range | Default | Unit | Notes |
|----------|-------|---------|------|-------|
| Frequency | 20 to 20000 | [varies] | Hz | Band center frequency |
| Gain | -12 to 12 | 0 | dB | Boost/cut amount |
| Q | 0.1 to 10.0 | 1.0 | - | Bandwidth (0.1=wide, 10=narrow) |

**Max Bands**: 10 simultaneously

---

### [7] EQ2 (Post EQ / Tone Shaping)

**Purpose**: Secondary 10-band EQ for sound signature shaping

Same parameters as EQ1. Typically used for:
- Fine-tuning after EQ1
- Tone shaping per source
- Compensation for room acoustics

---

### [8] DRC (Dynamic Range Compressor)

**Purpose**: Multi-band dynamic range compression/limiting

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

### [9] Volume

**Purpose**: Master volume control with smooth gain ramping

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -60 to 18 | 0 | dB | Output volume level |

---

<a name="tiếng-việt"></a>

## Hướng Dẫn Chỉnh Tham Số (Tiếng Việt)

Tham chiếu hoàn chỉnh cho 9 module DSP.

---

### [1] Compander

**Mục đích**: Nén/mở rộng động với tỷ lệ riêng biệt

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Ngưỡng | -60 đến 0 | -20 | dB | Điểm chia nén/mở rộng |
| Tỷ Lệ Trên | 1 đến 10 | 1 | :1 | Tỷ lệ nén trên ngưỡng |
| Tỷ Lệ Dưới | 0.5 đến 10 | 1 | :1 | Tỷ lệ mở rộng dưới ngưỡng |
| Thời Gian Attack | 1 đến 2000 | 10 | ms | Tốc độ attack nén |
| Thời Gian Release | 10 đến 2000 | 100 | ms | Tốc độ release nén |

---

### [2] Kích Thích

**Mục đích**: Thêm nội dung điều hòa tần số cao

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số Cutoff | 300 đến 10000 | 3000 | Hz | Tần số chia (crossover) cho Exciter |
| Dry | 0 đến 100 | 100 | % | Mức dải Mid (100% = giữ nguyên) |
| Wet | 0 đến 100 | 30 | % | Mức hài âm Treble |

---

### [3] Dynamic Bass

**Mục đích**: Bass động 3-zone thích ứng với năng lượng, tự động giảm boost khi tín hiệu quá lớn để bảo vệ loa.

**Cách Hoạt Động**:
- Đo RMS năng lượng trong dải bass (LP filter @ Cutoff)
- 3-zone state machine dựa trên mức năng lượng:
  1. **Vùng năng lượng thấp**: Áp dụng đầy đủ `Gain Boost`.
  2. **Vùng chuyển tiếp**: Gain giảm dần về 0dB khi đến ngưỡng Neutral.
  3. **Vùng bảo vệ**: Gain biến mất hoàn toàn và kích hoạt bộ lọc sub-limiter.

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số Cutoff | 30 đến 300 | 80 | Hz | Tần số chia dải bass |
| Gain Boost | 0 đến 20 | 6.00 | dB | Cường độ tăng bass (bước 0.01 dB) |
| Tăng Cường | on/off | off | bool | Tạo punch tại tần số cutoff |
| Ngưỡng Boost Đầy | -60 đến 0 | -24.00 | dB | Mức năng lượng cho boost đầy |
| Ngưỡng Trung Tính | -60 đến 0 | -16.00 | dB | Điểm giữa vùng chuyển tiếp |
| Ngưỡng Clip Đầy | -60 đến 0 | -8.00 | dB | Mức năng lượng xóa bỏ boost |
| Clip Attack | 1 đến 2000 | 600 | ms | Tốc độ chuyển vùng (mượt mà) |
| Clip Release | 1 đến 2000 | 200 | ms | Tốc độ quay lại vùng boost |

**Mẹo Chỉnh Tham Số**:
```
Bass tinh tế:         Cường Độ = 20%, Cutoff = 80Hz
Bass sâu:             Cường Độ = 60%, Cutoff = 60Hz, Tăng Cường = on
Bảo vệ bass (to):     Cường Độ = 80%, Clip Threshold = -1000
Tối ưu tai nghe:      Cutoff = 100Hz, Cường Độ = 70%
```

---

### [4] Mở Rộng Stereo

**Mục đích**: Nâng cao độ rộng stereo bằng xử lý M/S

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Đạt Được | -6 đến 12 | 3 | dB | Mức đầu ra tổng thể |
| Độ Rộng | 0.5 đến 2.0 | 1.2 | x | 1.0 = mono, 2.0 = rất rộng |
| Tần Số HPF | 100 đến 5000 | 200 | Hz | High-pass trên kênh side |

---

### [5] EQ Động

**Mục đích**: Chuyển đổi giữa hai đường cong EQ

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Ngưỡng Thấp | -60 đến 0 | -40 | dB | Ngưỡng mức cho EQ THẤP |
| Ngưỡng Bình Thường | -60 đến 0 | -20 | dB | Vùng không xử lý |
| Ngưỡng Cao | -60 đến 0 | -6 | dB | Ngưỡng mức cho EQ CAO |
| Thời Gian Attack | 1 đến 2000 | 10 | ms | Tốc độ chuyển EQ |
| Thời Gian Release | 10 đến 2000 | 100 | ms | Tốc độ quay lại bình thường |

---

### [6] EQ1 (EQ Chính Tham Số)

**Mục đích**: Parametric EQ 10 dải chính

| Mỗi Dải | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số | 20 đến 20000 | [thay đổi] | Hz | Tần số trung tâm |
| Đạt Được | -12 đến 12 | 0 | dB | Lượng tăng/cắt |
| Q | 0.1 đến 10.0 | 1.0 | - | Dải thông |

---

### [7] EQ2 (EQ Hậu Xử Lý)

**Mục đích**: Định hình chữ ký âm thanh

Cùng tham số như EQ1.

---

### [8] DRC (Bộ Nén Tầm Động)

**Mục đích**: Nén tầm động đa dải

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Số Dải | 1 đến 4 | 1 | - | Số dải tần số |
| Dải [1-4] Tần Số | 100 đến 10000 | [thay đổi] | Hz | Tần số chia |
| Dải [1-4] Ngưỡng | -60 đến 0 | -20 | dB | Ngưỡng nén |
| Dải [1-4] Tỷ Lệ | 1 đến 20 | 4 | :1 | Tỷ lệ nén |

---

### [9] Âm Lượng

**Mục đích**: Điều khiển âm lượng chính

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Đạt Được | -60 đến 18 | 0 | dB | Mức âm lượng đầu ra |
