---

document_id: HW-PINMAP-STM32L433-001
title: Proposed STM32L433RCT6 Pin Mapping
status: CANDIDATE
version: 0.2
owner: Hardware/Firmware
last_updated: 2026-07-17
mcu: STM32L433RCT6
package: LQFP64
hardware_baseline_status: NEW_BOARD_NOT_YET_FROZEN
related_decisions:

- DEC-HW-007
- DEC-HW-008
- DEC-PWR-001
related_documents:
- 94_linux_to_stm32_porting_plan.md

---

# Proposed STM32L433RCT6 Pin Mapping

## 1. Mục đích và trạng thái

Tài liệu này đề xuất pin mapping cho bo mạch mới sử dụng `STM32L433RCT6`, package `LQFP64`. Đây là đầu vào cho schematic, STM32CubeMX, firmware board manifest và quá trình review xung đột chân.

Mapping này có trạng thái `CANDIDATE`. Nó chưa phải `BOARD_VERIFIED` và chỉ được nâng thành baseline đã phê duyệt sau khi schematic, CubeMX, voltage domain và các đường wake/reset được review trên đúng board revision.

Các mạch trong `1.docs/02_hardware` là tài liệu tham khảo không liên quan được sao chép vào dự án. Chúng không phải pin-map baseline của bo mạch mới.

## 2. Phạm vi và giả định thiết kế

### 2.1. Thành phần nằm trong baseline

* MCU: `STM32L433RCT6`, LQFP64.
* Flow/time measurement: `MAX35103`, SPI1.
* Pressure sensor conditioner: `ZSSC3241`, I2C1, có `EOC` và reset riêng.
* Nonvolatile memory: `FM24CL04B`, dùng chung I2C1 với ZSSC3241.
* BLE: `nRF52810`, UART và handshake đánh thức host.
* Cellular: `EC200U-CN`, UART chính có RTS/CTS và các chân power/control.
* Field bus: half-duplex RS485, dùng LPUART1 và hardware Driver Enable.
* Battery monitoring: ADC đo điện áp pack 18650 1S2P qua cầu chia áp.
* Debug/programming: SWD.
* Low-power time base: LSE 32.768 kHz.

### 2.2. Thành phần không nằm trong baseline

* Segment LCD không được cấp chân trong revision này.
* HSE không được lắp; PH0/PH1 được dùng làm GPIO.
* Không có production service UART riêng.

Nếu LCD được đưa trở lại, phải tạo board/firmware variant và thực hiện pin allocation mới. Không được giả định các chân LCD cũ vẫn được giữ trống.

Revision 0.2 đưa các tín hiệu reliability/diagnostic sau vào baseline:

* `MAX_WDO_N` nối tới `NRST` qua tùy chọn 0 ohm/DNP và có test point.
* `MAX_CMP_OUT_UP_DN` nối PC6 để phục vụ bring-up, HIL và chẩn đoán analog front-end.
* `FM24CL04B_WP` nối PB8, có external pull-up để FRAM mặc định được chống ghi khi MCU reset hoặc chưa khởi tạo.

### 2.3. Giả định nguồn pin

Nguồn là pack hai cell 18650 INR mắc song song, tức `1S2P`. Thông tin `3500 mAh` phải được xác nhận là dung lượng của mỗi cell hay của toàn pack. Nếu mỗi cell là 3500 mAh thì dung lượng danh định của pack là khoảng 7000 mAh; điện áp vẫn là điện áp của một cell.

Pack phải là cụm 1S2P được bảo vệ và ghép bằng các cell đồng nhất theo quy trình phù hợp. Giá trị dung lượng không làm thay đổi pin mapping nhưng ảnh hưởng power profile và thời lượng vận hành.

## 3. Nguyên tắc phân bổ ngoại vi

