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

<img width="2443" height="1574" alt="{51F4B98E-F3B9-43BF-8073-599A7667CA8D}" src="https://github.com/user-attachments/assets/b9b29d46-0472-4ce6-9631-57951dc82543" />

---

### Step-by-Step Usage

#### 1. **Connect to ESP32**

```
1. When started app will go to auto detect state
   → Send packet all port with ESP32 DSP board vi USB-TTL until find right device

OR

1. Manually select port from dropdown menu
2. Click "Connect" button
3. Wait for goto main layout
```

#### 2. **Enable/Disable Modules**

Click switch next to module name:

<img width="2402" height="72" alt="{D8D60A6F-4EF0-4068-88DB-A84D0CD395DF}" src="https://github.com/user-attachments/assets/3da55d9b-db65-4d4b-9ab1-449db61441fb" />

```
☑ Module = ENABLED (active in pipeline)
☐ Module = DISABLED (bypassed)
```

**Note**: Disabled modules consume NO CPU

#### 3. **Adjust Parameters**

Each module has sliders/controls:

<img width="2414" height="279" alt="{6D8A3303-6181-4D38-9B5B-CE278662EEBF}" src="https://github.com/user-attachments/assets/6e24eaeb-b1f2-4df2-9aca-648559c4eca2" />

**How to Use**:
- Click and drag slider
- OR type value directly in text box
- Changes send immediately to ESP32

#### 4. **Design EQ Curves**

Click "EQ Designer..." button for EQ1 or EQ2:

<img width="2401" height="861" alt="{CCEF9C50-2971-4149-ABC2-2D52FD16A150}" src="https://github.com/user-attachments/assets/1f6c3df9-34ad-4205-8bd3-2d1362d198b6" />

**Per Band Parameters**:
- **Frequency**: 20Hz - 20kHz
- **Gain**: -24dB to +24dB
- **Q (Q Factor)**: 0.1 - 10 (higher = narrower)

#### 5. **Save Presets**

```
1. Adjust all modules and parameters
2. Click "Save" button
3. Preset saved to ESP32 NVS
4. Next power-on: Slot 0 loads automatically
```

**Preset Slots**: 8 available (0-7)

#### 6. **Load Presets**

```
1. Click 1->8 preset slot button
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

#### E. WiFi Off Mode

To minimize audio distortion (by preventing frame processing lag under high heap memory usage) and free up system resources:
1. **Activate**: Double-press the power button (`POWER_PIN_OFF` / GPIO 40) rapidly. 
   - The current DSP parameters (EQ, volume, etc.) will be **automatically saved** as the boot default (Preset Slot 0).
   - The WiFi transceiver will turn **fully OFF**, and the status LED will breathe **Purple/Magenta**.
2. **Deactivate**: Double-press the power button again. WiFi will resume and reconnect.
3. **NVS Persistence**: The WiFi state is remembered in NVS. If you boot the device while WiFi is OFF, it will remain OFF on boot (showing breathing purple) without ever initializing WiFi, keeping the RF environment silent.

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

<img width="2443" height="1574" alt="{51F4B98E-F3B9-43BF-8073-599A7667CA8D}" src="https://github.com/user-attachments/assets/b9b29d46-0472-4ce6-9631-57951dc82543" />

### Hướng Dẫn Từng Bước

#### 1. **Kết Nối với ESP32**

```
1. Khi mở app lần đầu đầu sẽ auto scan tất cả port
   App sẽ gửi sync packet cho từng port qua USB-TTL đến khi tìm được đúng thiết bị

HOẶC

1. Chọn cổng thủ công từ menu
2. Nhấp nút "Kết Nối"
3. Chờ thông báo "✓ Đã Kết Nối"
```

#### 2. **Bật/Tắt Module**

Bật/Tắt công tắt bên cạnh tên module:

<img width="2402" height="72" alt="{D8D60A6F-4EF0-4068-88DB-A84D0CD395DF}" src="https://github.com/user-attachments/assets/231b355e-f248-4841-9393-df6b14b7e791" />

```
☑ Module = BẬT (hoạt động)
☐ Module = TẮT (bỏ qua)
```

#### 3. **Điều Chỉnh Tham Số**

Sử dụng thanh trượt/điều khiển:

<img width="2414" height="279" alt="{6D8A3303-6181-4D38-9B5B-CE278662EEBF}" src="https://github.com/user-attachments/assets/6e24eaeb-b1f2-4df2-9aca-648559c4eca2" />

#### 4. **Thiết Kế Đường Cong EQ**

<img width="2401" height="861" alt="{CCEF9C50-2971-4149-ABC2-2D52FD16A150}" src="https://github.com/user-attachments/assets/1f6c3df9-34ad-4205-8bd3-2d1362d198b6" />

#### 5. **Lưu Preset**

```
1. Điều chỉnh tất cả module
2. Nhấp "Save"
3. Preset lưu vào NVS của ESP32
4. Lần khởi động tiếp: Slot 0 tải tự động
```

#### 6. **Tải Preset**

```
1. Nhấp preset slot từ 1->9
2. Nó sẽ tự động khôi phục tất cả trạng thái module
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

#### D. Chế độ Tắt WiFi (WiFi Off)

Để giảm thiểu hiện tượng méo âm thanh (do bộ nhớ heap tăng cao gây trễ xử lý khung nhạc) và giải phóng tài nguyên hệ thống:
1. **Kích hoạt**: Nhấn đúp nhanh nút nguồn (`POWER_PIN_OFF` / GPIO 40).
   - Hệ thống tự động **lưu cấu hình âm thanh** hiện tại làm mặc định khởi động (NVS Preset 0).
   - Chip WiFi sẽ được **tắt hoàn toàn**, và đèn LED trạng thái chuyển sang **hiệu ứng thở màu Tím/Magenta**.
2. **Khôi phục**: Nhấn đúp nhanh nút nguồn lần nữa. WiFi sẽ tự động bật lại và kết nối lại thiết bị.
3. **Lưu trạng thái bền vững**: Trạng thái WiFi Off được ghi nhớ vào bộ nhớ NVS. Nếu bạn khởi động lại mạch khi đang ở chế độ Tắt WiFi, WiFi sẽ tiếp tục được khóa tắt ngay từ giây đầu tiên (LED thở tím) để đảm bảo môi trường hoạt động tốt nhất.

---

### Vấn Đề Phổ Biến

| Vấn Đề | Giải Pháp |
|--------|----------|
| Thanh trượt nhảy | Kiểm tra cáp USB, thử cổng khác |
| Không có âm thanh | Kiểm tra đầu nối audio |
| Preset không tải | Kiểm tra số slot hợp lệ (0-7) |
| GUI bị đóng băng | Ngắt kết nối, chờ 5s, kết nối lại |
| Tham số đặt lại sau khởi động | Lưu preset vào Slot 0 |
