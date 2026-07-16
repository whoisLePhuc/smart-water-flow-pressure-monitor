---
document_id: HW-PRESSURE-001
title: Pressure Measurement Circuit Description
status: DRAFT
version: 0.1
owner: Hardware
last_updated: 2026-07-15
source_schematic: "pressure_zssc324.pdf"
source_sheet: "Measure.SchDoc"
primary_ic: ZSSC3241
related_machine_description: Pressure_Measurement_Circuit.json
---

# Pressure Measurement Circuit Description

## 1. Mục đích tài liệu

Tài liệu này mô tả cấu trúc và nguyên lý hoạt động của mạch đo áp suất sử dụng bộ điều hòa tín hiệu cảm biến **ZSSC3241**. Nội dung được xây dựng từ sơ đồ `pressure_zssc324.pdf` và đối chiếu với datasheet ZSSC3241 của Renesas.

Tài liệu phục vụ các mục đích:

- đọc hiểu và review phần cứng;
- liên kết phần cứng với firmware;
- xây dựng tài liệu kiểm thử và debug;
- cung cấp dữ liệu nguồn cho tệp JSON máy đọc được;
- theo dõi các điểm chưa xác định trong schematic.

> **Trạng thái:** DRAFT. Những kết nối có tên net rõ ràng được xem là đã xác nhận từ schematic. Những đường dây không có tên hoặc linh kiện chưa ghi mã được đánh dấu `NEEDS_VERIFICATION`.

## 2. Tổng quan hệ thống

Mạch nhận tín hiệu vi sai từ một phần tử cảm biến áp suất dạng cầu điện trở, lọc tín hiệu đầu vào và đưa vào ZSSC3241. IC thực hiện khuếch đại, chuyển đổi ADC, bù sai số và tạo kết quả đo đã hiệu chỉnh.

Kết quả có thể được truy cập qua:

- I2C;
- SPI;
- OWI (One-Wire Interface);
- ngõ ra analog;
- vòng dòng công nghiệp 4-20 mA, tùy cấu hình NVM và phần cứng lắp ráp.

Luồng tín hiệu chính:

```text
Pressure sensor bridge
    -> Input RC filter
    -> ZSSC3241 analog front-end and ADC
    -> Digital compensation
    -> I2C / SPI / OWI / analog or 4-20 mA output
```

## 3. Các khối chức năng

### 3.1. Đầu nối cảm biến và lọc đầu vào

Đầu nối `P1` kết nối cảm biến áp suất bốn dây với mạch.

| Chân P1 | Tín hiệu | Chức năng |
| --- | --- | --- |
| 1 | `VDDB` | Nguồn dương cho cầu cảm biến |
| 2 | Sensor positive output | Đi qua `R4` để tạo net `IN_P` |
| 3 | `VSSB` | Mass tham chiếu của cầu cảm biến |
| 4 | Sensor negative output | Đi qua `R6` để tạo net `IN_N` |

Mạch lọc đầu vào gồm:

| Linh kiện | Giá trị | Vai trò dự kiến |
| --- | ---: | --- |
| R4 | 1 kOhm | Hạn dòng và tạo bộ lọc RC cho `IN_P` |
| R6 | 1 kOhm | Hạn dòng và tạo bộ lọc RC cho `IN_N` |
| C6 | 102, tương đương 1 nF | Lọc common-mode từ `IN_P` về `VSSB` |
| C9 | 102, tương đương 1 nF | Lọc common-mode từ `IN_N` về `VSSB` |
| C7 | 0.1 uF | Lọc vi sai giữa `IN_P` và `IN_N` |

Hai nhánh `R4-C6` và `R6-C9` có cấu trúc đối xứng nhằm hạn chế sai lệch do trở kháng đầu vào không cân bằng.

### 3.2. Bộ điều hòa tín hiệu ZSSC3241

`I1` là ZSSC3241 dạng QFN-24. Các chân đang sử dụng trong schematic:

| Chân | Tên | Chức năng trong mạch |
| ---: | --- | --- |
| 1 | `VDDB` | Cấp nguồn cho cầu cảm biến |
| 2 | `INP` | Nhận tín hiệu `IN_P` |
| 3 | `VSSB` | Mass cầu cảm biến |
| 4 | `INN` | Nhận tín hiệu `IN_N` |
| 7 | `RESQ` | Reset mức thấp; được lọc bởi `C10` |
| 9 | `MOSI/SDA` | Dữ liệu SPI hoặc dữ liệu I2C |
| 11 | `SCLK/SCL` | Clock SPI hoặc clock I2C |
| 12 | `MISO` | Dữ liệu SPI từ IC về master |
| 13 | `SS` | Chọn slave SPI |
| 14 | `EOC` | End-of-conversion/interrupt; chưa đưa ra đầu nối trong schematic |
| 15 | `OWI2in` | Đầu vào OWI tùy chọn; chưa thấy kết nối ngoài |
| 16 | `AOUT/OWI1` | Điều khiển ngõ ra analog/vòng dòng hoặc OWI |
| 17 | `FB` | Phản hồi cho mạch vòng dòng |
| 18 | `VSS` | Mass nguồn IC |
| 19 | `VDD` | Nguồn chính của IC |
| 21 | `TEXT` | Ngõ ra kích dòng cho cảm biến nhiệt ngoài; chưa sử dụng |
| 23 | `LDOctrl` | Điều khiển transistor ổn áp ngoài `T1` |

