# MAX35103 driver cho STM32

Thư viện này cung cấp driver điều khiển MAX35103 trong các dự án STM32 sử dụng
STM32 HAL. Driver không gắn với một kiến trúc ứng dụng cụ thể: dự án sử dụng có
thể quản lý instance trong `main.c`, một module board, một module application
hoặc bất kỳ cấu trúc riêng nào.

Driver hỗ trợ:

- reset phần cứng, gửi `INIT` và cấu hình ảnh thanh ghi hoạt động;
- kiểm tra sự hiện diện của MAX35103;
- đo TOF trực tiếp bằng `TOF_DIFF`;
- đọc `WVRUP/WVRDN` và `HIT1..HIT6` của cả hai hướng;
- tự dò profile thu âm theo chiều dài đường ToF và tạo báo cáo kiểm chứng;
- chạy Event Timing và xử lý kết quả qua chân `MAX_INT`;
- đọc T1–T4, tính điện trở RTD và nhiệt độ theo IEC 60751;
- phát hiện timeout, dữ liệu không hợp lệ và completion SPI cũ;
- lưu kết quả TOF và nhiệt độ trong các mailbox độc lập.

Thư viện không ghi configuration flash của MAX35103. Profile được ghi vào các
thanh ghi hoạt động và phải được cấu hình lại sau mỗi lần reset hoặc mất nguồn.

## 1. Phạm vi sử dụng

Phạm vi được hỗ trợ của gói này là:

- vi điều khiển STM32;
- STM32 HAL;
- SPI 4 dây với NSS điều khiển bằng GPIO;
- chân reset phần cứng của MAX35103;
- chân `MAX_INT` nối với GPIO/EXTI của STM32.

`max35103.c/.h` không gọi trực tiếp `HAL_*`, không cố định `hspi1` và không dùng
macro GPIO của một board cụ thể. Việc tách này giúp cùng một driver được dùng
trên nhiều project STM32 có SPI instance và pin mapping khác nhau.

`Max35103Transport` là ranh giới nội bộ giữa logic thiết bị và STM32 HAL
adapter. Nó không quy định cách tổ chức toàn bộ ứng dụng và không yêu cầu một
framework hoặc composition model cụ thể.

## 2. Cấu trúc thư mục

```text
max35103_autocal_package/
├── driver/
│   ├── CMakeLists.txt
│   ├── max35103.c
│   ├── max35103.h
│   ├── max35103_autocal.c
│   ├── max35103_autocal.h
│   ├── max35103_stm32_hal.c
│   └── max35103_stm32_hal.h
├── host_test/
│   ├── CMakeLists.txt
│   └── max35103_autocal_host_test.c
└── test/
    ├── CMakeLists.txt
    ├── max35103_test.c
    └── max35103_test.h
```

Vai trò của từng phần:

| Thành phần | Chức năng |
|---|---|
| `max35103.c/.h` | Giao thức MAX35103, state machine, giải mã TOF/RTD và public API |
| `max35103_autocal.c/.h` | State machine tự dò profile và báo cáo bằng chứng |
| `max35103_stm32_hal.c/.h` | Kết nối driver với STM32 HAL, SPI, NSS, reset và HAL tick |
| `host_test/` | Unit/regression test bằng fake transport trên máy phát triển |
| `test/` | HIL test chạy với STM32 và MAX35103 thật |
| `CMakeLists.txt` | Tạo target core và STM32 HAL adapter |

`host_test/` chỉ phục vụ kiểm thử logic khi phát triển driver. Việc có host test
không có nghĩa thư viện cam kết hỗ trợ Linux hoặc một nền tảng production khác.

## 3. Yêu cầu phần cứng và CubeMX

### 3.1 SPI

Cấu hình SPI:

- master;
- full duplex;
- 8-bit data;
- MSB first;
- SPI mode 1: `CPOL = 0`, `CPHA = 1`;
- software NSS;
- tốc độ SPI không vượt giới hạn trong datasheet MAX35103.

Mức 10 MHz phù hợp với toàn bộ dải điện áp được MAX35103 hỗ trợ. Có thể dùng tốc
độ thấp hơn trong giai đoạn bring-up.

STM32 HAL adapter tự kéo NSS xuống trước và kéo lên sau mỗi transaction. Không
cấu hình hardware NSS cho cùng đường truyền này.

### 3.2 GPIO

- `MAX_NSS`: output push-pull, trạng thái idle là HIGH.
- `MAX_RST`: output push-pull, active-low, trạng thái bình thường là HIGH.
- `MAX_INT`: input có pull-up, EXTI cạnh xuống vì ngõ ra active-low/open-drain.

