---
document_id: FRAM-FM24CL04B-DATASHEET-NOTE
title: FM24CL04B Datasheet Summary
component: FM24CL04B
category: F-RAM / Non-volatile Memory
interface: I2C
source_document: Infineon/Cypress FM24CL04B Datasheet, Document Number 001-84455 Rev. *L, Revised December 5, 2018
status: Reviewed implementation note
---

# FM24CL04B — 4-Kbit Serial I2C F-RAM

> **Nguồn chuẩn:** Datasheet Infineon/Cypress `FM24CL04B, 4-Kbit (512 × 8) Serial (I2C) F-RAM`, Document Number `001-84455 Rev. *L`, revised `December 5, 2018`. Tài liệu này là implementation note, không thay thế các giới hạn điện, timing diagram và package drawing trong datasheet gốc.

## 1. Mục đích tài liệu

Tài liệu này tóm tắt các thông tin quan trọng từ datasheet của **FM24CL04B** để phục vụ thiết kế phần cứng, firmware và tích hợp vào hệ thống nhúng.

FM24CL04B là bộ nhớ **F-RAM không mất dữ liệu** có giao tiếp **I2C**, phù hợp để lưu các dữ liệu cần ghi thường xuyên như cấu hình, chỉ số tích lũy, trạng thái hệ thống, dữ liệu hiệu chuẩn và log sự kiện nhỏ.

Tài liệu phân biệt ba lớp trách nhiệm:

```text
I2C transport
→ FM24CL04B byte-address driver
→ persistent record/A-B slot repository
→ config, calibration, counter và event services
```

Độ bền ghi cao của F-RAM không tự tạo ra atomic update, CRC, versioning hoặc recovery sau mất nguồn. Các tính chất đó phải được xây dựng ở record/repository layer.

---

## 2. Tổng quan linh kiện

| Thuộc tính | Giá trị |
|---|---|
| Mã linh kiện | `FM24CL04B` |
| Loại bộ nhớ | F-RAM, non-volatile memory |
| Dung lượng | 4 Kbit |
| Tổ chức bộ nhớ | 512 × 8 bit = 512 byte |
| Giao tiếp | I2C / 2-wire serial interface |
| Tốc độ I2C tối đa | 1 MHz |
| Điện áp hoạt động | 2.7 V đến 3.65 V |
| Nhiệt độ hoạt động | -40 °C đến +85 °C |
| Package | 8-pin SOIC |
| RoHS | Có |

---

## 3. Đặc điểm chính

- Bộ nhớ **4-Kbit F-RAM**, tổ chức logic dạng **512 × 8 bit**.
- Ghi dữ liệu không cần thời gian chờ kiểu EEPROM.
- Hỗ trợ ghi/đọc ở tốc độ bus I2C.
- Độ bền ghi/đọc rất cao: **10^14 read/write cycles**.
- Data retention **151 năm ở 65 °C** theo bảng datasheet, đây là giá trị tối thiểu tại điều kiện `TA = 65 °C`.
- Hỗ trợ tốc độ I2C:
  - 100 kHz
  - 400 kHz
  - 1 MHz
- Có thể dùng thay thế trực tiếp cho một số EEPROM I2C cùng pinout.
- Dòng tiêu thụ thấp:
- Active current tối đa **100 µA tại 100 kHz**, **170 µA tại 400 kHz**, và **300 µA tại 1 MHz** theo điều kiện đo của datasheet.
  - Standby current điển hình khoảng **3 µA**.

---

## 4. So sánh nhanh với EEPROM I2C

| Tiêu chí | FM24CL04B F-RAM | EEPROM I2C thông thường |
|---|---|---|
| Cơ chế lưu trữ | Ferroelectric RAM | Floating-gate EEPROM |
| Non-volatile | Có | Có |
| Thời gian ghi | Gần như tức thời, theo tốc độ bus | Cần write delay |
| Acknowledge polling | Không cần | Thường cần |
| Độ bền ghi | Rất cao, khoảng 10^14 cycles | Thấp hơn nhiều |
| Phù hợp ghi thường xuyên | Rất phù hợp | Hạn chế hơn |
| Page buffering | Không có page buffer kiểu EEPROM | Thường có page write buffer |

**Ý nghĩa firmware:** Khi ghi vào FM24CL04B, firmware không cần chờ `write cycle time` hoặc thực hiện acknowledge polling như EEPROM. Sau khi truyền thành công byte dữ liệu, dữ liệu đã được ghi vào bộ nhớ.

---

## 5. Pinout

FM24CL04B sử dụng package **8-pin SOIC**.