Các chân 5, 6 và 8 là chân test của nhà sản xuất. Các chân 10, 20, 22 và 24 là `n.c.`. Chúng không được sử dụng làm tín hiệu ứng dụng.

### 3.3. Nguồn và ổn áp ngoài

Khối `T1`, `C1`, `C2`, `C3` và `C4` hỗ trợ tạo và ổn định nguồn `VDD` từ đường vòng dòng. Chân `LDOctrl` của ZSSC3241 điều khiển `T1`, phù hợp với topology ổn áp ngoài được Renesas đề xuất cho ứng dụng vòng dòng có điện áp đầu vào cao.

| Linh kiện | Giá trị/loại | Chức năng |
| --- | --- | --- |
| T1 | Chưa ghi mã | Transistor điều chỉnh nguồn ngoài |
| C1 | 10 uF, 10 V | Tụ ổn định/lọc nguồn |
| C2 | 0.1 uF | Bypass cao tần |
| C3 | 10 uF, 10 V | Tụ ổn định/lọc nguồn |
| C4 | 0.1 uF | Bypass cao tần |

Loại transistor `T1`, điện áp danh định trên đường vào và cực tính linh kiện cần được xác nhận bằng BOM hoặc PCB.

### 3.4. Ngõ ra vòng dòng 4-20 mA

Khối `T2`, `R3`, `R5`, `D2` và các linh kiện bù tạo tầng điều khiển dòng vòng.

| Linh kiện | Giá trị/loại | Chức năng dự kiến |
| --- | --- | --- |
| T2 | BJT, chưa ghi mã | Transistor điều chỉnh dòng vòng |
| R5 | 50 Ohm | Điện trở cảm biến dòng `RSENS` |
| R3 | 150 Ohm | Điện trở emitter/ổn định cho transistor vòng dòng |
| R1 | 1 kOhm | Điện trở nối tiếp trong mạng điều khiển/bù |
| R2 | 100 kOhm | Phân cực/xả điện tích cho nút điều khiển |
| C5 | 220 pF | Bù ổn định vòng điều khiển |
| D2 | Chưa ghi loại | Diode bảo vệ giữa miền `VDD` và phản hồi; cần xác nhận cực tính và mã linh kiện |

Theo datasheet, `AOUT/OWI1` tạo tín hiệu điều khiển tỷ lệ với giá trị cảm biến đã hiệu chỉnh. Chân `FB`, điện trở cảm biến dòng và transistor ngoài đóng vòng điều khiển để tạo dòng đầu ra. Giá trị `R5 = 50 Ohm` trùng với giá trị điển hình Renesas đề xuất cho `RSENS`.

Để sử dụng đúng chế độ này, tổng dòng tiêu thụ của ZSSC3241 và cầu cảm biến phải được kiểm soát để không phá vỡ mức thấp 4 mA.

### 3.5. Bảo vệ và đầu nối vòng dòng

`H1` là đầu nối hai chân của đường vòng dòng. `D1`, `F1` và `F2` nằm trên đường ra/vào nhưng schematic chưa ghi mã hoặc giá trị.

| Linh kiện | Trạng thái | Nhận định |
| --- | --- | --- |
| H1 | 2 chân | Đầu nối vòng dòng/nguồn hai dây |
| D1 | Chưa xác định | Có thể dùng bảo vệ ngược cực hoặc tạo đường bypass; cần kiểm tra BOM |
| F1 | Chưa xác định | Có thể là cầu chì, ferrite bead hoặc jumper |
| F2 | Chưa xác định | Có thể là cầu chì, ferrite bead hoặc jumper |

Không nên kết luận điện áp vòng dòng tối đa hoặc khả năng chịu xung cho đến khi có mã linh kiện và layout PCB.

### 3.6. Giao tiếp I2C

Hai đường giao tiếp dùng chung chân với SPI:

- `SDA` nối chân 9;
- `SCL` nối chân 11;
- `R7 = 10 kOhm` kéo `SDA` lên `VDD`;
- `R8 = 10 kOhm` kéo `SCL` lên `VDD`;
- `P2` đưa `SDA`, `SCL` và một chân thứ ba ra ngoài.

Chân 3 của `P2` có vẻ nối mass theo đường dây trong bản vẽ, nhưng nhãn cạnh chân chưa rõ; cần xác nhận bằng file nguồn schematic hoặc PCB.

### 3.7. Giao tiếp SPI

`H2` đưa hai tín hiệu `SS` và `MISO` ra ngoài. Hai tín hiệu còn lại của SPI dùng chung với I2C:

- `MOSI` dùng chân/net `SDA`;
- `SCLK` dùng chân/net `SCL`.

