---
document_id: HW-DATALOGGER-4G-001
title: Datalogger 4G Circuit Description
status: DRAFT
version: 0.1
owner: Hardware
last_updated: 2026-07-15
source_schematic: dataloger_4G.pdf
primary_mcu: STM32L433RCT6
cellular_module: EC200U
related_machine_description: datalogger_4G_Circuit.json
---

# Datalogger 4G Circuit Description

## 1. Mục đích

Tài liệu mô tả kiến trúc phần cứng, các khối chức năng, đường nguồn, giao diện, luồng tín hiệu và các vấn đề cần xác minh của bo mạch datalogger 4G sử dụng `STM32L433RCT6` và `EC200U`.

Nội dung được xây dựng từ schematic `Dataloger_4G.pdf`, gồm 8 trang, và được đối chiếu với tài liệu kỹ thuật của các nhà sản xuất IC chính.

> **Trạng thái:** DRAFT. Các kết luận được phân loại thành `confirmed`, `datasheet_supported`, `inferred` hoặc `needs_verification`.

## 2. Tổng quan hệ thống

Thiết bị có nhiệm vụ thu thập tín hiệu từ cảm biến và các đầu vào hiện trường, lưu dữ liệu cục bộ, sau đó truyền dữ liệu qua mạng di động.

Các chức năng chính:

- nguồn đầu vào danh định 12 V;
- sạc và bảo vệ pin Li-ion 1 cell;
- MCU STM32L433;
- modem 4G EC200U;
- GNSS với khả năng cấp nguồn antenna chủ động;
- hai kênh đầu vào dòng 4-20 mA cho cảm biến áp suất;
- RS485 half-duplex;
- flash SPI 64 Mbit;
- các đầu vào số và công tắc từ;
- giao diện BLE mở rộng;
- debug UART và SWD.

Luồng năng lượng và dữ liệu chính:

```text
12 V input
  -> reverse-polarity protection
  -> MP2615C charger
  -> 1-cell Li-ion battery
      -> 3.3 V logic rail
      -> switched V_GSM rail
      -> switched boost converter -> V_Ap sensor rail

Pressure / digital / RS485 inputs
  -> filtering and interface circuits
  -> STM32L433
  -> local SPI flash and/or EC200U
  -> cellular network
```

## 3. Phân chia schematic

| Trang | Sheet | Nội dung |
| ---: | --- | --- |
| 1 | `0Block.SchDoc` | Sơ đồ phân cấp toàn hệ thống |
| 2 | `1.Power.SchDoc` | Nguồn, sạc pin, bảo vệ pin và power switching |
| 3 | `2_MCU.SchDoc` | STM32L433, crystal, SWD, debug và flash SPI |
| 4 | `3.Peripheral.SchDoc` | RS485, analog input, digital input, LED và BLE |
| 5 | `4.EC200_4G.SchDoc` | EC200U, UART, SIM/eSIM, PWRKEY, reset và antenna |
| 6-7 | PCB print | Hai mặt/lớp hiển thị PCB |
| 8 | BOM | Danh sách vật tư |

## 4. Khối nguồn

### 4.1. Nguồn 12 V đầu vào

Nguồn `V_in` đi qua MOSFET P-channel `F100` để chống ngược cực và tạo đường `+12`. `C100 = 1000 uF/16 V` là tụ bulk đầu vào.

Điểm cần xác minh:

- chưa thấy cầu chì hoặc PTC trên đầu vào;
- chưa thấy TVS chống surge/EFT;
- tụ 16 V có biên điện áp thấp nếu đường 12 V chịu xung ngoài hiện trường;
- cần xác định dòng cực đại và rating connector.

### 4.2. Sạc pin

`I101 = MP2615C` là bộ sạc chuyển mạch cho pin Li-ion. Schematic thể hiện cấu hình pin một cell, phù hợp với đường `V_Bat`, `+4.2V`, IC bảo vệ `DW01` và MOSFET kép `FS8205A`.

Các tín hiệu trạng thái:

- `CHG`: trạng thái sạc;
- `ACOK`: phát hiện nguồn sạc hợp lệ.

`R108` là điện trở cảm biến dòng sạc. Schematic ghi `0.05 Ohm`, trong khi manufacturer part number trong BOM có vẻ là linh kiện `0.025 Ohm`. Đây là sai khác nghiêm trọng vì có thể làm dòng sạc thực tế thay đổi gần hai lần.

