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
| Ngưỡng | -60 đến 0 | -40 | dB | Cổng mở ở mức này |
| Thời Gian Attack | 0.5 đến 100 | 10 | ms | Cổng mở nhanh như thế nào |
| Thời Gian Release | 10 đến 1000 | 100 | ms | Cổng đóng nhanh như thế nào |
| Thời Gian Hold | 0 đến 500 | 50 | ms | Cổng giữ nguyên bao lâu |

---

### [2] Compander

**Mục đích**: Nén/mở rộng động với tỷ lệ riêng biệt trên/dưới ngưỡng

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Ngưỡng | -60 đến 0 | -20 | dB | Điểm chia cho nén/mở rộng |
| Tỷ Lệ Trên | 1 đến 10 | 1 | :1 | Tỷ lệ nén trên ngưỡng |
| Tỷ Lệ Dưới | 0.5 đến 10 | 1 | :1 | Tỷ lệ mở rộng dưới ngưỡng |
| Thời Gian Attack | 1 đến 2000 | 10 | ms | Tốc độ attack nén |
| Thời Gian Release | 10 đến 2000 | 100 | ms | Tốc độ release nén |
| Makeup Gain (WIP) | -12 đến 12 | 0 | dB | Bù lại mất mát từ nén |

---

### [3] Exciter

**Mục đích**: Thêm nội dung điều hòa tần số cao (sáng, không khí, lấp lánh)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số Cutoff | 300 đến 10000 | 3000 | Hz | Cutoff HighShelf cho xử lý |
| Dry | 0 đến 100 | 0 | % | Mức giữa |
| Wet | 0 đến 100 | 0 | % | Mức cao |

---

### [4] Bass Ảo

**Mục đích**: Sinh bass dưới từ điều hòa (mở rộng bass tâm lý)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số | 30 đến 300 | 60 | Hz | Tần số trung tâm bass được sinh ra |
| Cường Độ | 0 đến 100 | 0 | % | Cường độ sinh điều hòa |
| Tăng Cường | on/off | off | bool | Tăng cường cường độ sub-bass |

---

### [5] Bass Cổ Điển

**Mục đích**: Tăng bass cộng hưởng truyền thống (low-shelf EQ)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số | 30 đến 300 | 100 | Hz | Tần số trung tâm bass |
| Cường Độ | 0 đến 100 | 0 | % | Bass được tăng cường theo Cường Độ |

---

### [6] Mở Rộng Stereo (WIP Hiện Tại)

**Mục đích**: Nâng cao độ rộng stereo bằng xử lý Mid/Side (M/S)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Đạt Được | -6 đến 12 | 3 | dB | Mức đầu ra tổng thể |
| Độ Rộng | 0.5 đến 2.0 | 1.2 | x | 1.0 = mono, 2.0 = rất rộng |
| Tần Số HPF | 100 đến 5000 | 200 | Hz | High-pass trên kênh side |

---

### [7] EQ Động

**Mục đích**: Chuyển đổi giữa hai đường cong EQ dựa trên mức tín hiệu (yên tĩnh = tông khác với to)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Ngưỡng Thấp | -60 đến 0 | -40 | dB | Ngưỡng mức để chuyển sang EQ THẤP |
| Ngưỡng Bình Thường | -60 đến 0 | -20 | dB | Ngưỡng mức để quay lại bình thường (không dùng EQ) |
| Ngưỡng Cao | -60 đến 0 | -6 | dB | Ngưỡng mức để chuyển sang EQ CAO |
| Thời Gian Attack | 1 đến 2000 | 10 | ms | Tốc độ chuyển đổi đường cong EQ |
| Thời Gian Release | 10 đến 2000 | 100 | ms | Tốc độ quay lại bình thường |

EQ Động THẤP & EQ Động CAO: xem [8]

**Mẹo**:
- Ngưỡng Thấp = Ngưỡng Bình Thường: không có khoảng "không xử lý" ở phía dưới → chuyển trực tiếp THẤP↔CAO
- Ngưỡng Cao = Ngưỡng Bình Thường: không có khoảng "không xử lý" ở phía trên → chuyển trực tiếp THẤP↔CAO

---

### [8] EQ1 (EQ Chính Tham Số)

**Mục đích**: Equalizer tham số 10 dải chính

| Mỗi Dải | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Tần Số | 20 đến 20000 | [thay đổi] | Hz | Tần số trung tâm dải |
| Đạt Được | -12 đến 12 | 0 | dB | Lượng tăng/cắt |
| Q | 0.1 đến 10.0 | 1.0 | - | Dải thông (0.1=rộng, 10=hẹp) |

**Số Dải Tối Đa**: 10 cùng một lúc

---

### [9] EQ2 (EQ Hậu Xử Lý / Định Hình Tông)

**Mục đích**: EQ 10 dải thứ cấp cho định hình chữ ký âm thanh

Cùng tham số như EQ1. Thường dùng cho:
- Tinh chỉnh sau EQ1
- Định hình tông per source (radio, podcast, v.v.)
- Bù lại âm học phòng

---

### [10] DRC (Bộ Nén Tầm Động) (WIP Hiện Tại)

**Mục đích**: Nén tầm động đa dải/hạn chế (bảo vệ chống cắt ngắn)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Số Dải | 1 đến 4 | 1 | - | Số dải tần số |
| Dải [1-4] Tần Số | 100 đến 10000 | [thay đổi] | Hz | Tần số chia dải |
| Dải [1-4] Ngưỡng | -60 đến 0 | -20 | dB | Nén bắt đầu trên ngưỡng này |
| Dải [1-4] Tỷ Lệ | 1 đến 20 | 4 | :1 | Tỷ lệ nén |
| Dải [1-4] Attack | 1 đến 100 | 5 | ms | Tốc độ attack |
| Dải [1-4] Release | 10 đến 1000 | 50 | ms | Tốc độ release |
| Makeup Gain | -12 đến 12 | 0 | dB | Bù lại mất mát từ nén |

---

### [11] Âm Lượng

**Mục đích**: Điều khiển âm lượng chính với ramping gain mịn (tránh click/pop)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Đạt Được | -60 đến 18 | 0 | dB | Mức âm lượng đầu ra |

---

### [12] Kẹp Mềm

**Mục đích**: Bộ hạn chế kẹp mềm (tránh hư loa, thêm ấm điều hòa)

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Ngưỡng | -60 đến 0 | 0 | dB | Kẹp bắt đầu trên ngưỡng này |

---

### Mẫu Tham Số Nhanh

#### Mẫu 1: "Tăng Sắc Nét"
```
Cổng Nhiễu: -45dB
Kích Thích: Tần Số Cutoff = 3kHz
EQ1: +3dB @ 2kHz
Âm Lượng: -6dB
```

#### Mẫu 2: "Tăng Bass"
```
Bass Ảo: Cường Độ = 60Hz
Bass Cổ Điển: Tần Số = 100Hz, Cường Độ = 50%
Mở Rộng Stereo: Độ Rộng = 1.2x
Âm Lượng: -3dB
```

#### Mẫu 3: "Phát Sóng Ấm Áp"
```
Compander: Ngưỡng = -15dB, Tỷ Lệ Trên = 3:1
Bass Cổ Điển: Tần Số = 50Hz, Cường Độ = 50%
EQ1: +2dB @ 500Hz
Kẹp Mềm: Ngưỡng = 0dB
```
