# Getting Started - ESP32 DSP Core

> **Languages**: [English](#english) | [Tiếng Việt](#tiếng-việt)

---

<a name="english"></a>

## Getting Started (English)

### Prerequisites

- **Hardware**:
  - ESP32-DevKit board
  - PCM1808 stereo ADC module
  - PCM5102A stereo DAC module
  - USB-to-UART adapter (CH340, FT232, etc.)
  - Audio cables and connectors

- **Software**:
  - VS Code with PlatformIO extension
  - Python 3.8+ (for PlatformIO)
  - Node.js 14+ (for Electron app)
  - Git

### Step 1: Clone Repository

```bash
git clone https://github.com/ohminecraft/esp32-dsp-core.git
cd esp32-dsp-core
```

### Step 2: Build & Flash Firmware

```bash
# Install PlatformIO if needed
pip install platformio

# Build firmware
pio run

# Upload to ESP32
pio run -t upload

# Monitor serial output
pio device monitor -b 115200
```

**Expected Output**:
```
[ESP32] DSP Core Initializing...
Core 1: Audio Task Started
Core 0: Control Task Started
I2S Input: 96kHz, 32-bit float
I2S Output: 96kHz, 32-bit float
UART Protocol: Ready on GPIO16/17
```

### Step 3: Setup Control Application

```bash
cd dsp-control-app

# Install Node dependencies
npm install

# Start development server
npm start
```

**Expected**: Electron window opens with GUI

### Step 4: Connect Hardware

1. **Audio I/O**:
   ```
   PCM1808 (ADC) ← Line Input
   PCM5102A (DAC) → Line Output
   GPIO0 ← MCLK
   ```

2. **Serial Connection**:
   ```
   USB-UART ← GPIO16 (RX)
   USB-UART → GPIO17 (TX)
   ```

3. **Power**: 5V through micro-USB or AMS1117 regulator

### Step 5: First Test

1. Start the GUI application
2. Click "Auto-Detect" port
3. Connect audio source to PCM1808 input
4. Enable modules one-by-one
5. Adjust parameters on sliders
6. Listen to output from PCM5102A

---

### Troubleshooting

#### Q: "Board not found" error
**A**: Check USB cable and CH340 or CP210x drivers installed

#### Q: No sound output
**A**: 
- Verify audio connectors
- Check GPIO0 has connected with MCLK PCM1808
- Check PCM1808 must have both 3.3 and 5v connected
- Inspect PCM5102A bypass capacitors (100nF)
- Monitor serial output for errors

#### Q: GUI won't connect
**A**:
- Check UART port selected matches device
- Verify GPIO16/17 connections
- Try different USB port
- Restart GUI application

#### Q: Crackling/Distortion audio
**A**:
- Reduce input gain on PCM1808
- Disable soft clipper module
- Check for floating I2S clock lines
- Verify ESP32 power supply is clean

#### Q: High CPU usage
**A**:
- Disable unused modules
- Check for infinite loops in DSP code
- Use `pio device monitor` to see CPU % stat

---

### Next Steps

- **Learn Parameters**: Read [Parameter Tuning](./parameter-tuning.md)
- **Use GUI**: Follow [Usage Guide](./usage-guide.md)
- **Add Effects**: Check [Adding Effects](./adding-effects.md)
- **Protocol Details**: See [UART Protocol](./protocol.md)

---

<a name="tiếng-việt"></a>

## Bắt Đầu (Tiếng Việt)

### Yêu Cầu Chuẩn Bị

- **Phần Cứng**:
  - Bo mạch ESP32-DevKit
  - Module ADC stereo PCM1808
  - Module DAC stereo PCM5102A
  - Bộ chuyển đổi USB-UART (CH340, FT232, v.v.)
  - Cáp audio và đầu nối

- **Phần Mềm**:
  - VS Code + tiện ích PlatformIO
  - Python 3.8+
  - Node.js 14+
  - Git

### Bước 1: Clone Kho Lưu Trữ

```bash
git clone https://github.com/ohminecraft/esp32-dsp-core.git
cd esp32-dsp-core
```

### Bước 2: Xây Dựng & Tải Lên Firmware

```bash
# Cài PlatformIO
pip install platformio

# Xây dựng
pio run

# Tải lên ESP32
pio run -t upload

# Giám sát đầu ra nối tiếp
pio device monitor -b 115200
```

**Đầu Ra Dự Kiến**:
```
[ESP32] DSP Core Initializing...
Core 1: Audio Task Started
Core 0: Control Task Started
I2S Input: 96kHz, 32-bit float
I2S Output: 96kHz, 32-bit float
UART Protocol: Ready on GPIO16/17
```

### Bước 3: Thiết Lập Ứng Dụng Điều Khiển

```bash
cd dsp-control-app

# Cài đặt phụ thuộc Node
npm install

# Chạy máy chủ phát triển
npm start
```

**Dự Kiến**: Cửa sổ Electron mở cùng GUI

### Bước 4: Kết Nối Phần Cứng

1. **Đầu Vào/Ra Âm Thanh**:
   ```
   PCM1808 (ADC) ← Đầu vào dòng
   PCM5102A (DAC) → Đầu ra dòng
   GPIO0 ← MCLK
   ```

2. **Kết Nối Nối Tiếp**:
   ```
   USB-UART ← GPIO16 (RX)
   USB-UART → GPIO17 (TX)
   ```

3. **Nguồn**: 5V qua micro-USB

### Bước 5: Kiểm Tra Đầu Tiên

1. Chạy ứng dụng GUI
2. Nhấp "Auto-Detect"
3. Kết nối nguồn âm thanh vào PCM1808
4. Bật từng module
5. Điều chỉnh tham số
6. Nghe đầu ra từ PCM5102A

---

### Khắc Phục Sự Cố

#### Q: Lỗi "Board not found"
**A**: Kiểm tra cáp USB và driver CH340 hoặc CP210x được cài

#### Q: Không có âm thanh
**A**:
- Kiểm tra đầu nối audio
- Kiểm tra GPIO0 đã kết nối với chân MCLK của PCM1808 chưa
- Kiểm tra PCM1808 đã có đủ nguồn 3.3 và 5v chưa
- Kiểm tra tụ bypass trên PCM5102A (100nF)
- Giám sát đầu ra nối tiếp

#### Q: GUI không kết nối
**A**:
- Kiểm tra cổng UART được chọn
- Xác minh kết nối GPIO16/17
- Thử cổng USB khác
- Khởi động lại ứng dụng

#### Q: Tiếng rít/Âm thanh bị méo
**A**:
- Giảm độ lợi đầu vào trên PCM1808
- Tắt module soft clipper
- Kiểm tra dòng xung nhịp I2S nổi
- Xác minh cấp nguồn ESP32 sạch

#### Q: Sử dụng CPU cao
**A**:
- Tắt các module không dùng
- Kiểm tra vòng lặp vô hạn trong mã DSP
- Dùng `pio device monitor` để xem CPU %

---

### Bước Tiếp Theo

- **Học Tham Số**: Đọc [Chỉnh Tham Số](./parameter-tuning.md)
- **Sử Dụng GUI**: Theo [Hướng Dẫn Sử Dụng](./usage-guide.md)
- **Thêm Hiệu Ứng**: Xem [Thêm Hiệu Ứng](./adding-effects.md)
- **Chi Tiết Giao Thức**: Xem [Giao Thức UART](./protocol.md)