ISR của `MAX_INT` chỉ nên ghi nhận sự kiện. Không thực hiện chuỗi SPI blocking
dài trong EXTI callback.

### 3.3 Mạch RTD đã kiểm chứng

Profile sau tương ứng với PT100 và điện trở tham chiếu 1 kΩ:

```c
.reference_resistance_milliohm = 1000000U, /* 1 kΩ */
.rtd_nominal_resistance_milliohm = 100000U, /* PT100 */
```

Với PT100 hai dây, điện trở dây dẫn được cộng vào điện trở cảm biến. Nếu yêu cầu
độ chính xác cao, ứng dụng cần hiệu chuẩn hoặc bù điện trở dây ở lớp xử lý dữ
liệu.

## 4. Các đối tượng của driver

Một instance cần ba đối tượng:

```c
static Max35103Stm32HalContext max_hal;
static Max35103Transport max_transport;
static Max35103Driver max_driver;
```

- `Max35103Stm32HalContext` giữ `SPI_HandleTypeDef` và các GPIO của project.
- `Max35103Transport` chứa callback mà core dùng để truy cập STM32 HAL.
- `Max35103Driver` giữ trạng thái, profile, buffer, mailbox và diagnostic
  counter của một MAX35103.

Các đối tượng này có thể được đặt ở bất kỳ module nào do project sử dụng quyết
định. `max_hal` và profile phải còn tồn tại trong toàn bộ thời gian
`max_driver` hoạt động.

## 5. Khởi tạo trong project STM32

Ví dụ dưới đây đặt các instance trực tiếp trong `main.c`:

```c
#include "main.h"
#include "max35103.h"
#include "max35103_stm32_hal.h"
#include "board_max35103_profile.h"

static Max35103Stm32HalContext max_hal;
static Max35103Transport max_transport;
static Max35103Driver max_driver;

static const Max35103Profile max_profile = {
    .profile_id = 1U,
    .profile_version = 1U,

    .event_mode_cmd = MAX35103_CMD_EVTMG2,

    /*
     * Các macro BOARD_* phải chứa register image đã được xác nhận cho đúng
     * transducer và đường âm. Không định nghĩa fallback bằng 0.
     */
    .tof1 = BOARD_MAX35103_TOF1,
    .tof2 = BOARD_MAX35103_TOF2,
    .tof3 = BOARD_MAX35103_TOF3,
    .tof4 = BOARD_MAX35103_TOF4,
    .tof5 = BOARD_MAX35103_TOF5,
    .tof6 = BOARD_MAX35103_TOF6,
    .tof7 = BOARD_MAX35103_TOF7,
    .event_timing_1 = BOARD_MAX35103_EVT_TIMING_1,
    .event_timing_2 = MAX35103_EVT2_TEMP_T1_T3,
    .tof_measurement_delay = BOARD_MAX35103_TOF_MEAS_DELAY,
    .calibration_control = MAX35103_CAL_CTRL_INT_EN,

    .init_timeout_ms = 100U,
    .result_timeout_ms = 200U,
    .halt_timeout_ms = 100U,

    .reference_resistance_milliohm = 1000000U,
    .rtd_nominal_resistance_milliohm = 100000U,
};

static Max35103Status MAX35103_BoardInit(void)
{
    Max35103Status status;

    status = MAX35103_Stm32HalInitTransport(
        &max_hal,
        &hspi1,
        MAX_NSS_GPIO_Port,
        MAX_NSS_Pin,
        MAX_RST_GPIO_Port,
        MAX_RST_Pin,
        &max_transport);
    if (status != MAX35103_OK) {
        return status;
    }

    status = MAX35103_Init(&max_driver, &max_transport);
    if (status != MAX35103_OK) {
        return status;
    }

    status = MAX35103_ResetDevice(&max_driver);
    if (status != MAX35103_OK) {
        return status;
    }

    return MAX35103_Configure(&max_driver, &max_profile);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();

    if (MAX35103_BoardInit() != MAX35103_OK) {
        Error_Handler();
    }

    for (;;) {
        /* Gọi các API đo hoặc xử lý Event Timing tại đây. */
    }
}
```

Các giá trị `tof1..tof7` và Event Timing trong ví dụ chỉ là placeholder. Không
dùng profile toàn số 0 làm cấu hình production nếu chưa đối chiếu datasheet,
transducer, geometry của ống và kết quả bring-up.

Trình tự khởi tạo chuẩn:

```text
Khởi tạo HAL/GPIO/SPI
→ tạo STM32 HAL transport
→ MAX35103_Init()
→ MAX35103_ResetDevice()
→ MAX35103_Configure()
→ đo trực tiếp hoặc MAX35103_StartEventTiming()
```