| Pin | Tên chân | Kiểu | Chức năng |
|---:|---|---|---|
| 1 | NC | - | Không kết nối |
| 2 | A1 | Input | Chọn địa chỉ thiết bị trên bus I2C |
| 3 | A2 | Input | Chọn địa chỉ thiết bị trên bus I2C |
| 4 | VSS | Power | Ground |
| 5 | SDA | Input/Output | Đường dữ liệu I2C, open-drain, cần điện trở pull-up ngoài |
| 6 | SCL | Input | Đường clock I2C |
| 7 | WP | Input | Write Protect |
| 8 | VDD | Power | Nguồn cấp |

`NC` phải để hở. Không dùng chân này làm test point hoặc nối vào một net khác.

---

## 6. Ghi chú thiết kế phần cứng

### 6.1 Nguồn cấp

- Nối `VDD` vào nguồn trong khoảng **2.7 V đến 3.65 V**.
- Nối `VSS` vào ground hệ thống.
- Với hệ STM32 chạy 3.3 V, FM24CL04B có thể dùng trực tiếp trên cùng mức logic 3.3 V.
- Bố trí tụ decoupling 100 nF gần `VDD`-`VSS` là khuyến nghị thiết kế PCB thông thường. Giá trị này là khuyến nghị tích hợp, không phải một dòng thông số bắt buộc được nêu trong datasheet.
- FM24CL04B chỉ có power-on reset đơn giản, không có power-management circuit bảo vệ transaction đang chạy. Nguồn và brownout policy của hệ thống phải giữ giao tiếp trong vùng `VDD` hợp lệ.

### 6.2 I2C pull-up

Chân `SDA` là open-drain, vì vậy cần có điện trở pull-up ngoài trên bus I2C. Trong thực tế, cả `SDA` và `SCL` của bus I2C thường cần pull-up về `VDD`.

Datasheet đưa công thức tham khảo:

```text
Rp_min = (VDD - VOL_max) / IOL
Rp_max = tr / (0.8473 × Cb)
```

Trong đó:

- `Rp`: điện trở pull-up.
- `VOL_max`: điện áp mức thấp tối đa.
- `IOL`: dòng kéo xuống.
- `tr`: thời gian rise time cho phép.
- `Cb`: điện dung bus.

Không chọn pull-up chỉ theo một giá trị quen dùng. Phải kiểm tra đồng thời:

- `Rp >= Rp_min` để không vượt quá khả năng kéo xuống khi SDA ở mức LOW.
- `Rp <= Rp_max` để đạt rise time.
- Rise time tối đa là 1000 ns ở 100 kHz và 300 ns ở 400 kHz/1 MHz.
- Tổng `Cb` gồm chân IC, MCU, đường PCB, connector, cáp và đầu đo.

Nếu bus dài hoặc có nhiều thiết bị, 1 MHz có thể không đạt được dù peripheral của STM32 hỗ trợ tốc độ đó.

### 6.3 Chân A1, A2

- `A1` và `A2` dùng để chọn địa chỉ thiết bị trên bus I2C.
- Hai chân này có pull-down nội.
- Có thể gắn tối đa **4 IC FM24CL04B** trên cùng một bus I2C bằng cách cấu hình khác nhau hai chân `A1`, `A2`.
- Trong sản phẩm nên nối `A1/A2` rõ ràng về `VDD` hoặc `VSS`; không dựa vào pull-down nội như một thay thế cho định tuyến địa chỉ có chủ đích.

### 6.4 Chân WP

| Trạng thái WP | Ý nghĩa |
|---|---|
| `WP = HIGH / VDD` | Khóa ghi toàn bộ vùng nhớ |
| `WP = LOW / VSS` | Cho phép ghi toàn bộ vùng nhớ |

Chân `WP` có pull-down nội. Nếu không cần khóa ghi phần cứng, có thể nối `WP` xuống GND. Nếu muốn bảo vệ dữ liệu cấu hình/calibration, có thể điều khiển `WP` bằng GPIO của MCU.

Thiết kế fail-safe được khuyến nghị cho dữ liệu quan trọng:

```text
WP có pull-up ngoài lên VDD
MCU dùng GPIO open-drain để chỉ kéo WP xuống trong phiên ghi
reset/mất nguồn MCU → GPIO high-Z → WP tự trở về trạng thái khóa ghi
```

Datasheet không công bố timing setup/hold riêng cho `WP`, vì vậy giữ `WP` ổn định trong toàn bộ transaction ghi và chỉ khóa lại sau STOP. Khi `WP=HIGH`, IC không ACK data byte và address counter không tăng. Nếu firmware không đọc được trạng thái GPIO/WP, một lỗi NACK không thể tự động được phân loại chắc chắn là `FRAM_ERR_WP` thay vì lỗi bus.

---

## 7. Kiến trúc bộ nhớ