| Ngoại vi             | Binding                                                                  | Lý do lựa chọn                                                                                                                        |
| -------------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------- |
| RS485                | LPUART1 trên PA2/PA3, DE trên PB12                                       | LPUART1 có thể đánh thức STM32 từ STOP 2; PB12 có alternate function `LPUART1_RTS_DE`, cho phép phần cứng tự điều khiển hướng truyền. |
| BLE nRF52810         | USART1 trên PA9/PA10                                                     | BLE đánh thức STM32 bằng `BLE_HOST_WAKE` trên PC13 và đợi `BLE_MCU_READY`, do đó không cần chiếm LPUART1.                             |
| EC200U               | USART3 trên PC10/PC11, flow control PB13/PB14                            | Tạo một UART canonical có TX/RX/CTS/RTS, không xung đột SPI1, I2C1, SWD hoặc LSE.                                                     |
| MAX35103             | SPI1 trên PA5/PA6/PA7; CE PA4; INT PC4; reset PC5; CMP PC6; WDO tới NRST | Nhóm chân SPI1 liền nhau; INT phục vụ measurement event, CMP phục vụ chẩn đoán và WDO cung cấp watchdog độc lập với STM32.            |
| ZSSC3241 + FM24CL04B | I2C1 trên PB6/PB7; FRAM WP trên PB8                                      | Dùng chung bus theo quyết định kiến trúc; ZSSC có EOC/reset riêng và FRAM mặc định chống ghi nhờ WP pull-up.                          |
| Battery sense        | ADC1_IN5 trên PA0                                                        | PA0 hỗ trợ ADC và không xung đột với các peripheral bắt buộc.                                                                         |
| BLE wake             | PC13/EXTI13                                                              | PC13 phù hợp làm input low-speed/wakeup; không dùng chân này để cấp dòng hoặc drive tải.                                              |
| Low-power clock      | LSE trên PC14/PC15                                                       | Cung cấp RTC/LCD-independent low-power time base và hiệu chỉnh MSI; không cần HSE.                                                    |
| Debug                | SWD trên PA13/PA14                                                       | Giữ programming/debug chuẩn trong khi giải phóng các chân JTAG còn lại cho GPIO dự phòng.                                             |

## 4. Mapping theo khối chức năng

### 4.1. RS485 half-duplex

| Net                            | STM32 | Pin LQFP64 | Alternate function  | Hướng phía MCU | Lý do                                                                 |
| ------------------------------ | ----: | ---------: | ------------------- | -------------- | --------------------------------------------------------------------- |
| `RS485_TX` / U300.DI           |   PA2 |         16 | LPUART1_TX, AF8     | Output         | TX của LPUART1; giữ khả năng wake/sleep architecture đồng nhất.       |
| `RS485_RX` / U300.RO           |   PA3 |         17 | LPUART1_RX, AF8     | Input          | RX của LPUART1; có thể làm nguồn wake từ STOP 2 theo cấu hình LPUART. |
| `RS485_TXDE` / U300.DE + `/RE` |  PB12 |         33 | LPUART1_RTS_DE, AF8 | Output         | Hardware DE tự chuyển receive/transmit, giảm lỗi timing do firmware.  |

Quy ước baseline: DE active-high; DE thấp là receive, DE cao là transmit. Cực tính cuối cùng phải khớp với exact transceiver U300.

### 4.2. BLE nRF52810

| Net             | STM32 | Pin LQFP64 | Chức năng          | Hướng phía MCU | Lý do                                                                |
| --------------- | ----: | ---------: | ------------------ | -------------- | -------------------------------------------------------------------- |
| `BLE_UART_TX`   |   PA9 |         42 | USART1_TX, AF7     | Output         | USART1 được giải phóng khi bỏ LCD; không chiếm LPUART1 của RS485.    |
| `BLE_UART_RX`   |  PA10 |         43 | USART1_RX, AF7     | Input          | Cùng instance USART1 với TX.                                         |
| `BLE_HOST_WAKE` |  PC13 |          2 | GPIO input, EXTI13 | Input          | nRF52810 đánh thức STM32 trước khi gửi dữ liệu UART.                 |
| `BLE_MCU_READY` |   PH0 |          5 | GPIO output        | Output         | STM32 xác nhận đã thức và UART đã sẵn sàng, tránh mất byte đầu tiên. |
| `BLE_RESET_N`   |   PH1 |          6 | GPIO output        | Output         | Reset BLE có kiểm soát; PH1 khả dụng do baseline không dùng HSE.     |

Handshake bắt buộc khi STM32 có thể ở STOP 2:

1. nRF52810 assert `BLE_HOST_WAKE`.
2. STM32 wake, phục hồi clock và USART1.
3. STM32 assert `BLE_MCU_READY`.
4. nRF52810 mới bắt đầu gửi frame UART.

### 4.3. EC200U-CN