Sau POR hoặc hardware reset, phải gọi lại `MAX35103_ResetDevice()` và
`MAX35103_Configure()`.

## 6. STM32 HAL adapter

Hàm sau liên kết tài nguyên của project với driver:

```c
Max35103Status MAX35103_Stm32HalInitTransport(
    Max35103Stm32HalContext *context,
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *nss_port,
    uint16_t nss_pin,
    GPIO_TypeDef *reset_port,
    uint16_t reset_pin,
    Max35103Transport *transport);
```

Adapter:

- nhận bất kỳ `SPI_HandleTypeDef` hợp lệ, không cố định `hspi1`;
- điều khiển NSS cho toàn bộ SPI transaction;
- chuyển `HAL_OK`, `HAL_BUSY`, `HAL_TIMEOUT` và `HAL_ERROR` sang transport
  status;
- điều khiển reset active-low;
- dùng `HAL_GetTick()` và `HAL_Delay()` cho các thao tác blocking.

SPI handle, GPIO port và các instance context do project sở hữu và phải còn hợp
lệ khi driver đang hoạt động.

## 7. Profile cấu hình

`Max35103Profile` chứa:

| Trường | Ý nghĩa |
|---|---|
| `profile_id` | Mã nhận dạng profile |
| `profile_version` | Phiên bản profile |
| `event_mode_cmd` | Một trong `MAX35103_CMD_EVTMG1/2/3` |
| `tof1..tof7` | Register image cấu hình phép đo TOF |
| `event_timing_1/2` | Chu kỳ Event Timing và lựa chọn cổng nhiệt độ |
| `tof_measurement_delay` | Delay của phép đo TOF |
| `calibration_control` | Điều khiển calibration, continuous mode và interrupt |
| `init_timeout_ms` | Timeout cho quá trình INIT |
| `result_timeout_ms` | Timeout chờ kết quả TOF/nhiệt độ |
| `halt_timeout_ms` | Timeout chờ HALT hoàn tất |
| `reference_resistance_milliohm` | Điện trở tham chiếu theo mΩ |
| `rtd_nominal_resistance_milliohm` | Điện trở danh định RTD tại 0 °C |

Driver giữ con trỏ tới profile thay vì sao chép toàn bộ profile. Vì vậy profile
không được là biến local hết lifetime sau khi `MAX35103_Configure()` trả về.

Đặt một trong hai giá trị điện trở bằng 0 nếu chỉ muốn lấy timing thô T1–T4 mà
không chuyển đổi sang điện trở/nhiệt độ.

`MAX35103_ValidateProfile()` được gọi trước khi driver ghi thanh ghi. Hàm này
từ chối:

- `event_mode_cmd` không phải `EVTMG1`, `EVTMG2` hoặc `EVTMG3`;
- `PL=0`, vì cấu hình này tắt pulse launcher;
- `DPL=0`, vì datasheet đánh dấu giá trị này là reserved;
- bit reserved khác 0 trong `TOF1..TOF7` hoặc `CAL_CTRL`;
- `tof_measurement_delay < 0x0012`;
- measurement delay dài hơn timeout được chọn trong `TOF2`.

Validation chỉ xác nhận cấu trúc register image. Nó không thể xác nhận tần số
transducer, wave selector, comparator offset hoặc delay có phù hợp với đường âm
thực tế hay không.

`DPL` phải khớp tần số cộng hưởng của transducer:

| Tần số transducer | `DPL` | `TOF1` với 15 pulse, rising edge, `CT=0` |
|---:|---:|---:|
| 1 MHz | 1 | `0x0F10` |
| 500 kHz | 3 | `0x0F30` |
| 250 kHz | 7 | `0x0F70` |

Các giá trị này chỉ minh họa cách mã hóa `TOF1`, không phải profile hoàn chỉnh.
`TOF2..TOF7` và measurement delay phải được hiệu chỉnh theo bo.

## 8. Chức năng các API

### 8.1 Khởi tạo và cấu hình

| API | Chức năng |
|---|---|
| `MAX35103_Init()` | Khởi tạo một driver instance và sao chép transport callback |
| `MAX35103_ResetDevice()` | Pulse chân RST, xác nhận POR, gửi INIT và chờ hoàn tất |
| `MAX35103_ValidateProfile()` | Kiểm tra cấu trúc register image mà không truy cập IC |
| `MAX35103_Configure()` | Ghi và read-verify toàn bộ volatile register image |
| `MAX35103_Probe()` | Kiểm tra presence bằng readback không phá hủy |

SPI không có ACK như I²C, vì vậy `MAX35103_Probe()` là phép kiểm tra dựa trên
readback hợp lý, không phải xác thực chip ID tuyệt đối.