FM24CL04B có tổng cộng **512 byte**, được truy cập bằng địa chỉ 9 bit.

| Thành phần địa chỉ | Số bit | Ý nghĩa |
|---|---:|---|
| Page select bit | 1 bit | Chọn page 256 byte |
| Word address | 8 bit | Chọn byte trong page |
| Tổng địa chỉ | 9 bit | Địa chỉ duy nhất cho 512 byte |

Không gian nhớ:

```text
0x000 ... 0x0FF   Page 0
0x100 ... 0x1FF   Page 1
```

Sau khi truy cập đến địa chỉ cuối `0x1FF`, bộ đếm địa chỉ nội sẽ quay vòng về `0x000`.

Một byte dữ liệu chỉ có địa chỉ đầy đủ khi ghép:

```text
address[8]   = A0/page bit nằm trong slave address
address[7:0] = word address byte
```

Đây không phải thiết bị có word address 16 bit theo kiểu EEPROM dung lượng lớn.

---

## 8. Địa chỉ I2C

Byte địa chỉ slave có dạng:

```text
Bit:   7 6 5 4  3  2  1   0
       1 0 1 0  A2 A1 A0  R/W
```

Trong đó:

| Trường | Ý nghĩa |
|---|---|
| `1010b` | Slave ID cố định cho FM24CL04B |
| `A2:A1` | Bit chọn thiết bị, lấy từ chân phần cứng |
| `A0` | Page select bit, chọn page 256 byte |
| `R/W` | `0` = write, `1` = read |

### 8.1 Địa chỉ 7-bit khi A2 = 0, A1 = 0

| Vùng nhớ | A2 | A1 | A0/page | I2C 7-bit address |
|---|---:|---:|---:|---:|
| Page 0 | 0 | 0 | 0 | `0x50` |
| Page 1 | 0 | 0 | 1 | `0x51` |

### 8.2 Toàn bộ không gian slave address

| A2 | A1 | Page 0 | Page 1 |
|---:|---:|---:|---:|
| 0 | 0 | `0x50` | `0x51` |
| 0 | 1 | `0x52` | `0x53` |
| 1 | 0 | `0x54` | `0x55` |
| 1 | 1 | `0x56` | `0x57` |

Mỗi IC chiếm hai địa chỉ 7-bit liền nhau vì page bit nằm trong slave address. Bốn IC sẽ sử dụng toàn bộ dải `0x50`-`0x57`; phải kiểm tra xung đột với EEPROM/F-RAM khác trên cùng bus.

### 8.3 Chuyển địa chỉ logic sang STM32 HAL

Trong STM32 HAL, tham số `DevAddress` của các hàm I2C thường dùng địa chỉ 7-bit đã dịch trái 1 bit.

Ví dụ:

| I2C 7-bit address | STM32 HAL DevAddress |
|---:|---:|
| `0x50` | `0xA0` |
| `0x51` | `0xA2` |

Hàm chuyển đổi nên nhận địa chỉ logic `0x000`-`0x1FF`, không bắt application tự chọn page:

```c
#define FM24CL04B_SIZE_BYTES       512u
#define FM24CL04B_BASE_ADDR_7BIT  0x50u

static uint16_t fm24cl04b_hal_dev_address(uint16_t address,
                                           bool a2,
                                           bool a1)
{
    uint8_t page = (uint8_t)((address >> 8) & 0x01u);
    uint8_t addr7 = (uint8_t)(FM24CL04B_BASE_ADDR_7BIT |
                              ((uint8_t)a2 << 2) |
                              ((uint8_t)a1 << 1) |
                              page);
    return (uint16_t)addr7 << 1;
}

static uint8_t fm24cl04b_word_address(uint16_t address)
{
    return (uint8_t)(address & 0xFFu);
}
```

Quy ước `DevAddress` có thể khác giữa HAL/LL hoặc framework khác. Xác nhận API của phiên bản STM32 đang dùng thay vì áp dụng phép dịch trái một cách máy móc.

---

## 9. Giao tiếp I2C

FM24CL04B luôn hoạt động như một **I2C slave**. MCU đóng vai trò **I2C master** và tạo clock `SCL`.

Các điều kiện bus quan trọng:

| Điều kiện | Mô tả |
|---|---|
| START | SDA chuyển HIGH xuống LOW khi SCL đang HIGH |
| STOP | SDA chuyển LOW lên HIGH khi SCL đang HIGH |
| ACK | Receiver kéo SDA xuống LOW sau byte dữ liệu |
| NACK | Receiver không kéo SDA xuống LOW, thường dùng để kết thúc đọc |

Dữ liệu được truyền theo thứ tự **MSB first**.

Các quy tắc quan trọng khác:

- IC lấy mẫu dữ liệu vào ở cạnh lên SCL và thay đổi dữ liệu ra ở cạnh xuống.
- SDA phải ổn định khi SCL HIGH, trừ START/STOP.
- START mới có thể abort operation đang chạy và đưa IC về trạng thái nhận command mới.
- STOP cũng kết thúc/abort operation hiện tại.
- Nếu nguồn từng tụt dưới `VDD(min)` trong một operation, sau khi nguồn hợp lệ trở lại hệ thống phải phát START trước operation tiếp theo.

---

## 10. Ghi dữ liệu

### 10.1 Single-byte write

Trình tự ghi một byte:

```text
START
Slave Address + Write bit
ACK
Word Address
ACK
Data Byte
ACK
STOP
```

### 10.2 Multi-byte write

FM24CL04B cho phép ghi nhiều byte liên tiếp trong một transaction:

```text
START
Slave Address + Write bit
ACK
Word Address
ACK
Data Byte 0
ACK
Data Byte 1
ACK
...
STOP
```

Sau mỗi byte dữ liệu, địa chỉ nội bộ tự tăng. Nếu vượt quá `0x1FF`, địa chỉ sẽ quay vòng về `0x000`.

Không có page-write boundary tại `0x0FF`: nếu transaction không bị ngắt, internal 9-bit address counter có thể đi từ `0x0FF` sang `0x100`. Page bit trong slave address chỉ chọn địa chỉ bắt đầu.

Tuy nhiên, mỗi transaction mới phải tính lại `A0/page` từ địa chỉ logic mới. Driver có thể chủ động chia transfer tại `0x100` để đơn giản hóa trace, retry và tương thích với `HAL_I2C_Mem_*`.

### 10.3 Điểm khác EEPROM

- Không có write delay hiệu dụng.
- Không cần acknowledge polling.
- Không có page buffering.
- Ghi thực sự xảy ra sau khi bit dữ liệu thứ 8 được truyền và hoàn tất trước khi ACK được gửi.

Hệ quả về mất nguồn:

- Một byte đã truyền đủ 8 bit và được ACK có thể đã được ghi bền vững.
- Một byte chưa truyền đủ 8 bit không nên được xem là đã ghi.
- Một multi-byte write không atomic: mất nguồn giữa transaction có thể để lại prefix mới và phần còn lại cũ.
- Vì không có page buffer/commit nội, cấu trúc nhiều byte cần CRC và commit protocol ở firmware.

Muốn abort **data byte hiện tại** mà không làm byte đó thay đổi, START hoặc STOP phải xuất hiện trước bit dữ liệu thứ 8. Các byte trước đó đã ACK vẫn có thể đã được ghi và không được rollback.

---

## 11. Đọc dữ liệu

Datasheet mô tả hai kiểu đọc chính:

1. **Current Address Read**
2. **Selective / Random Read**

### 11.1 Current Address Read

FM24CL04B dùng địa chỉ nội đang được giữ trong address latch. Địa chỉ bắt đầu đọc là địa chỉ ngay sau lần truy cập trước đó.

Chính xác hơn, lower 8 bits lấy từ internal address latch, còn bit địa chỉ thứ 9 lấy từ page bit của slave address trong transaction đọc hiện tại. Vì vậy current-address read dễ phụ thuộc trạng thái trước đó và không nên là primitive mặc định cho persistent repository.

Trình tự:

```text
START
Slave Address + Read bit
ACK
Data Byte
NACK
STOP
```

### 11.2 Sequential Read

Nếu master tiếp tục ACK sau mỗi byte đọc, FM24CL04B sẽ xuất byte tiếp theo và tự tăng địa chỉ nội bộ.

```text
START
Slave Address + Read bit
ACK
Data Byte 0
ACK
Data Byte 1
ACK
...
Last Data Byte
NACK
STOP
```

### 11.3 Selective / Random Read

Để đọc từ một địa chỉ bất kỳ, master cần nạp trước word address bằng một pha write giả, sau đó phát repeated START để chuyển sang read.

```text
START
Slave Address + Write bit
ACK
Word Address
ACK
Repeated START
Slave Address + Read bit
ACK
Data Byte
NACK
STOP
```

Slave address ở cả pha write giả và pha read phải chứa page bit tương ứng với địa chỉ logic cần đọc. Với STM32 HAL, `HAL_I2C_Mem_Read(..., I2C_MEMADD_SIZE_8BIT, ...)` thường tạo đúng chuỗi write-address/repeated-START/read-address, nhưng phải truyền `DevAddress` đã chứa page bit.

Kết thúc đọc ưu tiên:

```text
master NACK byte cuối ở clock thứ 9
→ master phát STOP ở clock thứ 10
```