| Net                 | STM32 | Pin LQFP64 | Chức năng         | Hướng phía MCU | Lý do                                                            |
| ------------------- | ----: | ---------: | ----------------- | -------------- | ---------------------------------------------------------------- |
| `MODEM_UART_TX`     |  PC10 |         51 | USART3_TX, AF7    | Output         | UART canonical riêng cho modem.                                  |
| `MODEM_UART_RX`     |  PC11 |         52 | USART3_RX, AF7    | Input          | Cùng instance USART3 với TX.                                     |
| `MODEM_CTS`         |  PB13 |         34 | USART3_CTS, AF7   | Input          | Nhận RTS từ modem; cho phép hardware flow control khi modem bận. |
| `MODEM_RTS`         |  PB14 |         35 | USART3_RTS, AF7   | Output         | Điều khiển CTS phía modem.                                       |
| `MODEM_PWRKEY_CTRL` |  PA11 |         44 | GPIO output       | Output         | Điều khiển mạch transistor/open-drain của PWRKEY.                |
| `MODEM_RESET_CTRL`  |  PA12 |         45 | GPIO output       | Output         | Reset modem độc lập với PWRKEY.                                  |
| `MODEM_POWER_EN`    |   PC8 |         39 | GPIO output       | Output         | Điều khiển load switch/regulator cấp nguồn modem.                |
| `MODEM_STATUS`      |   PC9 |         40 | GPIO input        | Input          | Theo dõi trạng thái bật/tắt của modem.                           |
| `MODEM_DTR`         |  PC12 |         53 | GPIO output       | Output         | Hỗ trợ modem sleep/wake policy.                                  |
| `MODEM_RI`          |   PD2 |         54 | GPIO input, EXTI2 | Input          | Nhận ring/unsolicited wake event từ modem.                       |

Main UART và phần lớn digital interface của EC200U thuộc domain 1.8 V. Schematic phải dùng level translation và mạch PWRKEY/RESET phù hợp với tài liệu của Quectel; không nối trực tiếp với GPIO 3.3 V chỉ dựa trên tên net.

### 4.4. MAX35103

| Net                 | STM32 | Pin LQFP64 | Chức năng                  | Hướng phía MCU         | Lý do                                                                                               |
| ------------------- | ----: | ---------: | -------------------------- | ---------------------- | --------------------------------------------------------------------------------------------------- |
| `MAX_CE`            |   PA4 |         20 | GPIO output hoặc SPI1_NSS  | Output                 | Chip-select riêng; cho phép firmware kiểm soát transaction boundary rõ ràng.                        |
| `MAX_SCK`           |   PA5 |         21 | SPI1_SCK, AF5              | Output                 | SPI1 canonical clock.                                                                               |
| `MAX_DOUT`          |   PA6 |         22 | SPI1_MISO, AF5             | Input                  | Dữ liệu MAX35103 về MCU.                                                                            |
| `MAX_DIN`           |   PA7 |         23 | SPI1_MOSI, AF5             | Output                 | Lệnh/dữ liệu từ MCU đến MAX35103.                                                                   |
| `MAX_INT_N`         |   PC4 |         24 | GPIO input, EXTI4          | Input                  | Interrupt active-low giúp tránh polling liên tục.                                                   |
| `MAX_RESET_N`       |   PC5 |         25 | GPIO output                | Output                 | Reset độc lập phục vụ boot/self-check và recovery.                                                  |
| `MAX_CMP_OUT_UP_DN` |   PC6 |         37 | GPIO input, optional EXTI6 | Input                  | Quan sát comparator hoặc hướng phát xung trong bring-up/HIL; chọn chức năng bằng cấu hình MAX35103. |
| `MAX_WDO_N`         |  NRST |          7 | System reset input         | Open-drain từ MAX35103 | Watchdog độc lập; WDO kéo thấp để reset STM32 khi watchdog counter hết hạn.                         |

`MAX_WDO_N` phải đi qua tùy chọn 0 ohm/DNP và có test point để có thể tách khỏi NRST khi bring-up. Vì WDO là active-low open-drain, pull-up và voltage domain phải tương thích với NRST. Sau reset do WDO, firmware đọc watchdog flag của MAX35103 trước khi xóa cờ để ghi nhận reset reason.

`MAX_CMP_OUT_UP_DN` là CMOS output. Schematic phải bảo đảm voltage domain tương thích và nên có series resistor/test point gần MAX35103 hoặc tại điểm đo thuận tiện.

### 4.5. ZSSC3241 và FM24CL04B

| Net            | STM32 | Pin LQFP64 | Chức năng              | Hướng                     | Lý do                                                                                           |
| -------------- | ----: | ---------: | ---------------------- | ------------------------- | ----------------------------------------------------------------------------------------------- |
| `I2C1_SCL`     |   PB6 |         58 | I2C1_SCL, AF4          | Open-drain                | Bus chung cho ZSSC3241 và FRAM.                                                                 |
| `I2C1_SDA`     |   PB7 |         59 | I2C1_SDA, AF4          | Open-drain, bidirectional | Bus chung; giữ topology đơn giản.                                                               |
| `ZSSC_EOC`     |   PB1 |         27 | GPIO input, EXTI1      | Input                     | Nhận end-of-conversion khi NVM profile và timing đã được xác minh.                              |
| `ZSSC_RESET_N` |   PC3 |         11 | GPIO output            | Output                    | Reset/recovery độc lập cho sensor conditioner.                                                  |
| `FRAM_WP`      |   PB8 |         61 | GPIO open-drain output | Output                    | External pull-up làm WP high/protected mặc định; MCU chỉ kéo thấp trong transaction ghi hợp lệ. |

