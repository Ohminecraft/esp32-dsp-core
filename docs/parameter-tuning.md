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
2. Maps energy level → target preset index (highest threshold ≤ level)
3. Smoothly slews `slewIndex` toward target at a constant per-sample step
4. Blends output between adjacent presets (floor and ceil of `slewIndex`)
5. Per-sample blend & pregain ramp eliminates frame-boundary clicks during transitions

**Key constants (from include/config.h / isf.h)**

- `ISF_MAX_PRESETS` = 10 (maximum presets per ISF instance)
- `ISF_DEFAULT_RMS_MS` = 300 ms (default RMS averaging window)
- `ISF_DEFAULT_SLEW_MS` = 500 ms (default time to slew one index step)
- Lookahead buffer: up to 10 ms at 96 kHz (`ISF_LOOKAHEAD_MAX = 960` samples)

**Global Configuration**

| Parameter | Range | Default | Unit | Notes |
|-----------|-------|---------|------|-------|
| Num Presets | 1 to 10 | (module default) 1 if none loaded | - | Maximum is `ISF_MAX_PRESETS` (10). Module will initialize a single flat preset if no presets are provided by the controller |
| RMS Window | 10 to 5000 | 300 | ms | Energy averaging window; longer = slower response |
| Slew Time | 10 to 5000 | 500 | ms | Time to smoothly transition between adjacent preset indices |
| Lookahead | 0 to 10 | 0 | ms | Feed-forward delay for RMS detector; helps anticipate transients |
| Level Override | -60 to 0, or auto | auto | dB | Manual level for testing; `-9999` = use RMS detection (constant `ISF_OVERRIDE_AUTO`)

**Per-Preset Parameters**

Each preset contains:
- **Threshold** (-96 to 0 dB): Activation level stored Q8.8 in firmware (e.g. -30 * 256). Presets must be sorted by threshold (ascending order).
- **Pregain** (-72 to 18 dB): Volume adjustment applied before filtering (ramped per-sample during transition).
- **Filter Bank** (up to 10 bands): Each band is an independent biquad filter.
  - Enabled (on/off)
  - Type (Peaking, Low-shelf, High-shelf, Low-pass, High-pass, Notch)
  - Frequency (20 - 20000 Hz)
  - Gain (-12 to 12 dB)
  - Q (0.1 to 10.0)

**Default initialization behavior**
- If the ISF instance has zero presets when initialized, firmware creates a single flat preset with `threshold = -96 dB` and `pregain = 0` and sets `_numPresets = 1` so the module is safe by default.

**Smooth Crossfading Algorithm**:
```
// Per frame:
// slewIndex moves toward target at: (1 step / slewMs) per sample
// indexA = floor(slewIndex)
// indexB = ceil(slewIndex)
// blend = fractional part of slewIndex  (0.0..1.0)

// Per sample within frame:
// t = sample_index / numSamples           // 0..1 across frame
// blend_i = blendStart + t * (blendEnd - blendStart)  // ramped blend
// pgLin_i = pgLinA_start + t * (pgLinA_end - pgLinA)  // ramped pregain
// output[i] = (1 - blend_i) * pgLin_i * filterA(input[i])
//           + blend_i * pgLinB_i * filterB(input[i])
```

**Why ISF avoids clicks during level-based switching**:
1. **Per-sample blend ramp** (not per-frame constant): Eliminates amplitude steps at frame boundaries
2. **Per-sample pregain interpolation**: Smooth level transitions, no amplitude jump
3. **Always-warm filter state**: Both filterA and filterB continuously process, preventing stale transients when blend increases
4. **Smart state promotion**: When `slewIndex` crosses an integer boundary, stateB (preparing the next preset) may be promoted to stateA to preserve continuity
5. **Zero-init new states**: Newly entered presets start with zero state and warm up while their blend contribution is small

**Use Cases**:
1. **Loudness Compensation**: Low preset boost bass (+8dB @ 80Hz), high preset lift treble (+6dB @ 8kHz) to match loudness curves
2. **Adaptive Tone Shaping**: Different EQ curves for speech (narrow bandwidths) vs music (wider, more aggressive)
3. **Speaker Protection**: ISF ramps down all gains at high levels (e.g., a protection preset that reduces bass by -18dB)
4. **Parallel Makeup**: ISF before main EQ for level-aware bass, then main EQ handles general tone shaping
5. **Macro Control**: Bind ISF level override to a physical volume knob for manual preset selection

**Implementation Notes**:
- ISF instances are **fully independent**: each has its own RMS detector, slew state, lookahead buffer, and filter banks.
- **Preset atomicity**: Loading or updating presets is atomic from the audio thread's perspective; coefficients are recomputed and applied without audio interrupts.
- **CPU cost**: Both filter banks run during transitions. In steady state (blend == 0 or 1) ISF runs only one preset processing path.
- **Reporting & Commands**: The firmware exposes ISF-related commands and reporting opcodes (see `docs/protocol.md`) for remote configuration and telemetry.

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

**Mục đích**: Tự động chuyển đổi đường cong EQ dựa trên mức tín hiệu vào, kèm crossfade mượt mà giữa các preset. Hai instance hoàn toàn độc lập cho phép các cấu hình routing phức tạp (ví dụ ISF trước Master EQ hoặc chạy song song).

