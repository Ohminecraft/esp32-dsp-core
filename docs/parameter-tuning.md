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

**Purpose**: High-fidelity dynamic processor with separate compression (above threshold) and expansion (below threshold) ratios. Features stateful envelope tracking for smooth transitions.

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -60 to 0 | -20.00 | dB | Split point for compression/expansion |
| Ratio Above | 1.00 to 10 | 1.00 | :1 | Compression ratio (1.00 = No compression) |
| Ratio Below | 0.10 to 10 | 1.00 | :1 | Expansion ratio (<1.00 = Expansion, 1.00 = No expansion) |
| Attack Time | 1 to 2000 | 10 | ms | Smoothing for volume increases |
| Release Time | 10 to 2000 | 100 | ms | Smoothing for volume decreases |
| Pregain | -72 to 18 | 0 | dB | Level adjustment before the envelope follower |

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

### [9] Multi-band DRC

**Purpose**: Professional-grade multi-band dynamic range compressor with selectable crossovers. Prevents "pumping" artifacts by processing frequency bands independently.

**Global Controls**
- **Mode**: 
    - `Full Band`: Single band processing (legacy mode).
    - `2 Band`: Splits signal into Low/High.
    - `2 Band + Full`: Low/High processing followed by a global compressor.
    - `3 Band`: Low/Mid/High processing.
    - `3 Band + Full`: 3-band processing followed by a global compressor.
- **Crossover Filter**: Select between `Butterworth 6dB`, `Linkwitz-Riley 12dB`, `Linkwitz-Riley 24dB`, or `Q-Controlled 24dB` (Parametric).
- **Crossover Frequencies**: 20Hz - 20kHz range for splitting bands.

**Per-Band Parameters**
| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Threshold | -90 to 0 | -15.00 | dB | Compression starts above this level |
| Ratio | 1.00 to 20 | 4.00 | :1 | Ratio of reduction (1.00 to 20.00) |
| Attack | 1 to 500 | 5 | ms | Time to apply gain reduction |
| Release | 10 to 2000 | 160 | ms | Time to recover gain |
| Pregain | -72 to 18 | 0 | dB | Gain applied before detection |

---

### [10] Post Gain

**Purpose**: Master output volume control with smooth gain ramping at the end of the DSP signal chain.

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Gain | -60 to 18 | 0 | dB | Output volume level |

---

### [11] Index Selectable Filter (ISF)

**Purpose**: Dynamically switch EQ curves based on input signal level with smooth crossfading. Two independent instances allow complex routing scenarios (e.g., ISF before Master EQ, ISF in parallel chains).

**How it works**:
1. Continuously measures input RMS energy (configurable window)
2. Maps energy level → target preset index (closest threshold match)
3. Smoothly slews slewIndex toward target at constant rate (configurable time)
4. Blends output between adjacent presets (floor and ceil of slewIndex)
5. Per-sample blend & pregain ramp eliminates frame-boundary clicks during transitions

**Global Configuration**

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Num Presets | 1 to 5 | 5 | - | Number of presets loaded |
| RMS Window | 10 to 5000 | 100 | ms | Energy averaging window; longer = slower response |
| Slew Time | 10 to 5000 | 400 | ms | Time to smoothly transition between presets |
| Level Override | -60 to 0, or auto | auto | dB | Manual level for testing; `-9999` = auto RMS |

**Per-Preset Parameters**

Each preset contains:
- **Threshold** (-60 to 0 dB): Activation level. Presets must be sorted by threshold (ascending order).
- **Pregain** (-72 to 18 dB): Volume adjustment applied before filtering (ramp per-sample during transition).
- **Filter Bank** (up to 10 bands): Each band is an independent biquad filter.
  - Enabled (on/off)
  - Type (Peaking, Low-shelf, High-shelf, Low-pass, High-pass, Notch)
  - Frequency (20 - 20000 Hz)
  - Gain (-12 to 12 dB)
  - Q (0.1 to 10.0)

**Example preset arrangement** (sorted by threshold, ascending):
```
Preset 0: threshold = -60 dB → Main bass boost curve
Preset 1: threshold = -40 dB → Balanced curve (kicks in at moderate levels)
Preset 2: threshold = -20 dB → Treble-heavy curve (bright at high levels)
Preset 3: threshold = -6 dB  → Protection curve (reduces gain at peaks)
```

When input level = -25 dB → target preset = 1 (highest threshold ≤ -25).  
When input level = -5 dB → target preset = 2 (highest threshold ≤ -5).

**Smooth Crossfading Algorithm**:
```
// Per frame:
slewIndex moves toward target at: (1 step / slewMs) per sample
indexA = floor(slewIndex)
indexB = ceil(slewIndex)
blend = fractional part of slewIndex  (0.0..1.0)

// Per sample within frame:
t = sample_index / numSamples           // 0..1 across frame
blend_i = blendStart + t * (blendEnd - blendStart)  // ramped blend
pgLin_i = pgLinA_start + t * (pgLinA_end - pgLinA)  // ramped pregain
output[i] = (1 - blend_i) * pgLin_i * filterA(input[i])
          + blend_i * pgLinB_i * filterB(input[i])
```