I2C address, command/status format và ZSSC NVM interface profile vẫn phải được ghi vào `ProductVariantManifest`. Pull-up I2C phải nằm trong cùng voltage domain và được chọn theo bus capacitance/speed thực tế.

`FM24CL04B_WP` bảo vệ toàn bộ memory khi ở mức high, không bảo vệ theo vùng. Firmware phải nhả PB8 về high-impedance ngay sau transaction ghi; schematic không được chỉ dựa vào internal pull-down của FRAM vì baseline yêu cầu trạng thái protected-by-default.

### 4.6. Battery, clock, reset và debug

| Net             |     STM32 | Pin LQFP64 | Chức năng    | Lý do                                                                                  |
| --------------- | --------: | ---------: | ------------ | -------------------------------------------------------------------------------------- |
| `BATTERY_SENSE` |       PA0 |         14 | ADC1_IN5     | Đo điện áp toàn pack 1S2P qua cầu chia áp và RC; không nối trực tiếp cell vào ADC.     |
| `LSE_IN`        |      PC14 |          3 | OSC32_IN     | Crystal 32.768 kHz cho RTC/low-power time base.                                        |
| `LSE_OUT`       |      PC15 |          4 | OSC32_OUT    | Cặp với PC14; route ngắn và cân đối theo hướng dẫn crystal.                            |
| `SWDIO`         |      PA13 |         46 | SWDIO        | Programming/debug chuẩn.                                                               |
| `SWCLK`         |      PA14 |         49 | SWCLK        | Programming/debug chuẩn.                                                               |
| `NRST`          |      NRST |          7 | System reset | Connector SWD, reset supervisor/manual reset và `MAX_WDO_N` qua tùy chọn 0 ohm/DNP.    |
| `BOOT0`         | PH3-BOOT0 |         60 | Boot strap   | Giữ trạng thái boot mặc định xác định; không dùng làm GPIO application trong baseline. |

HSE bị disable. Clock baseline là `LSE + MSI auto-trim + PLL`; PH0 và PH1 được dùng cho BLE handshake/reset.

## 5. Bảng đầy đủ 64 chân LQFP64

Quy ước trạng thái:

* `USED`: chân tín hiệu được firmware/schematic sử dụng.
* `POWER`: chân nguồn/ground bắt buộc, không phải GPIO dự phòng.
* `RESERVED`: giữ cho reset, boot hoặc debug.
* `UNUSED`: chưa cấp chức năng; firmware phải khởi tạo về trạng thái an toàn.

