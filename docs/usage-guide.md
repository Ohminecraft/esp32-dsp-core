# Usage Guide - ESP32 DSP Core GUI

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Usage Guide (English)

### GUI Overview

The ESP32 DSP Core control application (Electron-based) provides a user-friendly interface to:
- Connect to and control ESP32 DSP board via UART
- Enable/disable modules in real-time
- Adjust parameters with live feedback
- Design and visualize EQ curves
- Save/load presets

### Main Window Layout

```
┌─ ESP32 DSP Core Control ──────────────────────────────┐
│                                                        │
│  📡 Connection Status  ✓ Connected (COM3 @ 115200)   │
│  [Auto-Detect] [Connect] [Disconnect]                │
│                                                        │
│  ┌─ Module Controls ──────────────────────────────┐  │
│  │  ☑ NoiseGate    ─ Threshold: [▯▯▯▯▯] -30dB   │  │
│  │  ☑ Compander    ─ Ratio↑: [▯▯▯▯▯] 4:1        │  │
│  │  ☑ Exciter      ─ Gain: [▯▯▯▯▯] +3dB         │  │
│  │  ☑ VirtualBass  ─ Intensity: [▯▯▯▯▯] 50%     │  │
│  │  ☑ BassClassic  ─ Gain: [▯▯▯▯▯] +5dB         │  │
│  │  ☑ StereoWidener─ Width: [▯▯▯▯▯] 1.2x        │  │
│  │  ☑ DynamicEQ    ─ Mode: [Low EQ ▼]           │  │
│  │  ☑ EQ1          ─ [EQ Designer...]            │  │
│  │  ☑ EQ2          ─ [EQ Designer...]            │  │
│  │  ☑ DRC          ─ Bands: [4 ▼]                │  │
│  │  ☑ Volume       ─ Gain: [▯▯▯▯▯] 0dB          │  │
│  │  ☑ SoftClipper  ─ Threshold: [▯▯▯▯▯] 0dB    │  │
│  └────────────────────────────────────────────────┘  │
│                                                        │
│  ┌─ System Status ────────────────────────────────┐  │
│  │  CPU Usage: 45%  |  Heap Left: 52KB            │  │
│  │  Sample Rate: 96kHz | Format: 32-bit float    │  │
│  └────────────────────────────────────────────────┘  │
│                                                        │
│  ┌─ Presets ─────────────────────────────────────┐  │
│  │  [Slot 0] [Slot 1] [Slot 2]... [Slot 7]       │  │
│  │  [Save to Slot] [Load from Slot]              │  │
│  └────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

---

### Step-by-Step Usage

#### 1. **Connect to ESP32**

```
1. Click "Auto-Detect" button
   → GUI scans available COM ports
   → Selects port with ESP32 DSP board

OR