**Why ISF avoids clicks during level-based switching**:
1. **Per-sample blend ramp** (not per-frame constant): Eliminates amplitude step at frame boundaries
2. **Per-sample pregain interpolation**: Smooth level transitions, no amplitude jump
3. **Always-warm filter state**: Both filterA and filterB continuously process, preventing stale transients when blend increases
4. **Smart state promotion**: When slewIndex crosses integer boundary, stateB (which was pre-computing the next preset) is promoted to stateA, maintaining signal continuity
5. **Zero-init new states**: When a new preset enters (from cold), its state starts at zero and warms up during the blending window when blend is still small

**Use Cases**:
1. **Loudness Compensation**: Low preset boost bass (+8dB @ 80Hz), high preset lift treble (+6dB @ 8kHz) to match Fletcher-Munson curves
2. **Adaptive Tone Shaping**: Different EQ curves for speech (narrow bandwidths, centered) vs music (wider, more aggressive)
3. **Speaker Protection**: ISF ramps down all gains at high levels (e.g., preset 3 applies -18dB to bass) to prevent clipping
4. **Parallel Makeup**: ISF before main EQ for level-aware bass, then main EQ handles general tone shaping
5. **Macro Control**: Tie ISF level override to a physical volume knob for manual preset selection

**Implementation Notes**:
- ISF instances are **fully independent**: Each has its own RMS detector, slew state, and filter banks.
- **Preset atomicity**: Loading a new preset (band parameter change) doesn't interrupt audio processing — coefficients are recomputed and applied smoothly.
- **CPU cost**: Only active during transitions (when blend != 0 or 1). In steady state (blend at 0 or 1), ISF runs a single filter chain.
- **State handoff robustness**: Handles edge cases like rapid level changes, backwards slewing, or manual level overrides.

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

**Mục đích**: Bộ xử xử lý động cao cấp với tỷ lệ nén (trên ngưỡng) và mở rộng (dưới ngưỡng) riêng biệt. Sử dụng thuật toán envelope tracking bền vững giúp chuyển đổi âm lượng mượt mà.

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

### [9] Multi-band DRC (Nén Đa Băng Tần)

**Mục đích**: Bộ nén động đa băng tần chuyên nghiệp. Giúp kiểm soát âm lượng các dải tần độc lập (Bass/Mid/Treble), tránh hiện tượng "pumping" khi có tiếng bass mạnh.

**Chế độ hoạt động (Mode)**:
- `Full Band`: Nén toàn dải (1 băng tần).
- `2 Band / 3 Band`: Chia tín hiệu thành các dải Bass/High hoặc Bass/Mid/High để xử lý riêng.
- `Mode + Full`: Kết hợp nén từng dải sau đó nén tổng thể (Series Compression).

**Bộ lọc phân tần (Crossover)**:
- Hỗ trợ các kiểu lọc chuyên dụng: `Linkwitz-Riley` (đặc tuyến phẳng tại điểm cắt) và `Butterworth`.
- Độ dốc lọc lên đến 24dB/octave giúp tách dải cực kỳ sạch sẽ.

**Tham số từng băng tần**:
- **Threshold**: Ngưỡng bắt đầu nén (dB).
- **Ratio**: Tỷ lệ nén (ví dụ 4.00:1).
- **Attack/Release**: Tốc độ phản ứng của bộ nén (ms).
- **Pregain**: Bù/giảm âm lượng trước khi nén.

---

### [10] Post Gain

**Mục đích**: Điều chỉnh âm lượng tổng thể đầu ra (Master Volume).

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Gain | -60 đến 18 | 0 | dB | Âm lượng đầu ra |

---

### [11] Index Selectable Filter (ISF)

**Mục đích**: Tự động chuyển đổi đường cong EQ dựa vào mức tín hiệu với crossfade mượt mà. Hai instance độc lập cho phép routing phức tạp (ví dụ: ISF trước Master EQ, ISF chạy song song).

**Cách hoạt động**:
1. Liên tục đo năng lượng RMS của input (cửa sổ configurable)
2. Ánh xạ mức năng lượng → chỉ số preset target (khớp threshold gần nhất)
3. Mượt mà "slew" slewIndex về target với tốc độ cố định (configurable)
4. Blend output giữa 2 preset kế cận (floor và ceil của slewIndex)
5. Per-sample blend & pregain ramp loại bỏ click tại frame boundary trong transition

**Cấu Hình Toàn Cục**