| Pin | Pin name       | Trạng thái | Assignment                          | Lý do hoặc ghi chú                                                                              |
| --: | -------------- | ---------- | ----------------------------------- | ----------------------------------------------------------------------------------------------- |
|   1 | VBAT           | POWER      | Backup-domain supply                | Nếu không có nguồn backup riêng, bind theo power scheme được ST khuyến nghị; không để floating. |
|   2 | PC13           | USED       | `BLE_HOST_WAKE`, EXTI13             | Wake input từ BLE; phù hợp tín hiệu low-speed, không drive tải.                                 |
|   3 | PC14-OSC32_IN  | USED       | `LSE_IN`                            | LSE 32.768 kHz.                                                                                 |
|   4 | PC15-OSC32_OUT | USED       | `LSE_OUT`                           | LSE 32.768 kHz.                                                                                 |
|   5 | PH0-OSC_IN     | USED       | `BLE_MCU_READY`                     | HSE không dùng nên PH0 là GPIO handshake.                                                       |
|   6 | PH1-OSC_OUT    | USED       | `BLE_RESET_N`                       | HSE không dùng nên PH1 là GPIO reset BLE.                                                       |
|   7 | NRST           | RESERVED   | `NRST` + `MAX_WDO_N`                | System reset/SWD; WDO open-drain nối qua tùy chọn 0 ohm/DNP để tạo watchdog độc lập.            |
|   8 | PC0            | UNUSED     | Spare GPIO/ADC-capable              | Từng dành cho LCD; hiện không có owner.                                                         |
|   9 | PC1            | UNUSED     | Spare GPIO/ADC-capable              | Từng dành cho LCD; hiện không có owner.                                                         |
|  10 | PC2            | UNUSED     | Spare GPIO/ADC-capable              | Từng dành cho LCD; hiện không có owner.                                                         |
|  11 | PC3            | USED       | `ZSSC_RESET_N`                      | Reset sensor conditioner.                                                                       |
|  12 | VSSA/VREF-     | POWER      | Analog ground/reference negative    | Route theo analog power scheme, không phải GPIO.                                                |
|  13 | VDDA/VREF+     | POWER      | Analog supply/reference positive    | Cấp nguồn/lọc cho ADC và analog domain theo ST.                                                 |
|  14 | PA0            | USED       | `BATTERY_SENSE`, ADC1_IN5           | Đo điện áp pack qua divider/RC.                                                                 |
|  15 | PA1            | UNUSED     | Spare GPIO/ADC-capable              | Có thể cấp cho analog input hoặc optional interrupt sau review.                                 |
|  16 | PA2            | USED       | `RS485_TX`, LPUART1_TX AF8          | TX RS485; LPUART1 giữ low-power behavior.                                                       |
|  17 | PA3            | USED       | `RS485_RX`, LPUART1_RX AF8          | RX và wake source tiềm năng từ STOP 2.                                                          |
|  18 | VSS            | POWER      | Digital ground                      | Ground bắt buộc.                                                                                |
|  19 | VDD            | POWER      | Digital supply                      | 3.3 V regulated rail, decouple tại pin.                                                         |
|  20 | PA4            | USED       | `MAX_CE`                            | Chip enable riêng cho MAX35103.                                                                 |
|  21 | PA5            | USED       | `MAX_SCK`, SPI1_SCK AF5             | SPI1 clock.                                                                                     |
|  22 | PA6            | USED       | `MAX_DOUT`, SPI1_MISO AF5           | SPI data về MCU.                                                                                |
|  23 | PA7            | USED       | `MAX_DIN`, SPI1_MOSI AF5            | SPI data đến MAX35103.                                                                          |
|  24 | PC4            | USED       | `MAX_INT_N`, EXTI4                  | MAX35103 interrupt active-low.                                                                  |
|  25 | PC5            | USED       | `MAX_RESET_N`                       | MAX35103 reset/recovery.                                                                        |
|  26 | PB0            | UNUSED     | Spare GPIO/ADC-capable              | Không có owner trong baseline.                                                                  |
|  27 | PB1            | USED       | `ZSSC_EOC`, EXTI1                   | End-of-conversion input.                                                                        |
|  28 | PB2            | UNUSED     | Spare GPIO                          | LCD_VLCD không còn dùng.                                                                        |
|  29 | PB10           | UNUSED     | Spare GPIO                          | Không có owner; có thể dùng cho optional feature sau review AF.                                 |
|  30 | PB11           | UNUSED     | Spare GPIO                          | Không có owner; không dùng LPUART1 trên pin này.                                                |
|  31 | VSS            | POWER      | Digital ground                      | Ground bắt buộc.                                                                                |
|  32 | VDD            | POWER      | Digital supply                      | 3.3 V regulated rail, decouple tại pin.                                                         |
|  33 | PB12           | USED       | `RS485_TXDE`, LPUART1_RTS_DE AF8    | Hardware direction control cho half-duplex RS485.                                               |
|  34 | PB13           | USED       | `MODEM_CTS`, USART3_CTS AF7         | Hardware flow control input.                                                                    |
|  35 | PB14           | USED       | `MODEM_RTS`, USART3_RTS AF7         | Hardware flow control output.                                                                   |
|  36 | PB15           | UNUSED     | Spare GPIO                          | Không có owner trong baseline.                                                                  |
|  37 | PC6            | USED       | `MAX_CMP_OUT_UP_DN`, optional EXTI6 | Diagnostic input cho comparator hoặc hướng phát xung của MAX35103.                              |
|  38 | PC7            | UNUSED     | Spare GPIO/timer-capable            | Không có owner trong baseline.                                                                  |
|  39 | PC8            | USED       | `MODEM_POWER_EN`                    | Điều khiển power switch/regulator của modem.                                                    |
|  40 | PC9            | USED       | `MODEM_STATUS`                      | Theo dõi trạng thái modem.                                                                      |
|  41 | PA8            | UNUSED     | Spare GPIO/MCO-capable              | Không còn LCD_COM0; có thể dùng test clock hoặc optional GPIO sau review.                       |
|  42 | PA9            | USED       | `BLE_UART_TX`, USART1_TX AF7        | UART từ STM32 đến nRF52810.                                                                     |
|  43 | PA10           | USED       | `BLE_UART_RX`, USART1_RX AF7        | UART từ nRF52810 đến STM32.                                                                     |
|  44 | PA11           | USED       | `MODEM_PWRKEY_CTRL`                 | Điều khiển mạch PWRKEY, không nối thẳng nếu domain không tương thích.                           |
|  45 | PA12           | USED       | `MODEM_RESET_CTRL`                  | Reset modem qua mạch interface phù hợp.                                                         |
|  46 | PA13           | RESERVED   | `SWDIO`                             | Giữ cho programming/debug.                                                                      |
|  47 | VSS            | POWER      | Digital ground                      | Ground bắt buộc.                                                                                |
|  48 | VDDUSB         | POWER      | USB-domain supply                   | USB không dùng nhưng pin nguồn phải bind đúng ST power scheme; không để floating.               |
|  49 | PA14           | RESERVED   | `SWCLK`                             | Giữ cho programming/debug.                                                                      |
|  50 | PA15           | UNUSED     | Spare GPIO                          | Pin JTAG sau reset; chỉ dùng như GPIO khi SYS debug là Serial Wire.                             |
|  51 | PC10           | USED       | `MODEM_UART_TX`, USART3_TX AF7      | Main UART TX đến modem.                                                                         |
|  52 | PC11           | USED       | `MODEM_UART_RX`, USART3_RX AF7      | Main UART RX từ modem.                                                                          |
|  53 | PC12           | USED       | `MODEM_DTR`                         | Modem sleep/wake control.                                                                       |
|  54 | PD2            | USED       | `MODEM_RI`, EXTI2                   | Modem ring/unsolicited wake input.                                                              |
|  55 | PB3            | UNUSED     | Spare GPIO                          | JTAG/SWO sau reset; chỉ dùng khi debug mode phù hợp.                                            |
|  56 | PB4            | UNUSED     | Spare GPIO                          | JTAG reset function sau reset; chỉ dùng khi debug mode phù hợp.                                 |
|  57 | PB5            | UNUSED     | Spare GPIO                          | Không có owner trong baseline.                                                                  |
|  58 | PB6            | USED       | `I2C1_SCL`, AF4                     | Shared ZSSC3241/FRAM clock.                                                                     |
|  59 | PB7            | USED       | `I2C1_SDA`, AF4                     | Shared ZSSC3241/FRAM data.                                                                      |
|  60 | PH3-BOOT0      | RESERVED   | `BOOT0` strap                       | Giữ boot mode xác định; không cấp chức năng application.                                        |
|  61 | PB8            | USED       | `FRAM_WP`, GPIO open-drain          | WP mặc định high/protected bằng external pull-up; kéo low chỉ trong transaction ghi.            |
|  62 | PB9            | UNUSED     | Spare GPIO                          | Không còn LCD_COM3; có thể dùng optional GPIO/I2C alternate sau review.                         |
|  63 | VSS            | POWER      | Digital ground                      | Ground bắt buộc.                                                                                |
|  64 | VDD            | POWER      | Digital supply                      | 3.3 V regulated rail, decouple tại pin.                                                         |