1. Manually select port from dropdown menu
2. Click "Connect" button
3. Wait for "✓ Connected" message
```

**Status Indicators**:
- ✓ Green = Connected
- ✗ Red = Disconnected
- ⟳ Yellow = Connecting...

#### 2. **Enable/Disable Modules**

Click checkbox next to module name:
```
☑ Module = ENABLED (active in pipeline)
☐ Module = DISABLED (bypassed)
```

**Note**: Disabled modules consume NO CPU

#### 3. **Adjust Parameters**

Each module has sliders/controls:

```
Module: Exciter
├─ Gain: [0---●---10] dB
├─ Saturation: [0---●---100] %
└─ HPF Frequency: [100--●--10k] Hz
```

**How to Use**:
- Click and drag slider
- OR type value directly in text box
- Changes send immediately to ESP32

#### 4. **Design EQ Curves**

Click "EQ Designer..." button for EQ1 or EQ2:

```
┌─ EQ Designer (EQ1) ──────────────────┐
│                                       │
│  Frequency Response Graph:            │
│  ┌───────────────────────────────┐   │
│  │         /\                     │   │
│  │        /  \                    │   │
│  │       /    \___                │   │
│  │      /          \              │   │
│  │  ──────────────────────        │   │
│  │  20Hz  100Hz  1kHz  10kHz     │   │
│  └───────────────────────────────┘   │
│                                       │
│  Band 1: 100Hz, Gain: +5dB, Q: 0.7  │
│  Band 2: 1kHz, Gain: -2dB, Q: 1.5   │
│  Band 3: 5kHz, Gain: +3dB, Q: 1.0   │
│                                       │
│  [Add Band] [Remove Band] [Apply]    │
└───────────────────────────────────────┘
```

**Per Band Parameters**:
- **Frequency**: 20Hz - 20kHz
- **Gain**: -12dB to +12dB
- **Q (Quality)**: 0.5 - 10 (higher = narrower)

#### 5. **Save Presets**

```
1. Adjust all modules and parameters
2. Click "Save to Slot X" button
3. Preset saved to ESP32 NVS
4. Next power-on: Slot 0 loads automatically
```

**Preset Slots**: 8 available (0-7)

#### 6. **Load Presets**

```
1. Click "Load from Slot X" button
2. All module states restored
3. GUI sliders update automatically
```

---

### Advanced Tips

#### A. Real-Time Listening

Enable/disable modules while music plays:
- Notice changes instantly
- No audio interruption
- Perfect for A/B testing

#### B. CPU Monitoring

Watch CPU usage % while adjusting:
```
CPU Usage: 45% (comfortable)
CPU Usage: 92% (approaching limit)
```

If exceeds 95%:
- Disable complex modules (DRC, multiple EQ)
- Reduce filter order
- Consider using fewer EQ bands

#### C. Gain Staging

For clean audio:
1. Set NoiseGate threshold just above noise floor
2. Adjust Compander to prevent clipping
3. Use SoftClipper only as safety limiter
4. Master Volume should be 0dB (unity)

#### D. Preset Organization

**Suggested naming** (in presets):
- Slot 0: Default flat response
- Slot 1: Bass boost (for bass music)
- Slot 2: Bright (for vocals)
- Slot 3: Treble cut (for harsh sources)
- Slot 4-7: Custom per use-case

---

### Common Issues

| Issue | Solution |
|-------|----------|
| Sliders jump around | Check USB cable, try different port |
| No sound after enable | Verify audio connections |
| Preset won't load | Check slot number valid (0-7) |
| GUI freezes | Disconnect, wait 5s, reconnect |
| Parameters reset after power | Save preset to Slot 0 |

---

<a name="tiếng-việt"></a>

## Hướng Dẫn Sử Dụng (Tiếng Việt)

### Tổng Quan Giao Diện

Ứng dụng điều khiển ESP32 DSP Core (dựa trên Electron) cung cấp giao diện thân thiện để:
- Kết nối và điều khiển bo mạch ESP32 qua UART
- Bật/tắt module thời gian thực
- Điều chỉnh tham số với phản hồi trực tiếp
- Thiết kế và trực quan hóa đường cong EQ
- Lưu/tải preset

### Bố Cục Cửa Sổ Chính

```
┌─ ESP32 DSP Core Điều Khiển ────────────────────────┐
│                                                     │
│  📡 Trạng Thái Kết Nối  ✓ Đã Kết Nối (COM3)       │
│  [Tự động Phát Hiện] [Kết Nối] [Ngắt Kết Nối]   │
│                                                     │
│  ┌─ Điều Khiển Module ───────────────────────────┐ │
│  │  ☑ Cổng Nhiễu      ─ Ngưỡng: [●---] -30dB    │ │
│  │  ☑ Compander       ─ Tỷ Lệ: [●---] 4:1       │ │
│  │  ☑ Kích Thích      ─ Đạt: [●---] +3dB        │ │
│  │  ☑ Bass Ảo         ─ Cường: [●---] 50%       │ │
│  │  ☑ Bass Cổ Điển    ─ Đạt: [●---] +5dB        │ │
│  │  ☑ Mở Rộng Stereo  ─ Độ Rộng: [●---] 1.2x    │ │
│  │  ☑ EQ Động         ─ Chế Độ: [EQ Thấp ▼]     │ │
│  │  ☑ EQ1             ─ [EQ Designer...]         │ │
│  │  ☑ EQ2             ─ [EQ Designer...]         │ │
│  │  ☑ DRC             ─ Dải: [4 ▼]               │ │
│  │  ☑ Âm Lượng         ─ Đạt: [●---] 0dB        │ │
│  │  ☑ Kẹp Mềm         ─ Ngưỡng: [●---] 0dB     │ │
│  └───────────────────────────────────────────────┘ │
│                                                     │
│  ┌─ Trạng Thái Hệ Thống ────────────────────────┐ │
│  │  Sử Dụng CPU: 45% | Heap Còn Lại: 52KB       │ │
│  │  Tần Số Mẫu: 96kHz | Định Dạng: 32-bit float│ │
│  └───────────────────────────────────────────────┘ │
│                                                     │
│  ┌─ Preset ──────────────────────────────────────┐ │
│  │  [Slot 0] [Slot 1]... [Slot 7]                │ │
│  │  [Lưu] [Tải]                                  │ │
│  └───────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