Vì vậy, để dùng SPI, hệ thống ngoài phải truy cập cả `H2` và `P2`, hoặc các net tương ứng phải được nối ở phần mạch khác.

### 3.8. Tụ và test point phía đầu nối số

`C11 = 0.1 uF`, `C12 = 10 uF`, `TP1` và `TP2` nằm gần `P2`. Cách nối trong bản PDF chưa cho phép xác định chắc chắn đây là tụ lọc cho nguồn nào; các nút phía dưới tụ có vẻ đi tới test point trong khi phía trên nối mass.

Khối này được đánh dấu `NEEDS_VERIFICATION` vì có khả năng:

- thiếu nhãn nguồn;
- test point đang đặt ở phía nguồn của tụ nhưng dây hiển thị không rõ;
- hoặc schematic có kết nối chưa hoàn tất.

## 4. Luồng hoạt động

1. Nguồn được đưa vào qua đầu nối vòng dòng `H1`.
2. Khối bảo vệ và ổn áp ngoài tạo nguồn `VDD` cho ZSSC3241.
3. ZSSC3241 tạo nguồn `VDDB` cho cầu cảm biến tại `P1`.
4. pressure_zssc324 làm thay đổi điện trở cầu, tạo điện áp vi sai giữa hai ngõ ra cảm biến.
5. `R4`, `R6`, `C6`, `C7` và `C9` lọc nhiễu trước khi tín hiệu tới `INP/INN`.
6. ZSSC3241 khuếch đại, số hóa và bù offset, độ nhạy, phi tuyến và ảnh hưởng nhiệt độ theo hệ số lưu trong NVM.
7. Kết quả được xuất qua giao diện số hoặc chuyển thành tín hiệu analog/vòng dòng.
8. Trong cấu hình 4-20 mA, `AOUT/OWI1`, `FB`, `R5` và `T2` điều chỉnh dòng chạy trên hai dây của `H1`.

## 5. Giao diện phần cứng dành cho firmware

| Giao diện | Tín hiệu | Ghi chú firmware |
| --- | --- | --- |
| I2C | `SDA`, `SCL` | Cần biết địa chỉ slave được lập trình trong NVM |
| SPI | `MOSI/SDA`, `SCLK/SCL`, `MISO`, `SS` | Không dùng đồng thời với I2C trên cùng chân |
| OWI | `AOUT/OWI1`, tùy chọn `OWI2in` | Có thể hoạt động trên cấu hình vòng dòng |
| EOC/Interrupt | `EOC` | Chưa được route ra đầu nối trong bản vẽ này |
| Reset | `RESQ` | Active-low, có pull-up nội |

Việc chọn giao diện, chế độ khởi động, chu kỳ đo, cấu hình cầu cảm biến và kiểu đầu ra phụ thuộc nội dung NVM của ZSSC3241; schematic không chứa các giá trị cấu hình này.

## 6. Danh sách điểm cần xác minh

| ID | Mức độ | Nội dung cần xác minh |
| --- | --- | --- |
| VERIFY-001 | High | Mã linh kiện và cực tính của `T1`, `T2`, `D1`, `D2` |
| VERIFY-002 | High | Loại và giá trị của `F1`, `F2` |
| VERIFY-003 | High | Pinout chính xác của `H1`, bao gồm `Loop+` và `Loop-` |
| VERIFY-004 | Medium | Chân 3 của `P2` có thực sự là `GND` hay không |
| VERIFY-005 | Medium | Kết nối thực của `C11`, `C12`, `TP1`, `TP2` |
| VERIFY-006 | High | Điện áp vòng dòng đầu vào và công suất/điện áp chịu đựng của linh kiện |
| VERIFY-007 | High | Cấu hình NVM: loại cảm biến, gain, offset, chế độ giao tiếp và `Aout_setup` |
| VERIFY-008 | Medium | Exposed pad chân 25 có được nối đúng theo khuyến nghị PCB hay không |
| VERIFY-009 | Medium | Các chân test 5, 6, 8 có để hở hoặc nối VSS đúng datasheet hay không |
| VERIFY-010 | Medium | Giá trị `C10 = 0.1 uF` trên `RESQ` có đáp ứng yêu cầu thời gian reset khi khởi động hay không |

## 7. Nguồn tham khảo

- Renesas, [ZSSC3241 Datasheet](https://www.renesas.com/en/document/dst/zssc3241-datasheet), revision 2024-02-02.
- Schematic nguồn: `pressure_zssc324.pdf`, trang 1, xuất từ `Measure.SchDoc`, ngày 2026-07-15.

## 8. Quy ước mức độ tin cậy

- `confirmed`: đọc trực tiếp từ nhãn chân/net hoặc giá trị trong schematic.
- `datasheet_supported`: chức năng được xác nhận bằng datasheet nhưng điều kiện cấu hình thực tế chưa biết.
- `inferred`: suy luận từ topology mạch và cần kiểm tra với BOM/PCB.
- `needs_verification`: không đủ thông tin để kết luận an toàn.