## 6. Danh sách chân chưa dùng

### 6.1. GPIO hoàn toàn chưa có owner

```text
PA1, PA8, PA15
PB0, PB2, PB3, PB4, PB5, PB9, PB10, PB11, PB15
PC0, PC1, PC2, PC7
```

Tổng cộng: 16 GPIO chưa được cấp chức năng.

### 6.2. Ràng buộc của nhóm UNUSED

* PA15, PB3 và PB4 liên quan JTAG sau reset. CubeMX phải đặt `SYS Debug = Serial Wire` trước khi dùng chúng làm GPIO.
* Các chân chưa dùng không được xem là đã dành cho LCD. Việc thêm LCD về sau cần một pin-map revision mới.
* Firmware nên cấu hình chân chưa dùng ở analog/no-pull để giảm leakage, trừ các chân có pull ngoài hoặc yêu cầu EMC/boot cụ thể.
* Chỉ đưa chân dự phòng ra test point/connector khi đã review ESD, stub, leakage và khả năng bị drive lúc MCU chưa cấp nguồn.
* Không dùng PC13 để drive LED hoặc tải dòng lớn; pin này được giữ làm wake input.

### 6.3. Gợi ý ưu tiên khi cần mở rộng

| Nhu cầu tương lai        | Chân candidate          | Ghi chú                                                                    |
| ------------------------ | ----------------------- | -------------------------------------------------------------------------- |
| Service/debug GPIO       | PA8, PB9, PB10          | Ưu tiên chân không ảnh hưởng JTAG và analog measurement.                   |
| Optional interrupt input | PC7, PB5, PB15          | Phải kiểm tra EXTI line không xung đột với pin đã dùng.                    |
| Optional ADC input       | PA1, PB0, PC0, PC1, PC2 | Phải kiểm tra channel availability, source impedance và ADC sampling time. |
| Future display           | Tạo variant mới         | Không restore âm thầm mapping LCD cũ vì PA9/PA10/PB12 đã có owner mới.     |