### 8.2 Đo và điều khiển

| API | Chức năng |
|---|---|
| `MAX35103_SelfCheck()` | Thực hiện một phép đo blocking `TOF_DIFF` |
| `MAX35103_MeasureTemperature()` | Đo blocking T1–T4 và chuyển đổi RTD nếu profile cho phép |
| `MAX35103_StartEventTiming()` | Khởi động EVTMG mode đã chọn trong profile |
| `MAX35103_Halt()` | Dừng Event Timing và chờ `HALT_COMPLETE` |
| `MAX35103_Cancel()` | Hủy thao tác đọc phía MCU; không gửi HALT cho IC |

### 8.3 Event Timing và SPI state machine

| API | Chức năng |
|---|---|
| `MAX35103_OnInt()` | Ghi nhận một sự kiện `MAX_INT` và lên lịch đọc `INT_STATUS` |
| `MAX35103_Process()` | Kiểm tra deadline và thực hiện tối đa một SPI request đang chờ |
| `MAX35103_GetPendingSpiRequest()` | Lấy request để project tự thực hiện bằng IRQ/DMA |
| `MAX35103_OnSpiDone()` | Báo completion theo token cho state machine |
| `MAX35103_ExecuteSpi()` | Thực hiện request đang chờ bằng transport blocking |
| `MAX35103_OnTimeout()` | Ép state machine xử lý timeout hiện tại |

`MAX35103_Process()` phù hợp với superloop/cooperative loop. Cặp
`MAX35103_GetPendingSpiRequest()` và `MAX35103_OnSpiDone()` dành cho project
muốn tự nối state machine với SPI IRQ/DMA.

Không để `MAX35103_Process()` và một SPI DMA owner khác cùng thực hiện một
request.

### 8.4 Kết quả và thanh ghi

| API | Chức năng |
|---|---|
| `MAX35103_HasResult()` | Kiểm tra mailbox TOF |
| `MAX35103_GetResult()` | Lấy và consume kết quả TOF |
| `MAX35103_HasTemperatureResult()` | Kiểm tra mailbox nhiệt độ |
| `MAX35103_GetTemperatureResult()` | Lấy và consume kết quả nhiệt độ |
| `MAX35103_ReadResult()` | Đọc blocking một snapshot TOF |
| `MAX35103_ReadReg()` | Đọc một thanh ghi 16 bit |
| `MAX35103_WriteReg()` | Ghi một thanh ghi 16 bit |
| `MAX35103_WriteVerifyReg()` | Ghi rồi đọc lại để kiểm tra |
| `MAX35103_IsBusy()` | Kiểm tra driver đang có thao tác hay không |
| `MAX35103_GetState()` | Lấy state hiện tại |
| `MAX35103_PlatinumRtdToMilliCelsius()` | Chuyển điện trở PT100/PT1000 sang m°C |

## 9. Đo nhiệt độ trực tiếp

```c
Max35103TemperatureResult temperature;
Max35103Status status =
    MAX35103_MeasureTemperature(&max_driver, &temperature);

if (status == MAX35103_OK &&
    temperature.valid &&
    temperature.rtd1_temperature_valid) {
    int32_t temperature_millicelsius =
        temperature.rtd1_temperature_millicelsius;
    uint32_t resistance_milliohm =
        temperature.rtd1_resistance_milliohm;

    /* Sử dụng kết quả tại đây. */
}
```

Các cờ cần kiểm tra:

- `selected_port_mask`: cổng được profile lựa chọn;
- `valid_port_mask`: cổng có timing hợp lệ;
- `open_circuit_mask`: cổng có dấu hiệu hở mạch;
- `short_circuit_mask`: cổng có dấu hiệu ngắn mạch;
- `rtd1_valid`/`rtd2_valid`: tỷ lệ RTD/reference hợp lệ;
- `rtd1_temperature_valid`/`rtd2_temperature_valid`: nhiệt độ đã chuyển đổi
  thành công;
- `valid`: toàn bộ kết quả đáp ứng điều kiện sử dụng.

## 10. Đo TOF trực tiếp

`MAX35103_SelfCheck()` chạy một phép đo `TOF_DIFF` blocking và đưa kết quả vào
mailbox TOF:

```c
Max35103Status status = MAX35103_SelfCheck(&max_driver);

if (status == MAX35103_OK) {
    Max35103RawResult tof;

    if (MAX35103_GetResult(&max_driver, &tof) == MAX35103_OK &&
        tof.valid) {
        /* tof.tof_up_ps, tof.tof_down_ps và tof.tof_diff_ps hợp lệ. */
    }
}
```