### 4.3. Bảo vệ pin

`I100 = DW01` kết hợp với `F101 = FS8205A` bảo vệ pin một cell khỏi:

- quá sạc;
- quá xả;
- quá dòng/ngắn mạch.

`H107` là đầu nối pin. Cần xác minh cực tính và pinout trên PCB.

### 4.4. Nguồn 3.3 V

`U100 = TPS7A0333PDBVR` tạo nguồn `3.3V` từ `V_Bat`. Nguồn này cấp cho MCU, flash, logic và các giao diện số.

Các tụ đầu vào/đầu ra đã được bố trí trong schematic. Cần kiểm tra tổng dòng tải so với khả năng của LDO và nhiệt độ vận hành.

### 4.5. Nguồn modem V_GSM

Nguồn pin được đóng/cắt qua MOSFET `F106`, điều khiển bởi `ON/OFF_SIM`, để tạo `V_GSM`.

EC200U yêu cầu nguồn không được tụt dưới giới hạn vận hành trong burst truyền dữ liệu. Thiết kế có nhiều tụ 100 uF phân bố trên đường `V_GSM`, nhưng vẫn phải đo điện áp tại chân module trong các điều kiện:

- đăng ký mạng;
- phát ở công suất cao;
- pin gần hết;
- nhiệt độ thấp;
- cáp/connector pin có điện trở cao.

### 4.6. Nguồn cảm biến V_Ap

`I102 = TPS61085` tạo đường boost `V_Ap`. Nguồn vào boost là `V_in_boot`, được đóng/cắt bằng tín hiệu `ON_BOOT`.

Với `R128 = 100 kOhm` và `R129 = 11.5 kOhm`, đầu ra được thiết kế xấp xỉ 12 V. `C124 = 470 uF/16 V` cung cấp năng lượng dự trữ cho tải cảm biến.

## 5. MCU STM32L433

`U200 = STM32L433RCT6`, package LQFP64, là bộ điều khiển trung tâm.

Các nhóm chức năng:

| Chức năng | Tín hiệu chính |
| --- | --- |
| Modem 4G | `SIM_TX`, `SIM_RX`, `SIM_DTR`, `SIM_RI`, `PWRKEY`, `RESET_SIM`, `ON/OFF_SIM` |
| RS485 | `TX_485`, `RX_485`, `NET485IO`, `ON/OFF_RS485` |
| Analog pressure | `Signal_Ap_1`, `Signal_Ap_2` |
| SPI flash | `CS`, `MOSI`, `MISO`, `SCK`, `HOLD/RESET`, `ON_PW_FLASH_1` |
| Power management | `ON_PW`, `ON_BOOT`, `Measure_pin`, `ACOK`, `CHG` |
| Digital inputs | `DIR1`, `DIR2`, `IN1`, `IN2`, `DT_CUT` |
| BLE | `WU_BLE`, `Det_BLE` |
| Debug | `SWD`, `SWC`, `TX_DEBUG`, `RX_DEBUG` |

### 5.1. Reset và boot

- `NRST` có pull-up `R200 = 10 kOhm` và `C203 = 0.1 uF`.
- `BOOT0` có pull-down `R201 = 10 kOhm`.

### 5.2. Clock RTC

Crystal `Y200 = 32.768 kHz` dùng `C200 = C201 = 15 pF`. BOM ghi load capacitance của crystal là 12.5 pF. Cần tính lại điện dung tải với điện dung ký sinh PCB và đo sai số RTC.

### 5.3. Decoupling

Có sáu tụ 0.1 uF cho nguồn MCU. Cần kiểm tra thêm:

- tụ bulk 4.7-10 uF đặt gần package;
- tụ riêng cho `VDDA/VREF+`;
- cách tách nguồn analog khỏi nhiễu boost, charger và modem.

## 6. Flash SPI

`U201 = EN25QH64A`, dung lượng 64 Mbit, được MCU truy cập qua SPI.

Các vấn đề cần sửa/xác minh:

1. Gate MOSFET cấp nguồn `F200` không có pull-up/pull-down, nên trạng thái nguồn flash có thể không xác định trong lúc MCU reset.
2. Chân `WP#` của flash đang để hở. Nếu không dùng Quad SPI, nên kéo chân này lên VCC.
3. `HOLD/RESET#` nối với MCU; firmware phải đảm bảo mức logic hợp lệ khi flash hoạt động.
4. Tên component, footprint và library reference trong BOM không đồng nhất hoàn toàn.