## 7. Interrupt và wake allocation

| EXTI/wake source | Pin                    | Owner    | Mục đích                                             |
| ---------------- | ---------------------- | -------- | ---------------------------------------------------- |
| EXTI1            | PB1                    | ZSSC3241 | End-of-conversion.                                   |
| EXTI2            | PD2                    | EC200U   | Ring/unsolicited modem event.                        |
| EXTI4            | PC4                    | MAX35103 | Measurement/event interrupt.                         |
| EXTI6            | PC6                    | MAX35103 | Optional diagnostic edge capture từ `CMP_OUT/UP_DN`. |
| EXTI13           | PC13                   | nRF52810 | BLE host wake.                                       |
| LPUART1 RX wake  | PA3                    | RS485    | Wake từ bus theo mode được firmware cấu hình.        |
| RTC/LSE wake     | PC14/PC15 clock domain | System   | Alarm/scheduled wake.                                |

Không có EXTI line collision trong mapping hiện tại.

## 8. Trạng thái GPIO an toàn khi reset/boot

| Tín hiệu            | Trạng thái an toàn yêu cầu                | Cơ chế schematic/firmware                                                               |
| ------------------- | ----------------------------------------- | --------------------------------------------------------------------------------------- |
| `RS485_TXDE`        | Low/receive                               | Pull-down phù hợp với leakage và transceiver; firmware chỉ enable DE trong transaction. |
| `MODEM_POWER_EN`    | Modem off                                 | Pull xác định tại load switch/regulator; không phụ thuộc trạng thái GPIO lúc reset.     |
| `MODEM_PWRKEY_CTRL` | Inactive                                  | Interface transistor/open-drain có default inactive.                                    |
| `MODEM_RESET_CTRL`  | Inactive hoặc theo power sequence đã chốt | Pull và polarity theo Quectel hardware design.                                          |
| `BLE_RESET_N`       | Trạng thái xác định                       | Pull ngoài bảo đảm BLE không reset ngẫu nhiên khi STM32 high-impedance.                 |
| `MAX_RESET_N`       | Trạng thái xác định                       | Pull theo MAX35103 datasheet; firmware release reset sau khi power/clock ổn định.       |
| `MAX_WDO_N`         | High-impedance khi watchdog bình thường   | External pull-up trên NRST; nối qua 0 ohm/DNP và có test point để cô lập khi bring-up.  |
| `MAX_CMP_OUT_UP_DN` | PC6 input, không được drive ngược         | Cấu hình GPIO input; function comparator/direction do register MAX35103 quyết định.     |
| `ZSSC_RESET_N`      | Trạng thái xác định                       | Pull theo ZSSC3241 datasheet/profile.                                                   |
| `MAX_CE`            | Inactive                                  | Pull/default level tránh SPI transaction giả trong boot.                                |
| `FRAM_WP`           | High/protected                            | External pull-up giữ WP high; PB8 open-drain chỉ kéo low trong transaction ghi.         |

## 9. Ràng buộc schematic cho RS485

Sơ đồ tham khảo có `RO`, `/RE`, `DE`, `DI`, nguồn `+3.3_485`, ground `GND_485`, termination 120 ohm, bias và TVS. Trước khi schematic freeze phải xử lý các điểm sau:

1. Chốt exact part number của U300, pinout, logic threshold, fail-safe behavior, common-mode range và DE polarity.
2. Nếu `+3.3_485/GND_485` là domain cách ly, RX/TX/DE không được nối trực tiếp tới MCU; phải có digital isolation và nguồn cách ly tương ứng. Nếu không cách ly, ground phải có chung tham chiếu rõ ràng.
3. Điện trở termination 120 ohm chỉ được populate ở hai đầu vật lý của bus; nên có DNP/jumper option theo board role.
4. Mạng bias 10 kohm cùng termination 120 ohm chỉ tạo differential bias xấp xỉ 20 mV. Không được coi là đủ nếu U300 không có internal fail-safe; phải tính lại theo datasheet và topology thực tế.
5. Chọn TVS chuyên dụng cho RS485 và review return path/placement gần connector.
6. Nếu rail RS485 bị power-gate, phải ngăn back-power qua TX/RX/DE và xác định trạng thái bus khi MCU hoặc transceiver mất nguồn.

## 10. CubeMX baseline đề xuất