Với lệnh `TOF_DIFF` trực tiếp, `valid_cycle_count` có thể bằng 0; bộ đếm này
dành cho các chuỗi Event Timing. `tof_diff_ps == 0` chỉ có thể được diễn giải
khi `valid == true`. Ở Event Timing, consumer nên kiểm tra thêm
`valid_cycle_count > 0`.

Ống cần đầy chất lỏng, hạn chế bọt khí và transducer phải được ghép âm đúng
trước khi dùng HIL TOF để đánh giá profile. Ống khô vẫn có thể cho kết quả nhiệt
độ hợp lệ nhưng phép đo TOF thường không hợp lệ.

## 11. Event Timing qua MAX_INT

### 11.1 Ghi nhận EXTI

Ví dụ tối thiểu dùng một cờ deferred:

```c
static volatile bool max_int_pending;

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == MAX_INT_Pin) {
        max_int_pending = true;
    }
}
```

Không gọi SPI blocking trong callback trên.

### 11.2 Xử lý trong main loop

Project nên cung cấp timestamp monotonic theo microsecond:

```c
static uint64_t Board_MonotonicUs(void);

if (MAX35103_StartEventTiming(&max_driver) != MAX35103_OK) {
    Error_Handler();
}

for (;;) {
    uint64_t now_us = Board_MonotonicUs();

    if (max_int_pending) {
        __disable_irq();
        max_int_pending = false;
        __enable_irq();

        MAX35103_OnInt(&max_driver, now_us);
    }

    (void)MAX35103_Process(&max_driver, now_us);

    if (MAX35103_HasResult(&max_driver)) {
        Max35103RawResult tof;
        if (MAX35103_GetResult(&max_driver, &tof) == MAX35103_OK &&
            tof.valid) {
            /* Xử lý TOF. */
        }
    }

    if (MAX35103_HasTemperatureResult(&max_driver)) {
        Max35103TemperatureResult temperature;
        if (MAX35103_GetTemperatureResult(
                &max_driver, &temperature) == MAX35103_OK &&
            temperature.valid) {
            /* Xử lý nhiệt độ. */
        }
    }
}
```

State machine:

1. ghi nhận `MAX_INT`;
2. đọc `INT_STATUS` đúng một lần vì thanh ghi tự xóa khi đọc;
3. đọc đủ các thanh ghi TOF và/hoặc nhiệt độ được báo sẵn sàng;
4. giải mã một snapshot nhất quán;
5. publish kết quả vào mailbox;
6. trở về trạng thái chờ Event Timing tiếp theo.

Token trong `Max35103SpiRequest` giúp loại completion cũ sau cancel, timeout
hoặc thay đổi generation.

## 12. Tính toán TOF

MAX35103 trả kết quả Q16 theo chu kỳ clock 4 MHz:

$$
T_{Q16}=(T_{INT}\ll16)+T_{FRAC}
$$

Một chu kỳ danh định:

$$
T_{CLK}=\frac{1}{4\,MHz}=250\,ns=250000\,ps
$$

Do đó:

$$
TOF_{ps}=\frac{T_{Q16}\times250000}{65536}
$$

Driver kiểm tra tính nhất quán:

$$
TOF_{DIFF}\approx TOF_{UP}-TOF_{DOWN}
$$

với tolerance một đơn vị Q16.

Các trường kết quả quan trọng:

- `tof_up_q16`, `tof_down_q16`, `tof_diff_q16`: dữ liệu theo Q16;
- `tof_up_ps`, `tof_down_ps`, `tof_diff_ps`: dữ liệu quy đổi sang ps;
- `valid_cycle_count`: số chu kỳ hợp lệ;
- `tof_range`: range được giải mã từ cycle/range word;
- `status_flags`: snapshot cờ interrupt;
- `timestamp_us`: thời điểm ghi nhận `MAX_INT`;
- `valid`: kết luận coherence của kết quả.

Các giá trị ps đang giả định clock chính xác 4 MHz. Nếu hệ thống yêu cầu độ
chính xác cao hơn, lớp xử lý của project phải áp dụng calibration/correction
phù hợp.

## 13. Tính điện trở và nhiệt độ RTD

Với T1 là PT100 và T3 là điện trở tham chiếu:

$$
R_{RTD1}=R_{REF}\frac{T1_{Q16}}{T3_{Q16}}
$$

Tương tự:

$$
R_{RTD2}=R_{REF}\frac{T2_{Q16}}{T4_{Q16}}
$$

Ví dụ HIL đã kiểm chứng:

```text
T1   = 2803517 Q16 cycles
T3   = 25185365 Q16 cycles
RREF = 1000000 mΩ
```

$$
R_{RTD1}=1000000\frac{2803517}{25185365}
\approx111315\,m\Omega=111.315\,\Omega
$$