### Hướng Dẫn Từng Bước

#### 1. **Kết Nối với ESP32**

```
1. Nhấp nút "Tự Động Phát Hiện"
   → GUI quét cổng COM
   → Chọn cổng có bo mạch ESP32

HOẶC

1. Chọn cổng thủ công từ menu
2. Nhấp nút "Kết Nối"
3. Chờ thông báo "✓ Đã Kết Nối"
```

#### 2. **Bật/Tắt Module**

Nhấp hộp kiểm bên cạnh tên module:
```
☑ Module = BẬT (hoạt động)
☐ Module = TẮT (bỏ qua)
```

#### 3. **Điều Chỉnh Tham Số**

Sử dụng thanh trượt/điều khiển:
```
Module: Kích Thích
├─ Đạt: [0---●---10] dB
├─ Độ Bão Hòa: [0---●---100] %
└─ Tần Số HPF: [100--●--10k] Hz
```

#### 4. **Thiết Kế Đường Cong EQ**

Nhấp nút "EQ Designer...":
```
Tần Số: 100Hz | Đạt: +5dB | Q: 0.7
Tần Số: 1kHz  | Đạt: -2dB | Q: 1.5
Tần Số: 5kHz  | Đạt: +3dB | Q: 1.0
```

#### 5. **Lưu Preset**

```
1. Điều chỉnh tất cả module
2. Nhấp "Lưu vào Slot X"
3. Preset lưu vào NVS của ESP32
4. Lần khởi động tiếp: Slot 0 tải tự động
```

#### 6. **Tải Preset**

```
1. Nhấp "Tải từ Slot X"
2. Khôi phục tất cả trạng thái module
3. Thanh trượt GUI cập nhật tự động
```

---

### Mẹo Nâng Cao

#### A. Nghe Trực Tiếp Thời Gian Thực

- Bật/tắt module trong khi nhạc chạy
- Nhận thấy thay đổi ngay lập tức
- Hoàn hảo cho so sánh A/B

#### B. Giám Sát CPU

```
Sử Dụng CPU: 45% (thoải mái)
Sử Dụng CPU: 92% (gần đạt giới hạn)
```

Nếu vượt 95%:
- Tắt module phức tạp (DRC, EQ đa)
- Giảm thứ tự bộ lọc
- Sử dụng ít dải EQ hơn

#### C. Điều Chỉnh Đạt

Để âm thanh sạch:
1. Đặt ngưỡng NoiseGate trên tiếng ồn nền
2. Điều chỉnh Compander để tránh cắt ngắn
3. Sử dụng SoftClipper chỉ như an toàn
4. Master Volume nên là 0dB (unity)

---

### Vấn Đề Phổ Biến

| Vấn Đề | Giải Pháp |
|--------|----------|
| Thanh trượt nhảy | Kiểm tra cáp USB, thử cổng khác |
| Không có âm thanh | Kiểm tra đầu nối audio |
| Preset không tải | Kiểm tra số slot hợp lệ (0-7) |
| GUI bị đóng băng | Ngắt kết nối, chờ 5s, kết nối lại |
| Tham số đặt lại sau khởi động | Lưu preset vào Slot 0 |