```text
RCC:
  HSE = Disabled
  LSE = Crystal/Ceramic Resonator
  MSI = Enabled, auto-trim from LSE
  PLL source = MSI

SYS:
  Debug = Serial Wire

LPUART1:
  Mode = RS485 / asynchronous with Driver Enable
  TX = PA2 AF8
  RX = PA3 AF8
  DE = PB12 AF8
  DE polarity = High
  Stop 2 wakeup = enabled according to protocol

USART1:
  Mode = Asynchronous
  TX = PA9 AF7
  RX = PA10 AF7

USART3:
  Mode = Asynchronous with RTS/CTS
  TX = PC10 AF7
  RX = PC11 AF7
  CTS = PB13 AF7
  RTS = PB14 AF7

SPI1:
  SCK = PA5 AF5
  MISO = PA6 AF5
  MOSI = PA7 AF5
  MAX_CE = PA4 GPIO output

I2C1:
  SCL = PB6 AF4
  SDA = PB7 AF4

ADC1:
  BATTERY_SENSE = PA0 / ADC1_IN5

GPIO reliability/diagnostic:
  MAX_CMP_OUT_UP_DN = PC6 input, optional EXTI6
  FRAM_WP = PB8 open-drain output, external pull-up, default released/high

Reset tree:
  MAX_WDO_N = NRST through 0-ohm/DNP option
```

## 11. ProductVariantManifest fields bắt buộc

```text
mcu.part = STM32L433RCT6
mcu.package = LQFP64
clock.hse = absent
clock.lse = 32768_Hz_crystal
debug.interface = SWD

rs485.uart = LPUART1
rs485.tx = PA2
rs485.rx = PA3
rs485.de = PB12
rs485.de_active = high
rs485.transceiver = NEEDS_VERIFICATION
rs485.isolation = NEEDS_VERIFICATION
rs485.termination_role = NEEDS_VERIFICATION

ble.uart = USART1
ble.tx = PA9
ble.rx = PA10
ble.host_wake = PC13
ble.mcu_ready = PH0
ble.reset_n = PH1

modem.uart = USART3
modem.level_domain = 1V8
modem.variant = EC200U_CN_NEEDS_FREEZE

max35103.spi = SPI1
max35103.wdo_n = NRST_VIA_0R_DNP_OPTION
max35103.wdo_action = RESET_MCU
max35103.cmp_out_up_dn = PC6
max35103.cmp_exti = EXTI6_OPTIONAL
zssc3241.i2c = I2C1
fram.i2c = I2C1
fram.wp = PB8
fram.wp_active = high
fram.wp_default = protected_by_external_pullup
zssc3241.address_profile = NEEDS_VERIFICATION
zssc3241.nvm_interface_profile = NEEDS_VERIFICATION

battery.topology = 1S2P
battery.capacity_definition = NEEDS_VERIFICATION
battery.adc = PA0_ADC1_IN5

display.segment_lcd = absent
```

## 12. Điều kiện nâng trạng thái thành APPROVED

Pin mapping chỉ được nâng từ `CANDIDATE` thành `APPROVED` khi toàn bộ điều kiện sau đạt:

1. CubeMX mở đúng `STM32L433RCT6`, không có pin/peripheral conflict.
2. Schematic dùng đúng physical pin number LQFP64 và pass ERC.
3. EC200U 1.8 V translation, PWRKEY, RESET, STATUS, DTR và RI được review theo exact module revision.
4. U300 RS485 exact part, isolation policy, termination role và bias được chốt.
5. Shared I2C voltage domain, pull-up, ZSSC address và NVM profile được xác minh.
6. Default states của DE/reset/power-enable/FRAM_WP an toàn trong reset, bootloader và STOP 2; WDO-to-NRST không tạo reset loop.
7. BLE handshake được chốt để USART1 không mất byte khi MCU thức từ STOP 2.
8. Pin map trong schematic, CubeMX, firmware board header và `ProductVariantManifest` giống nhau.
9. PCB bring-up xác minh SWD, LSE, SPI1, I2C1, ba UART, EXTI và battery ADC.

Cho tới khi hoàn tất mục 1–8, firmware có thể dùng mapping này ở trạng thái `PRE_PCB_IMPLEMENTED`; không được ghi `BOARD_VERIFIED`.

## 13. Tài liệu tham chiếu chính

* STMicroelectronics, STM32L433xx datasheet: https://www.st.com/resource/en/datasheet/stm32l433cc.pdf
* Analog Devices, MAX35103 datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf
* Renesas, ZSSC3241 datasheet: https://www.renesas.com/en/document/dst/zssc3241-datasheet
* Infineon, Designing with I2C F-RAM application note: https://www.infineon.com/assets/row/public/documents/10/42/infineon-an96578-designing-with-i2c-f-ramtm-applicationnote-en.pdf
* Quectel, EC200U Series Hardware Design: https://forums.quectel.com/uploads/short-url/j0qEXlvPl25PfUDBf4QEkc9AQyx.pdf