| Tham Số | Phạm Vi | Mặc Định | Đơn Vị | Ghi Chú |
|---------|---------|----------|--------|--------|
| Số Presets | 1 đến 16 | 4 | - | Số lượng preset được load |
| RMS Window | 10 đến 5000 | 100 | ms | Cửa sổ lấy trung bình năng lượng; càng dài → đáp ứng càng chậm |
| Slew Time | 10 đến 5000 | 400 | ms | Thời gian chuyển tiếp mượt mà giữa các preset |
| Level Override | -60 đến 0, hoặc auto | auto | dB | Mức thủ công để test; `-9999` = dùng RMS |

**Tham Số Từng Preset**

Mỗi preset chứa:
- **Threshold** (-60 đến 0 dB): Mức kích hoạt. Presets phải được sắp xếp theo threshold (tăng dần).
- **Pregain** (-72 đến 18 dB): Điều chỉnh âm lượng trước lọc (ramp per-sample trong transition).
- **Filter Bank** (tối đa 10 dải): Mỗi dải là một biquad lọc độc lập.
  - Enabled (bật/tắt)
  - Type (Peaking, Low-shelf, High-shelf, Low-pass, High-pass, Notch)
  - Frequency (20 - 20000 Hz)
  - Gain (-12 đến 12 dB)
  - Q (0.1 đến 10.0)

**Ví dụ sắp xếp preset** (theo threshold tăng dần):
```
Preset 0: threshold = -60 dB → Đường cong bass boost chính
Preset 1: threshold = -40 dB → Đường cong cân bằng (kích hoạt ở mức trung bình)
Preset 2: threshold = -20 dB → Đường cong treble nặng (sáng ở mức cao)
Preset 3: threshold = -6 dB  → Đường cong bảo vệ (giảm gain ở đỉnh)
```

Khi mức input = -25 dB → preset target = 1 (threshold cao nhất ≤ -25).  
Khi mức input = -5 dB → preset target = 2 (threshold cao nhất ≤ -5).

**Thuật Toán Crossfade Mượt Mà**:
```
// Mỗi frame:
slewIndex di chuyển về target: (1 step / slewMs) mỗi sample
indexA = floor(slewIndex)
indexB = ceil(slewIndex)
blend = phần lẻ của slewIndex  (0.0..1.0)

// Mỗi sample trong frame:
t = sample_index / numSamples           // 0..1 qua frame
blend_i = blendStart + t * (blendEnd - blendStart)  // blend được ramp
pgLin_i = pgLinA_start + t * (pgLinA_end - pgLinA)  // pregain được ramp
output[i] = (1 - blend_i) * pgLin_i * filterA(input[i])
          + blend_i * pgLinB_i * filterB(input[i])
```

**Tại sao ISF tránh được click khi chuyển đổi theo mức**:
1. **Per-sample blend ramp** (không phải per-frame hằng số): Loại bỏ bước nhảy biên độ tại ranh giới frame
2. **Per-sample pregain interpolation**: Chuyển tiếp mứcmượt mà, không bất ngờ
3. **Filter state luôn "warm"**: Cả filterA và filterB liên tục xử lý, tránh transient cũ khi blend tăng
4. **Smart state promotion**: Khi slewIndex vượt số nguyên, stateB (đang pre-compute preset tiếp theo) được promote lên stateA, giữ liên tục tín hiệu
5. **Zero-init state mới**: Khi preset mới vào (cold start), state bắt đầu từ zero và warm up trong cửa sổ blend khi blend còn nhỏ

**Tình Huống Sử Dụng**:
1. **Loudness Compensation**: Preset thấp boost bass (+8dB @ 80Hz), preset cao lift treble (+6dB @ 8kHz) để match Fletcher-Munson curve
2. **Adaptive Tone Shaping**: Đường cong EQ khác nhau cho speech (hẹp, chặt) vs nhạc (rộng, mạnh mẽ)
3. **Bảo vệ Loa**: ISF ramp xuống all gains ở mức cao (ví dụ preset 3 áp -18dB bass) để tránh clipping
4. **Parallel Makeup**: ISF trước main EQ cho bass theo mức, sau đó main EQ xử lý tone chung
5. **Macro Control**: Gắn ISF level override với nút volume vật lý để chọn preset thủ công

**Ghi Chú Kỹ Thuật**:
- **ISF instances hoàn toàn độc lập**: Mỗi cái có RMS detector, slew state, và filter bank riêng.
- **Preset atomicity**: Load preset mới (thay band parameter) không gián đoạn xử lý âm thanh — hệ số được tính lại và áp dụng mượt mà.
- **Chi phí CPU**: Chỉ active trong transition (blend ≠ 0 hoặc 1). Ở trạng thái ổn định (blend = 0 hoặc 1), ISF chạy single filter chain.
- **State handoff robust**: Xử lý đúng các trường hợp biên (level change nhanh, slew lùi, override thủ công).