Nếu master ACK byte cuối, FM24CL04B sẽ chuẩn bị drive byte tiếp theo và có thể gây bus contention khi master cố phát command mới.

---

## 12. Đặc tính điện DC

| Thông số | Điều kiện | Min | Typ | Max | Đơn vị |
|---|---|---:|---:|---:|---|
| VDD | Nguồn cấp | 2.7 | 3.3 | 3.65 | V |
| IDD | fSCL = 100 kHz | - | - | 100 | µA |
| IDD | fSCL = 400 kHz | - | - | 170 | µA |
| IDD | fSCL = 1 MHz | - | - | 300 | µA |
| ISB | Standby current | - | 3 | 6 | µA |
| VIH | Input HIGH | 0.7 × VDD | - | VDD + 0.3 | V |
| VIL | Input LOW | -0.3 | - | 0.3 × VDD | V |
| VOL | IOL = 3 mA | - | - | 0.4 | V |

---

## 13. Data retention và endurance

| Thông số | Điều kiện | Giá trị tối thiểu |
|---|---|---:|
| Data retention | TA = 85 °C | 10 năm |
| Data retention | TA = 75 °C | 38 năm |
| Data retention | TA = 65 °C | 151 năm |
| Endurance | Over operating temperature | 10^14 cycles |

Không diễn giải “151 năm” thành retention chung cho mọi nhiệt độ: datasheet chỉ bảo đảm mức đó tại 65 °C; tại 85 °C là 10 năm. Retention và endurance là hai giới hạn độc lập. Endurance rất cao giúp giảm nhu cầu wear-leveling, nhưng không loại bỏ nhu cầu atomic commit và integrity check.

Ví dụ, ngay cả 16 lần cập nhật mỗi giây liên tục trong 20 năm cũng khoảng `1.01 × 10^10` lần truy cập, vẫn thấp hơn nhiều so với `10^14`. Trong dự án này, lý do dùng A/B slot chủ yếu là chống torn-write và tạo snapshot nhất quán, không phải vì endurance quá thấp.

---

## 14. Capacitance và thermal

### 14.1 Capacitance

| Thông số | Mô tả | Max | Đơn vị |
|---|---|---:|---|
| CO | Output pin capacitance, SDA | 8 | pF |
| CI | Input pin capacitance | 6 | pF |

### 14.2 Thermal resistance

| Thông số | Mô tả | 8-pin SOIC | Đơn vị |
|---|---|---:|---|
| ΘJA | Junction-to-ambient | 147 | °C/W |
| ΘJC | Junction-to-case | 47 | °C/W |

---

## 15. AC switching characteristics

FM24CL04B hỗ trợ các mức tốc độ I2C chính:

| Mode | fSCL tối đa | Ghi chú |
|---|---:|---|
| Standard-mode | 100 kHz | Tương thích legacy timing |
| Fast-mode | 400 kHz | Tương thích legacy timing |
| 1 MHz mode | 1 MHz | Tốc độ tối đa của linh kiện theo datasheet; không nhầm với I2C High-speed mode 3.4 MHz |

Một số timing quan trọng:

| Thông số | 100 kHz | 400 kHz | 1 MHz | Đơn vị |
|---|---:|---:|---:|---|
| tLOW min | 4.7 | 1.3 | 0.6 | µs |
| tHIGH min | 4.0 | 0.6 | 0.4 | µs |
| tBUF min | 4.7 | 1.3 | 0.5 | µs |
| tSU;DAT min | 250 | 100 | 100 | ns |
| tAA max | 3.0 | 0.9 | 0.55 | µs |

Ngoài `fSCL`, peripheral timing của STM32 phải thỏa `tLOW`, `tHIGH`, repeated-START setup/hold, STOP setup, bus-free time, rise/fall time và `tAA`. Không nên chỉ đặt `ClockSpeed=1 MHz` rồi giả định bus hợp lệ.

---

## 16. Power cycle timing

| Thông số | Mô tả | Min | Đơn vị |
|---|---|---:|---|
| tPU | Từ VDD(min) đến lần truy cập đầu tiên | 1 | ms |
| tPD | Từ lần truy cập cuối đến power-down | 0 | µs |
| tVR | VDD power-up ramp rate | 30 | µs/V |
| tVF | VDD power-down ramp rate | 30 | µs/V |

`tPU=1 ms` được tính từ thời điểm VDD vượt `VDD(min)=2.7 V` đến START đầu tiên, không phải từ thời điểm bật regulator.

`tVR` và `tVF` có đơn vị µs/V: waveform nguồn phải đáp ứng tối thiểu 30 µs/V trong cả power-up và power-down. Đây không phải delay firmware.