Driver dùng đặc tuyến platinum IEC 60751:

$$
R(T)=R_0(1+AT+BT^2)
$$

cho nhiệt độ không âm và:

$$
R(T)=R_0[1+AT+BT^2+C(T-100)T^3]
$$

cho nhiệt độ âm, trong đó:

```text
A = 3.9083e-3
B = -5.775e-7
C = -4.183e-12
```

Kết quả tương ứng:

```text
111315 mΩ → 29076 m°C ≈ 29.076 °C
```

## 14. Mailbox và chính sách mất mẫu

Driver có hai mailbox một phần tử:

- mailbox TOF;
- mailbox nhiệt độ.

Chính sách hiện tại là giữ kết quả cũ chưa được lấy và bỏ kết quả mới. Các
counter sau giúp phát hiện consumer xử lý không kịp:

- `dropped_result_count`;
- `dropped_temperature_result_count`.

Gọi `MAX35103_GetResult()` hoặc `MAX35103_GetTemperatureResult()` sẽ consume
kết quả tương ứng. Project nên đọc mailbox đủ nhanh hoặc sao chép kết quả sang
queue riêng nếu cần lưu nhiều mẫu.

## 15. Mã trạng thái

| Status | Ý nghĩa |
|---|---|
| `MAX35103_OK` | Thao tác thành công |
| `MAX35103_BUSY` | Driver đang thực hiện thao tác khác |
| `MAX35103_TIMEOUT` | Quá deadline |
| `MAX35103_INVALID_ARG` | Tham số hoặc callback không hợp lệ |
| `MAX35103_NOT_READY` | Thiết bị chưa reset/configure hoặc thiếu reset callback |
| `MAX35103_SPI_ERROR` | STM32 HAL transport thất bại |
| `MAX35103_DEVICE_ERROR` | POR bất ngờ, cờ lỗi hoặc dữ liệu thiết bị không hợp lệ |
| `MAX35103_CONFIG_ERROR` | Giá trị readback không khớp profile |
| `MAX35103_NO_RESULT` | Mailbox không có kết quả |
| `MAX35103_STALE` | Completion token không còn thuộc request hiện tại |
| `MAX35103_OUT_OF_RANGE` | Điện trở nằm ngoài dải RTD được hỗ trợ |

Khi gặp POR bất ngờ, driver đánh dấu thiết bị chưa sẵn sàng. Project phải thực
hiện lại reset/init/configure trước khi tiếp tục đo.

## 16. Tích hợp bằng CMake

Trong CMake của project STM32:

```cmake
add_subdirectory(path/to/max35103_autocal_package/driver)

target_link_libraries(your_application_target
    PRIVATE
        driver_max35103_core
        driver_max35103_stm32_hal
        service_max35103_autocal
)
```

Không link `service_max35103_autocal` nếu firmware production không chứa chế
độ commissioning; core driver và HAL adapter vẫn hoạt động độc lập.

Target `driver_max35103_stm32_hal` chỉ được tạo khi target `stm32cubemx` đã tồn
tại. Điều này phù hợp với cấu trúc CMake do STM32CubeMX/CubeIDE tạo ra.

Để thêm HIL test:

```cmake
add_subdirectory(path/to/max35103_autocal_package/test)

target_link_libraries(your_application_target
    PRIVATE
        test_max35103
)
```

## 17. Chạy test

### 17.1 Host regression test

Host test kiểm tra logic mà không cần bo STM32:

- cửa sổ vật lý mặc định cho đường ToF 62 mm;
- profile wave hợp lệ và chuỗi wave lặp bị từ chối;
- giải mã `WVRUP/WVRDN` và hit registers;
- toàn bộ state machine tìm kiếm, recovery sau timeout, perturbation;
- reset verification, zero-flow evidence và CRC báo cáo.

```bash
cmake -S host_test -B build/max35103-host
cmake --build build/max35103-host
ctest --test-dir build/max35103-host --output-on-failure
```

### 17.2 HIL test trên STM32

Sau khi tạo transport và profile:

```c
int test_result = MAX35103_Test_RunAll(
    &max_transport,
    &max_profile);
```

HIL test reset và cấu hình lại MAX35103 nhưng không ghi configuration flash.

Kết quả nhiệt độ đã xác nhận:

```text
status=0x0800 selected=0x05 valid=0x05 open=0x00 short=0x00
T1=2803517 T2=0 T3=25185365 T4=0
RTD1=111315 milliohm temperature=29076 millidegC
```

Nếu ống khô hoặc ghép âm không đúng, temperature test vẫn có thể PASS trong khi
TOF trả `valid_cycle_count=0` và `MAX35103_DEVICE_ERROR`. Đây là kết quả TOF
không hợp lệ, không phải zero-flow.