**Cách hoạt động (tóm tắt)**:
1. Liên tục đo năng lượng RMS của tín hiệu vào (cửa sổ có thể cấu hình)
2. Ánh xạ mức năng lượng → chỉ số preset mục tiêu (chọn chỉ số có threshold lớn nhất ≤ level)
3. `slewIndex` mượt mà tiến về mục tiêu với một bước cố định trên mỗi sample
4. Blend đầu ra giữa hai preset kề nhau (floor/ceil của `slewIndex`)
5. Per-sample blend và per-sample pregain ramp loại bỏ click khi chuyển đổi

**Các hằng số chính (từ include/config.h / isf.h)**

- `ISF_MAX_PRESETS` = 10 (số preset tối đa cho mỗi ISF)
- `ISF_DEFAULT_RMS_MS` = 300 ms (mặc định cửa sổ trung bình RMS)
- `ISF_DEFAULT_SLEW_MS` = 500 ms (mặc định thời gian để chuyển 1 bước index)
- Lookahead: tối đa ~10 ms ở 96 kHz (`ISF_LOOKAHEAD_MAX = 960` samples)

**Cấu hình toàn cục**

| Tham số | Phạm vi | Mặc định | Đơn vị | Ghi chú |
|---------|---------|----------|--------|--------|
| Số Presets | 1 → 10 | (mặc định module) 1 nếu không có preset | - | Giới hạn bởi `ISF_MAX_PRESETS` (10). Nếu không có preset, firmware tạo 1 preset flat mặc định để an toàn |
| RMS Window | 10 → 5000 | 300 | ms | Cửa sổ trung bình năng lượng; càng dài → phản hồi càng chậm |
| Slew Time | 10 → 5000 | 500 | ms | Thời gian chuyển mượt giữa hai chỉ số kế tiếp |
| Lookahead | 0 → 10 | 0 | ms | Độ trễ feed-forward cho detector, giúp phát hiện sớm các transient |
| Level Override | -96 → 0, hoặc auto | auto | dB | Ghi chú: firmware dùng hằng số `ISF_OVERRIDE_AUTO` = -9999 để biểu thị chế độ tự động (dùng RMS)

**Tham số mỗi preset**

Mỗi preset gồm:
- **Threshold** (định dạng Q8.8, tương đương -96 → 0 dB): Mức kích hoạt; preset phải được sắp xếp theo threshold tăng dần.
- **Pregain** (-72 → 18 dB): Điều chỉnh trước khi lọc; được nội suy per-sample trong chuyển tiếp.
- **Filter Bank** (tối đa 10 dải): mỗi dải là một biquad.
  - Enabled (bật/tắt)
  - Type (Peaking, Low-shelf, High-shelf, Low-pass, High-pass, Notch)
  - Frequency (20 → 20000 Hz)
  - Gain (-12 → 12 dB)
  - Q (0.1 → 10.0)

**Hành vi khởi tạo mặc định**
- Nếu ISF được khởi tạo mà không có preset nào (_numPresets == 0), firmware tự tạo một preset flat mặc định với `threshold = -96 dB` và `pregain = 0`, và đặt `_numPresets = 1` để module an toàn theo mặc định.

**Thuật toán crossfade (tóm tắt)**
```
// Mỗi frame:
// slewIndex tiến về target với tốc độ: (1 step / slewMs) trên mỗi sample
// indexA = floor(slewIndex)
// indexB = ceil(slewIndex)
// blend = phần thập phân của slewIndex (0.0 .. 1.0)

// Mỗi sample trong frame:
// t = sample_index / numSamples
// blend_i = blendStart + t * (blendEnd - blendStart)   // blend được ramp per-sample
// pgLin_i  = pgLinA_start + t * (pgLinA_end - pgLinA) // pregain nội suy per-sample
// output[i] = (1 - blend_i) * pgLin_i * filterA(x[i]) + blend_i * pgLinB_i * filterB(x[i])
```

**Tại sao ISF tránh được click**
1. Nội suy blend per-sample (không phải hằng số per-frame) → loại bỏ bước nhảy biên độ giữa các frame.
2. Nội suy pregain per-sample → không có nhảy về mức âm lượng.
3. Cả hai bộ lọc (A và B) luôn xử lý trong giai đoạn chuyển tiếp để giữ state "warm".
4. Khi vượt ranh số nguyên, firmware có cơ chế promote state để giữ continuity giữa preset.
5. Preset mới bắt đầu bằng state = 0 và sẽ warm up khi blend còn nhỏ, tránh transient lớn.

**Tình huống sử dụng**
- Loudness compensation: preset thấp boost bass, preset cao lift treble để phù hợp cảm nhận âm lượng.
- Adaptive tone: dùng preset khác cho speech vs music.
- Bảo vệ loa: preset bảo vệ giảm gain tại mức cao.
- Routing song song: ISF trước main EQ để xử lý bass theo mức.
- Điều khiển macro: gắn level-override với núm volume vật lý để chọn preset thủ công.

**Ghi chú triển khai**
- ISF là module độc lập: mỗi instance có detector, slew state, lookahead buffer và filter banks riêng.
- Tải/ cập nhật preset là nguyên tử từ góc nhìn audio thread — hệ số được tính trước rồi copy vào cấu trúc preset.
- Về CPU: cả hai filter banks chỉ chạy đồng thời khi đang chuyển tiếp; ở trạng thái ổn định chỉ chạy một path.
- Firmware cung cấp các lệnh cấu hình và báo cáo ISF (xem `docs/protocol.md`) để điều khiển từ xa và thu thập telemetry.