### 16.1 Power-fail policy

FM24CL04B không có power-fail transaction manager. Thiết kế hệ thống nên:

1. Cấu hình BOR/PVD của MCU để không bắt đầu I2C access khi nguồn gần dưới 2.7 V.
2. Chặn request ghi mới khi phát hiện power-fail.
3. Nếu điều khiển `WP`, đưa WP về HIGH càng sớm càng tốt nhưng không coi thao tác này là rollback cho byte đã ghi.
4. Sau brownout/reset, đợi đủ `tPU`, recovery bus nếu cần, rồi scan/validate A/B slots.
5. Không dùng “STOP đã phát” làm bằng chứng application record hoàn chỉnh; phải kiểm tra commit marker và CRC.

---

## 17. Ordering information

| Ordering code | Package | Operating range | Ghi chú |
|---|---|---|---|
| `FM24CL04B-G` | 8-pin SOIC | Industrial | Standard |
| `FM24CL04B-GTR` | 8-pin SOIC | Industrial | Tape and Reel |

Cả hai mã đều là Pb-free.

---

## 18. Gợi ý dùng trong hệ thống đồng hồ nước siêu âm

Với hệ thống **STM32 + MAX35103/MAX35101 + Modbus + F-RAM**, FM24CL04B phù hợp để lưu các nhóm dữ liệu sau:

| Nhóm dữ liệu | Ví dụ |
|---|---|
| Calibration | Offset ToF, hệ số lưu lượng, hệ số hiệu chỉnh nhiệt độ |
| Metering data | Tổng lưu lượng tích lũy, chỉ số đồng hồ |
| Device config | Modbus address, baudrate, parity, device ID |
| Runtime state | Boot counter, last measurement state, fault flags |
| Event log nhỏ | Power loss, lỗi cảm biến, lỗi giao tiếp, lỗi MAX35103 |

Do dung lượng chỉ **512 byte**, không nên dùng FM24CL04B để lưu log dài. Nên dùng nó cho dữ liệu nhỏ nhưng quan trọng và có tần suất ghi cao.

### 18.1 Phân tầng ownership

| Layer | Trách nhiệm |
|---|---|
| `fram_driver` | Mapping địa chỉ 9 bit, I2C transfer, timeout, range check, WP control |
| `storage_ab_slot` | Header, sequence, CRC, commit-last, chọn slot mới nhất |
| `config_repository` | Serialize/migrate cấu hình, validation range |
| `calibration_repository` | Calibration profile và compatibility metadata |
| `meter_counter_repository` | Persistent volume/counter snapshot |
| `event_repository` | Ring buffer nhỏ, drop/overwrite policy |

Driver không nên hiểu Modbus config, calibration formula hoặc `RuntimeSnapshot`.

---

## 19. Gợi ý memory map có A/B slot

Đây là một memory map tham khảo cho firmware. Kích thước và địa chỉ có thể thay đổi theo thiết kế thực tế.

| Range | Size | Vùng | Nội dung |
|---:|---:|---|---|
| `0x000`-`0x03F` | 64 B | CONFIG A | Header + device/reporting/Modbus config |
| `0x040`-`0x07F` | 64 B | CONFIG B | Bản dự phòng CONFIG |
| `0x080`-`0x0DF` | 96 B | CALIB A | Header + calibration profile compact |
| `0x0E0`-`0x13F` | 96 B | CALIB B | Bản dự phòng CALIB |
| `0x140`-`0x15F` | 32 B | METER A | Forward/reverse volume và counter metadata |
| `0x160`-`0x17F` | 32 B | METER B | Bản dự phòng METER |
| `0x180`-`0x1BF` | 64 B | Runtime/queue metadata | Boot/fault/report queue metadata nhỏ |
| `0x1C0`-`0x1FF` | 64 B | Event ring | Event record nhỏ hoặc reserved |

Tổng cộng đúng 512 byte. Đây là layout khởi đầu; chỉ freeze sau khi kích thước payload, schema migration và alignment/serialization đã được thiết kế.

Không ghi trực tiếp C struct bằng cast vì compiler padding, alignment, endianness và kiểu `bool/enum` có thể thay đổi. Serialize từng field theo byte order được quy định.

### 19.1 Record header gợi ý

```text
offset +0x00  magic          u32
offset +0x04  record_type    u8
offset +0x05  schema_version u8
offset +0x06  payload_length u16
offset +0x08  sequence       u32
offset +0x0C  crc32          u32
offset +0x10  commit         u8
offset +0x11  reserved       ...
```

CRC nên cover các metadata có ý nghĩa và payload, nhưng không cover commit byte nếu commit được ghi riêng cuối cùng.

### 19.2 Atomic A/B commit