## 18. Checklist trước khi đưa vào project

- SPI mode 1 và tốc độ SPI đã được xác nhận.
- NSS idle HIGH và chỉ do adapter điều khiển trong transaction.
- Reset active-low hoạt động đúng.
- `MAX_INT` có pull-up và EXTI cạnh xuống.
- Register image trong profile phù hợp với phần cứng thực tế.
- Profile có lifetime dài hơn driver.
- Sau reset luôn gọi lại `MAX35103_Configure()`.
- Không đọc `INT_STATUS` từ một module khác vì thanh ghi tự xóa khi đọc.
- EXTI chỉ tạo evidence; SPI được xử lý ngoài ISR.
- Chỉ một owner thực hiện mỗi pending SPI request.
- Consumer lấy mailbox đủ nhanh hoặc chuyển kết quả sang queue riêng.
- Kiểm tra HIL nhiệt độ bằng điện trở/reference đã biết.
- Chỉ đánh giá TOF khi ống đầy chất lỏng và đường truyền âm được lắp đúng.

## 19. Giới hạn hiện tại

- STM32 HAL adapter hiện dùng SPI blocking.
- Driver chưa tự hiệu chỉnh sai số clock 4 MHz.
- Driver chuyển đổi PT100/PT1000 theo IEC 60751 nhưng không tự bù điện trở dây.
- Mailbox chỉ giữ một kết quả cho mỗi loại dữ liệu.
- `MAX35103_Cancel()` không dừng Event Timing trong IC; dùng
  `MAX35103_Halt()` khi cần dừng phần cứng.
- Driver không quản lý queue, storage, telemetry hoặc logic tính lưu lượng của
  project sử dụng.

## 20. Max35103AutoCalibrator

### 20.1 Phạm vi

`Max35103AutoCalibrator` chỉ tự dò **profile thu âm**. Nó không hiệu chuẩn phép
đổi TOF sang lưu lượng và không ghi flash cấu hình của MAX35103.

Các tham số được quét theo từng tầng:

1. `DPL`, số xung phát, `STOP_POL` và `DLY` thô.
2. `CT`.
3. `DLY` tinh.
4. Initial comparator offset của UP và DN, quét thô rồi quét tinh.
5. `T2WV`, số hit và `HIT1..HIT6` liên tiếp.
6. Comparator return offset của UP và DN.
7. Holdout verification.
8. Perturbation test với `DLY`, offset và `T2WV` lân cận.
9. Reset phần cứng, `INIT`, nạp lại profile và verification lần cuối.

Profile chỉ được cấp `ACOUSTIC_VERIFIED` khi vượt toàn bộ physical, waveform,
statistics, perturbation và reset gate. Nếu người vận hành xác nhận chắc chắn
zero-flow, báo cáo có thể đạt `ZERO_FLOW_COMPENSATED` và chứa `b0` tại điều
kiện hiện tại.

### 20.2 Bằng chứng waveform

Driver mới cung cấp:

```c
Max35103WaveEvidence wave;
Max35103Status status =
    MAX35103_ReadWaveEvidence(&max_driver, &wave);
```

Kết quả chứa:

- `WVRUP` và `WVRDN`, tách thành bốn tỷ số Q1.7;
- thời gian Q16 và picosecond của `HIT1..HIT6`;
- số hit thực sự được `TOF2.STOP` cấu hình;
- cờ `valid` xác nhận hit hợp lệ và tăng đơn điệu.

API không đọc `INT_STATUS`, do đó không xóa interrupt evidence. Gọi API sau một
phép `TOF_DIFF` thành công và ngoài ISR.

### 20.3 Cấu hình cho đường ToF 62 mm

```c
#include "max35103_autocal.h"

static Max35103AutoCalibrator g_max_autocal;
static Max35103AutoCalBackend g_max_autocal_backend;
static Max35103AutoCalSample g_max_autocal_samples[128];
static Max35103AutoCalReport g_max_autocal_report;

static Max35103AutoCalStatus MaxAutoCal_Start(void)
{
    Max35103AutoCalConfig config;

    Max35103AutoCalStatus status = MAX35103_AutoCalDefaultConfig(
        &config,
        62000U,   /* One-way acoustic path: 62 mm. */
        1000000U  /* Nominal transducer frequency: 1 MHz. */
    );
    if (status != MAX35103_AUTOCAL_OK) {
        return status;
    }

    /*
     * Chỉ đặt true khi ống đầy nước và đã xác nhận nước đứng yên.
     * Nếu chưa chắc chắn, giữ false.
     */
    config.zero_flow_confirmed = true;

    status = MAX35103_AutoCalBindDriver(
        &max_driver, &g_max_autocal_backend);
    if (status != MAX35103_AUTOCAL_OK) {
        return status;
    }

    status = MAX35103_AutoCalInit(
        &g_max_autocal,
        &g_max_autocal_backend,
        &config,
        &max_profile, /* Seed; không cần là profile production cuối cùng. */
        g_max_autocal_samples,
        (uint16_t)(sizeof(g_max_autocal_samples) /
                   sizeof(g_max_autocal_samples[0])));
    if (status != MAX35103_AUTOCAL_OK) {
        return status;
    }
    return MAX35103_AutoCalStart(&g_max_autocal);
}
```