## 7. Hai kênh đo áp suất

Hai kênh `Sig_Ap_1` và `Sig_Ap_2` có cấu trúc giống nhau:

```text
Field input
  -> 120 Ohm shunt
  -> 1 kOhm series resistor
  -> 10 kOhm pull-down
  -> 1 uF + 0.1 uF + 15 pF filter
  -> STM32 ADC
```

Điện trở shunt 120 Ohm cho thấy mạch được thiết kế cho cảm biến dòng 4-20 mA.

Điện áp ADC gần đúng:

| Dòng đầu vào | Điện áp ADC gần đúng |
| ---: | ---: |
| 4 mA | 0.43 V |
| 12 mA | 1.30 V |
| 20 mA | 2.16 V |

Khuyến nghị:

- dùng điện trở shunt sai số thấp và hệ số nhiệt nhỏ;
- bổ sung TVS/clamp cho cáp ngoài hiện trường;
- bảo vệ khi đấu nhầm 12/24 V;
- xác định ngưỡng phát hiện hở dây và quá dòng trong firmware.

## 8. RS485

`U300 = SN65HVD485E` tạo giao tiếp RS485 half-duplex. Nguồn `3.3V_RS485` được đóng/cắt bởi `ON/OFF_RS485`.

Các tín hiệu:

- `TX_485`: dữ liệu từ MCU đến driver;
- `RX_485`: dữ liệu từ driver về MCU;
- `NET485IO`: điều khiển hướng truyền;
- `A`, `B`: cặp dây bus.

Mạng bias đang dùng các điện trở 1 kOhm. Schematic không có termination 120 Ohm tiêu chuẩn và không có TVS trên A/B.

Nên bổ sung:

- footprint termination 120 Ohm có jumper;
- TVS chuyên dụng cho RS485;
- tùy chọn common-mode choke;
- chân GND/shield cùng connector bus nếu hệ thống yêu cầu.

## 9. Digital I/O và BLE

Các tín hiệu `DIR1`, `DIR2`, `IN1`, `IN2` và `DT_CUT` có pull-up, điện trở series và tụ 0.1 uF để lọc nhiễu/chống rung.

Các đầu vào này có vẻ phù hợp dry-contact hoặc mức logic 3.3 V. Chưa có bằng chứng chúng chịu được 12/24 V trực tiếp.

`S300` là công tắc từ, có pull-up `R318 = 10 kOhm` và tụ lọc `C308 = 0.1 uF`.

`H302` đưa ra `WU_BLE` và `Det_BLE`; nguồn của module BLE không thể hiện trên connector này và cần xác minh ở bo/module liên quan.

## 10. Modem EC200U

`U400 = EC200U` cung cấp kết nối mạng di động và GNSS.

### 10.1. Nguồn

Các chân `VBAT_RF` và `VBAT_BB` được cấp từ `V_GSM`. Nguồn có tụ bulk và tụ lọc cao tần. Cần kiểm tra đường nguồn dạng star, chiều rộng trace và sụt áp tại module.

### 10.2. UART và chuyển mức

EC200U sử dụng miền logic UART 1.8 V, còn MCU sử dụng 3.3 V. Các transistor `T401-T403` thực hiện chuyển mức cho TX, RX và RI.

### 10.3. PWRKEY và reset

`T404` và `T405` kéo thấp `PWRKEY` và `RESET_N`. Firmware phải tuân thủ thời gian xung trong hardware design của EC200U.

### 10.4. SIM/eSIM

`E400` là footprint MFF2/eSIM. Các đường chính gồm `SIM_VCC`, `SIM_RST`, `SIM_CLK` và `SIM_DATA`.

Các vấn đề:

- `R415 = 10 kOhm` có vẻ kéo `SIM_DATA` xuống GND, trong khi reference design khuyến nghị pull-up về `USIM_VDD`;
- chưa thấy điện trở series 0 Ohm trên DATA/CLK/RST;
- chưa thấy tụ khoảng 33 pF trên ba đường tín hiệu;
- chưa thấy TVS low-capacitance cho SIM.

### 10.5. Antenna