```text
1. Scan A và B; chọn slot valid có sequence mới nhất.
2. Chọn slot còn lại làm inactive slot.
3. Ghi commit = INVALID bằng một byte riêng.
4. Đọc lại để chắc chắn slot đã ở trạng thái INVALID.
5. Ghi payload mới.
6. Ghi magic/type/version/length/sequence/CRC.
7. Đọc lại và kiểm tra nội dung + CRC.
8. Ghi commit = VALID bằng một byte riêng cuối cùng.
9. Đọc lại commit; chỉ sau đó publish state mới cho application.
```

Khi boot:

- Kiểm tra bounds của `payload_length` trước khi tính CRC.
- Kiểm tra magic, type, supported schema, commit và CRC.
- Chọn sequence mới nhất bằng phép so sánh wrap-safe.
- Nếu chỉ một slot hợp lệ, dùng slot đó và lên lịch repair slot còn lại.
- Nếu cả hai invalid, dùng factory defaults và phát persistent-storage fault.

Không cần một “active pointer” riêng; sequence + validity của từng slot tránh tạo single point of failure.

Các vùng không có A/B trong layout tham khảo phải là dữ liệu có thể tái tạo, hoặc tự dùng record journal/commit byte riêng. Không ghi đè in-place một struct runtime quan trọng chỉ vì nó nằm ngoài CONFIG/CALIB/METER.

### 19.3 Counter update

Endurance `10^14` rất lớn nhưng counter nhiều byte vẫn cần atomicity. Có thể:

- luân phiên METER A/B theo sequence;
- chỉ persist khi volume delta hoặc thời gian đạt ngưỡng;
- lưu forward/reverse totals trong cùng record để snapshot nhất quán;
- sau boot không cộng lại sample đã được phản ánh trong persistent sequence.

---

## 20. Gợi ý API firmware

```c
typedef enum {
    FRAM_OK = 0,
    FRAM_ERR_I2C,
    FRAM_ERR_ADDR,
    FRAM_ERR_WP,
    FRAM_ERR_TIMEOUT,
    FRAM_ERR_VERIFY,
} fram_status_t;

fram_status_t fram_init(void);
fram_status_t fram_read(uint16_t addr, uint8_t *buf, size_t len);
fram_status_t fram_write(uint16_t addr, const uint8_t *buf, size_t len);
fram_status_t fram_set_write_enabled(bool enabled);  // Nếu WP do MCU điều khiển.

typedef enum {
    STORAGE_OK = 0,
    STORAGE_NOT_FOUND,
    STORAGE_BAD_MAGIC,
    STORAGE_BAD_LENGTH,
    STORAGE_BAD_VERSION,
    STORAGE_BAD_CRC,
    STORAGE_NOT_COMMITTED,
    STORAGE_IO_ERROR,
} storage_status_t;

storage_status_t storage_ab_load(storage_region_t region,
                                 void *payload,
                                 size_t capacity);
storage_status_t storage_ab_commit(storage_region_t region,
                                   const void *payload,
                                   size_t length);
```

API driver chỉ xử lý byte transport và nên nhận địa chỉ logic duy nhất; application không truyền trực tiếp `0x50/0x51`. CRC, schema và A/B commit thuộc storage layer, không thuộc IC driver.

### 20.1 Range check không overflow

```c
static bool fram_range_valid(uint16_t address, size_t length)
{
    if (length == 0u) {
        return true;
    }

    return address < FM24CL04B_SIZE_BYTES &&
           length <= (size_t)(FM24CL04B_SIZE_BYTES - address);
}
```

Không dùng riêng `address + length <= 512` nếu kiểu số có thể overflow.

### 20.2 STM32 HAL read/write theo page

Ví dụ rút gọn dưới đây chủ động chia transaction tại biên 256 byte. Datasheet cho phép một sequential transaction đi xuyên biên, nhưng chia page làm cho mỗi transaction mới luôn có slave/page address rõ ràng.

```c
static size_t fram_page_chunk(uint16_t address, size_t remaining)
{
    size_t until_page_end = 0x100u - (size_t)(address & 0xFFu);
    return remaining < until_page_end ? remaining : until_page_end;
}

fram_status_t fram_write(uint16_t address,
                         const uint8_t *buffer,
                         size_t length)
{
    if ((buffer == NULL && length != 0u) ||
        !fram_range_valid(address, length)) {
        return FRAM_ERR_ADDR;
    }

    while (length != 0u) {
        size_t chunk = fram_page_chunk(address, length);
        uint16_t dev = fm24cl04b_hal_dev_address(address, FRAM_A2, FRAM_A1);
        uint8_t word = fm24cl04b_word_address(address);

        if (HAL_I2C_Mem_Write(&hi2c1, dev, word,
                              I2C_MEMADD_SIZE_8BIT,
                              (uint8_t *)buffer, (uint16_t)chunk,
                              FRAM_I2C_TIMEOUT_MS) != HAL_OK) {
            return FRAM_ERR_I2C;
        }

        address = (uint16_t)(address + chunk);
        buffer += chunk;
        length -= chunk;
    }

    return FRAM_OK;
}
```