Default cho `62 mm/1 MHz` tạo:

- cửa sổ TOF vật lý xấp xỉ `37.75–49.29 µs`;
- `DPL=1`;
- quét `PL=8, 12, 16, 20, 24`;
- quét cả hai `STOP_POL`;
- vùng `DLY` tính từ cửa sổ vật lý, sau đó quét tinh 1 tick = 250 ns;
- 16 mẫu cho mỗi ứng viên và 128 mẫu cho verification.

Nếu chỉ muốn chấp nhận đúng cửa sổ đã quyết định trước đó:

```c
config.expected_min_tof_ps = 38000000LL;
config.expected_max_tof_ps = 50000000LL;
config.dly_min = 0x0070U;
config.dly_max = 0x00B8U;
```

### 20.4 Chạy trong superloop

Mỗi `Step()` thực hiện tối đa một phép đo hoặc một transition:

```c
void App_Poll(void)
{
    Max35103AutoCalStatus status =
        MAX35103_AutoCalStep(&g_max_autocal);

    if (status == MAX35103_AUTOCAL_COMPLETE) {
        if (MAX35103_AutoCalGetReport(
                &g_max_autocal,
                &g_max_autocal_report) ==
            MAX35103_AUTOCAL_COMPLETE) {
            /*
             * Gửi report sang storage task/service.
             * Không ghi F-RAM trực tiếp trong callback hoặc ISR.
             */
        }
    } else if (status < 0) {
        /* Log state, last driver status và giữ profile production cũ. */
    }
}
```

Để log tiến độ:

```c
Max35103AutoCalProgress progress;
MAX35103_AutoCalGetProgress(&g_max_autocal, &progress);

/* progress.state, candidate_index/count, sample_index/target */
```

`MAX35103_AutoCalCancel()` dừng state machine ở ranh giới cooperative. Service
không tự thay profile production trong storage. Chỉ `selected_profile` trong
báo cáo là ứng viên đã kiểm chứng; application quyết định version, lưu F-RAM và
kích hoạt ở lần boot phù hợp.

### 20.5 Các gate trong báo cáo

| Gate | Điều kiện chính |
|---|---|
| Communication | Tỷ lệ phép đo hợp lệ đạt `min_valid_rate_per_mille` |
| Physical | UP và DN đều nằm trong cửa sổ TOF của đường âm |
| Waveform | Hit tăng dần, chu kỳ gần tần số phát và WVR nằm trong biên |
| Statistics | MAD của UP/DN/DIFF trong ngưỡng và không cycle-slip |
| Robustness | Đủ perturbation lân cận vẫn PASS |
| Reset | `RST + INIT + configure + holdout` vẫn PASS |

`evidence_crc32` là CRC-32/ISO-HDLC trên các trường báo cáo được mã hóa rõ ràng.
Không lưu raw memory image của struct vì padding có thể khác giữa compiler;
storage service nên dùng codec có version.

### 20.6 Điều kiện chạy HIL

- Ống đầy nước và không có bọt khí lớn.
- Đường truyền âm một chiều thật sự là 62 mm.
- Tần số danh định của đầu dò đã được nhập đúng; nếu chưa chắc chắn có thể mở
  rộng `dpl_min/dpl_max`.
- Không chạy Event Timing, temperature command hoặc module SPI khác song song.
- Watchdog chỉ được feed khi `progress` thay đổi.
- Chỉ đặt `zero_flow_confirmed=true` khi điều kiện nước đứng yên được xác nhận.

Quá trình có thể kéo dài vài phút vì mỗi ứng viên cần nhiều phép TOF. Đây là
chế độ commissioning/manufacturing, không phải tác vụ chạy ở mọi lần boot.

## 21. Nguồn kỹ thuật

- [MAX35103 datasheet, register map và conversion results](https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf)
- [Analog Devices: Configuring the MAX35101/MAX35103 for an ultrasonic water meter](https://www.analog.com/en/resources/app-notes/configuring-the-max35101-timetodigital-converter-as-an-ultrasonic-water-meter.html)