- `F402`: đầu nối antenna chính LTE.
- `F401`: đầu nối antenna GNSS.
- `F400`, `R404`, `L400` và `C402`: mạch bias-tee cấp nguồn cho antenna GNSS chủ động.

Cần kiểm tra trở kháng 50 Ohm, ground-via stitching, khoảng cách khỏi nguồn xung và khả năng chịu ngắn mạch của nguồn antenna.

## 11. PCB

Từ bản in PCB có thể nhận thấy:

- EC200U và MCU được tách thành hai vùng chính;
- connector hiện trường nằm gần mép bo;
- có ground pour và via stitching;
- antenna connector nằm gần mép bo;
- charger, boost và các tải nguồn được bố trí ở nửa dưới bo.

Không thể xác nhận từ PDF:

- stack-up và trở kháng RF;
- độ rộng đường `V_GSM`;
- vòng dòng switching của MP2615C và TPS61085;
- khoảng cách chính xác giữa tụ bulk và chân modem;
- tính liên tục của ground plane;
- creepage/clearance và thermal performance.

## 12. Sai khác schematic và BOM

| ID | Đối tượng | Schematic | BOM/part number | Mức độ |
| --- | --- | --- | --- | --- |
| BOM-001 | R108 | 0.05 Ohm | Part number có vẻ 0.025 Ohm | Critical |
| BOM-002 | C100, C105 | 1000 uF/16 V | Description ghi 330 uF nhưng part number có vẻ 1000 uF | High |
| BOM-003 | D100 | Schottky | Mô tả dòng định mức 500 mA và 1 A không thống nhất | Medium |
| BOM-004 | U201 | EN25QH64A | Library/footprint tham chiếu họ S25FL | High |
| BOM-005 | U300 | SN65HVD485E | Library text có MAX485/SN65HVD72 | Medium |
| BOM-006 | Fxxx | Nhiều linh kiện là MOSFET | Designator `F` dễ bị hiểu là fuse/ferrite | Low |

## 13. Danh sách hành động ưu tiên

### Critical

1. Xác nhận giá trị và part number của `R108`.
2. Kiểm tra và sửa hướng kéo của `R415` trên `SIM_DATA`.
3. Đồng bộ schematic, BOM, footprint và manufacturer part number.

### High

4. Thêm pull resistor cho gate `F200`.
5. Đặt mức xác định cho `WP#` của flash.
6. Thêm bảo vệ đầu vào 12 V.
7. Đo `V_GSM` trong burst truyền dữ liệu.
8. Bổ sung TVS và termination tùy chọn cho RS485.
9. Bổ sung bảo vệ hai đầu vào 4-20 mA.

### Medium

10. Review nguồn `VDDA/VREF+` và decoupling MCU.
11. Tính lại tải crystal 32.768 kHz.
12. Hoàn thiện mạng lọc và ESD cho SIM.
13. Xác định mức điện áp hợp lệ của các digital input.
14. Review layout RF, nguồn xung và đường dòng lớn từ file PCB gốc.

## 14. Nguồn tham khảo

- STMicroelectronics, [STM32L433xx Datasheet](https://www.st.com/resource/en/datasheet/stm32l433cc.pdf).
- STMicroelectronics, [Getting Started with STM32L4 Hardware Development](https://www.st.com/resource/en/application_note/an4555-getting-started-with-stm32l4-series-and-stm32l4-series-hardware-development-stmicroelectronics.pdf).
- Quectel, [EC200U Series Hardware Design](https://forums.quectel.com/uploads/short-url/j0qEXlvPl25PfUDBf4QEkc9AQyx.pdf).
- Monolithic Power Systems, [MP2615C Datasheet](https://www.monolithicpower.com/jp/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MP2615CGQ-Z/).
- Texas Instruments, [TPS61085 Datasheet](https://www.ti.com/lit/gpn/TPS61085).
- ESMT, [EN25QH64A Datasheet](https://www.esmt.com.tw/upload/pdf/ESMT/datasheets/EN25QH64A.pdf).
- Schematic nguồn: `Dataloger_4G.pdf`, 8 trang, xuất ngày 2026-07-15.

## 15. Quy ước mức độ tin cậy

- `confirmed`: đọc trực tiếp từ schematic hoặc BOM.
- `datasheet_supported`: chức năng được datasheet xác nhận.
- `inferred`: suy luận từ topology mạch.
- `needs_verification`: phải xác nhận bằng BOM, PCB, firmware hoặc phép đo.