`fram_read()` dùng cùng page splitter và `HAL_I2C_Mem_Read`. Không thêm EEPROM write delay hoặc ACK polling sau `HAL_I2C_Mem_Write` thành công.

Ở 1 MHz, STM32 có thể cần bật Fast-mode Plus cho đúng instance/pin và cấu hình analog/digital filter phù hợp. Việc IC hỗ trợ 1 MHz không tự bảo đảm timing của MCU, GPIO và bus đạt yêu cầu.

### 20.3 Concurrency và timeout

- Serialize mọi access trên I2C bus bằng mutex/transaction owner nếu có RTOS.
- Không gọi blocking HAL I2C từ ISR.
- Phân biệt NACK/address error, timeout, bus error và arbitration loss nếu HAL cung cấp đủ thông tin.
- Sau timeout/bus error, chạy bus-recovery policy của platform rồi retry có giới hạn.
- Không tự động retry toàn bộ A/B commit như một opaque write; rescan slot để xác định bước nào đã bền vững.
- Với DMA, buffer phải còn tồn tại đến callback hoàn thành và repository chỉ commit sau khi transfer thật sự thành công.

### Quy tắc nên áp dụng

- Kiểm tra địa chỉ không vượt quá `0x1FF`.
- Chặn range vượt `0x1FF`; không cho public API dựa vào rollover về `0x000`.
- Theo datasheet, internal address tự tăng qua `0x0FF`; mỗi transaction mới vẫn phải tính lại `A0/page`.
- Dùng CRC cho các vùng dữ liệu quan trọng.
- Dùng version field cho cấu trúc dữ liệu để hỗ trợ nâng cấp firmware.
- Với record quan trọng, dùng A/B slot, sequence, CRC, commit-last và readback verification.
- Không coi `HAL_OK` của một multi-byte transfer là application-level atomic commit.

---

## 21. Checklist tích hợp nhanh

- [ ] Xác nhận nguồn cấp FM24CL04B là 3.3 V hoặc trong khoảng 2.7 V đến 3.65 V.
- [ ] Kiểm tra pull-up cho SDA/SCL.
- [ ] Xác định trạng thái chân A1/A2.
- [ ] Xác định địa chỉ I2C page 0/page 1.
- [ ] Kiểm tra xung đột toàn dải hai địa chỉ mà mỗi IC sử dụng.
- [ ] Quyết định WP nối GND, GPIO, hay fail-safe pull-up + open-drain GPIO.
- [ ] Viết driver đọc/ghi theo địa chỉ 9 bit.
- [ ] Kiểm thử single-byte write/read.
- [ ] Kiểm thử multi-byte write/read.
- [ ] Kiểm thử đọc/ghi qua ranh giới `0x0FF` sang `0x100`.
- [ ] Kiểm thử wrap-around hoặc chặn truy cập vượt `0x1FF` trong firmware.
- [ ] Kiểm thử byte cuối được NACK trước STOP khi đọc.
- [ ] Kiểm thử WP: data NACK, address counter không tăng, dữ liệu không đổi.
- [ ] Kiểm thử brownout/reset ở từng bước của A/B commit.
- [ ] Kiểm thử slot A valid/B invalid, A invalid/B valid, cả hai valid và cả hai invalid.
- [ ] Kiểm thử sequence wrap-around và schema version không được hỗ trợ.
- [ ] Kiểm thử CRC lỗi, length vượt slot và commit marker sai.
- [ ] Kiểm thử mutex/timeout/bus recovery.
- [ ] Xác nhận `tPU=1 ms` tính từ lúc VDD vượt 2.7 V.
- [ ] Thiết kế memory map có CRC/version/sequence/commit.

---

## 22. Kết luận

FM24CL04B phù hợp cho dữ liệu nhỏ, quan trọng và ghi thường xuyên trong đồng hồ nước. Driver phải che giấu cách ghép địa chỉ 9 bit, chặn rollover và xử lý timeout I2C. Repository phía trên phải cung cấp A/B slot, CRC, sequence, commit-last và recovery sau brownout. Chỉ khi cả hai lớp này tồn tại thì ưu điểm ghi nhanh và endurance cao của F-RAM mới chuyển thành persistent storage đáng tin cậy cho cấu hình, calibration và meter counters.
