# Driver nhúng ZSSC3241 portable với STM32 HAL adapter

Driver điều khiển Renesas ZSSC3241 qua I²C, gồm:

- đo sensor và nhiệt độ dạng raw 24-bit;
- đọc kết quả SSC đã tuyến tính hóa và bù nhiệt;
- oversampling 2, 4, 8 và 16 lần;
- Command, Sleep và Cyclic Mode;
- xử lý blocking hoặc FSM non-blocking kết hợp chân EOC;
- đọc diagnostics và chạy self-diagnostic;
- đọc, ghi, verify và dump NVM có cơ chế bảo vệ;
- overwrite shadow register mà không ghi NVM;
- core độc lập nền tảng thông qua `Zssc3241Transport`;
- STM32 HAL adapter cho I²C, RESQ, tick và delay;
- unit test trên máy tính và bộ hardware-test không phá hủy dữ liệu.

Driver không tính hệ số hiệu chuẩn từ các điểm áp suất/nhiệt độ chuẩn. Nó chỉ cung cấp các primitive nhúng cần thiết để đọc raw, đọc corrected, cấu hình shadow và ghi coefficient đã được ứng dụng cung cấp.

Tài liệu tham khảo: [ZSSC3241 Datasheet, Renesas](https://www.renesas.com/en/document/dst/zssc3241-datasheet).

## 1. Thành phần

| File | Chức năng |
|---|---|
| `zssc3241.h` | Command, mask, kiểu dữ liệu và public API |
| `zssc3241.c` | Portable core: command engine, FSM, measurement, diagnostics và NVM |
| `zssc3241_stm32_hal.h/.c` | Adapter STM32 HAL cho I²C, GPIO reset và thời gian |
| `zssc3241_test/zssc3241_test.h/.c` | Bộ test logic và phần cứng qua UART2, không ghi NVM |
| `zssc3241_test/CMakeLists.txt` | Target `swfpm::test_zssc3241` |
| `tests/test_zssc3241_transport.c` | Host smoke test với transport giả lập |
| `tests/CMakeLists.txt` | Target host test và đăng ký CTest |
| `CMakeLists.txt` | Target core và STM32 HAL adapter |

## 2. Phạm vi hỗ trợ

### Command I²C

| Nhóm | Command |
|---|---|
| Đọc NVM | `0x00` đến `0x3F` |
| Ghi NVM customer | `0x40` đến `0x75` |
| Tạo NVM checksum | `0x90` |
| Raw sensor/temperature | `0xA2`, `0xA4` |
| Sleep/Command/Cyclic | `0xA8`, `0xA9`, `0xAB` |
| Corrected measurement | `0xAA` |
| Oversampling | `0xAC` đến `0xAF` |
| Diagnostics | `0xB0` đến `0xB4` |
| Post-calibration offset | `0xD1` |
| Shadow overwrite | `0xD6` đến `0xDB` |

Không hỗ trợ `0xD2` vì đây là lệnh khởi động OWI. Nhóm NOP `0xF0` đến `0xFF` chỉ dùng cho SPI nên không xuất hiện trong driver I²C.

Driver hiện hỗ trợ I²C Standard/Fast Mode, phù hợp cấu hình 100 kHz hoặc 400 kHz. I²C High-Speed Mode yêu cầu mọi command request có ba byte nên chưa được hỗ trợ.

## 3. Kết nối phần cứng

| Chân ZSSC3241 | Kết nối MCU |
|---|---|
| `MOSI/SDA` | I²C SDA, kéo lên VDD |
| `SCLK/SCL` | I²C SCL, kéo lên VDD |
| `SS` | Giữ ở mức không kích hoạt khi dùng I²C |
| `RESQ` | GPIO output tùy chọn, reset active-low |
| `EOC` | GPIO EXTI tùy chọn |
| `MISO` | Không sử dụng trong I²C |

Sau power-on, giao tiếp hợp lệ đầu tiên sẽ chọn cố định I²C, SPI hoặc OWI. Khi sử dụng I²C, không được tạo mức kích hoạt trên chân `SS`; nếu không IC có thể mất phản hồi cho đến khi reset.

Địa chỉ I²C 7-bit nằm trong NVM `0x02[6:0]`. Giá trị mặc định của IC là `0x00`, nhưng sản phẩm nên dùng địa chỉ đã được lập trình và xác nhận. Tránh `0x04` đến `0x07` vì các giá trị này liên quan đến I²C High-Speed Mode.

Core luôn sử dụng địa chỉ I²C 7-bit. STM32 HAL adapter dịch địa chỉ sang định
dạng HAL:

```c
hal_address = address_7bit << 1;
```

Application luôn truyền địa chỉ 7-bit vào `ZSSC3241_Init()`; không tự shift trước.

## 4. Cấu hình STM32CubeMX

Thiết lập đề xuất ban đầu:

- I²C ở Standard Mode 100 kHz hoặc Fast Mode 400 kHz;
- SDA và SCL open-drain với điện trở pull-up ngoài phù hợp điện áp bus;
- `RESQ` là output push-pull, mặc định mức cao;
- `EOC` là GPIO input có EXTI cạnh lên khi `INT_setup=00`;
- không bật SPI trên các chân đang dùng chung với giao tiếp ZSSC3241.

Khi `INT_setup=00`, EOC là một xung khoảng 5 µs sau end-of-conversion. Nếu NVM cấu hình EOC làm threshold interrupt, ngắt không còn mang nghĩa mỗi phép đo đã hoàn tất. Driver vẫn luôn kiểm tra status `BUSY` trước khi đọc kết quả.

## 5. Tích hợp CMake

```cmake
add_subdirectory(path/to/zssc3241_driver)

target_link_libraries(your_application
    PRIVATE
        swfpm::driver_zssc3241
        swfpm::driver_zssc3241_stm32_hal
)
```

`swfpm::driver_zssc3241` là core portable và luôn được tạo.
`swfpm::driver_zssc3241_stm32_hal` chỉ được tạo khi target `stm32cubemx` đã tồn
tại. Host test chỉ cần link core và không cần header hoặc thư viện STM32 HAL.

Để build bộ test trên STM32:

```cmake
add_subdirectory(path/to/zssc3241_driver)
add_subdirectory(path/to/zssc3241_driver/zssc3241_test)

target_link_libraries(your_application
    PRIVATE
        swfpm::test_zssc3241
)
```

## 6. Khởi tạo với STM32 HAL

```c
#include "zssc3241.h"
#include "zssc3241_stm32_hal.h"

static Zssc3241 g_zssc3241;
static Zssc3241Stm32HalContext g_zssc3241_hal;
static Zssc3241Transport g_zssc3241_transport;

static Zssc3241Status App_Zssc3241Init(void)
{
    Zssc3241Config config = ZSSC3241_DefaultConfig();
    config.use_eoc_interrupt = true;

    Zssc3241Status status = ZSSC3241_Stm32HalInitTransport(
        &g_zssc3241_hal,
        &hi2c1,
        ZSSC_RESQ_GPIO_Port,
        ZSSC_RESQ_Pin,
        &g_zssc3241_transport);
    if (status != ZSSC3241_OK) {
        return status;
    }

    status = ZSSC3241_Init(
        &g_zssc3241,
        &g_zssc3241_transport,
        0x28U, /* Địa chỉ 7-bit thực tế của IC. */
        &config);
    if (status != ZSSC3241_OK) {
        return status;
    }

    status = ZSSC3241_Reset(&g_zssc3241);
    if (status != ZSSC3241_OK) {
        return status;
    }

    return ZSSC3241_EnterCommandMode(&g_zssc3241);
}
```

Nếu MCU không điều khiển `RESQ`, truyền `NULL` và `0` khi tạo HAL transport:

```c
ZSSC3241_Stm32HalInitTransport(
    &g_zssc3241_hal,
    &hi2c1,
    NULL,
    0U,
    &g_zssc3241_transport);

ZSSC3241_Init(
    &g_zssc3241,
    &g_zssc3241_transport,
    address_7bit,
    &config);

ZSSC3241_Probe(&g_zssc3241);
```

Trong trường hợp này `ZSSC3241_Reset()` trả về `ZSSC3241_UNSUPPORTED`.

## 7. Đọc phép đo blocking

### Corrected sensor và temperature

```c
Zssc3241Measurement measurement;
Zssc3241Status status = ZSSC3241_Measure(
    &g_zssc3241, &measurement);

if (status == ZSSC3241_OK && measurement.valid) {
    uint32_t sensor_code = measurement.sensor_raw24;
    uint32_t temperature_code = measurement.temperature_raw24;
}
```

Khung I²C nhận được từ lệnh `0xAA`:

| Byte | Nội dung |
|---:|---|
| 0 | General status |
| 1 | Sensor bit 23:16 |
| 2 | Sensor bit 15:8 |
| 3 | Sensor bit 7:0 |
| 4 | Temperature bit 23:16 |
| 5 | Temperature bit 15:8 |
| 6 | Temperature bit 7:0 |

### Raw ADC

```c
Zssc3241Measurement raw;

if (ZSSC3241_MeasureRawSensor(&g_zssc3241, &raw) ==
    ZSSC3241_OK) {
    int32_t signed_sensor = raw.sensor_signed24;
}

if (ZSSC3241_MeasureRawTemperature(&g_zssc3241, &raw) ==
    ZSSC3241_OK) {
    int32_t signed_temperature = raw.temperature_signed24;
}
```

Raw ADC là số bù hai 24-bit. Driver giữ cả payload gốc trong `*_raw24` và giá trị đã sign-extend trong `*_signed24`.

### Oversampling

```c
Zssc3241Measurement averaged;

Zssc3241Status status = ZSSC3241_MeasureOversampled(
    &g_zssc3241,
    8U,
    &averaged);
```

`sample_count` chỉ nhận `2`, `4`, `8` hoặc `16`. Timeout được nhân theo số mẫu. Timeout thực tế vẫn nên được xác nhận theo cấu hình ADC, auto-zero và yêu cầu sản phẩm.

## 8. FSM non-blocking và EOC

Với adapter mặc định, mỗi lời gọi STM32 HAL I²C vẫn là một giao dịch blocking
ngắn. FSM không block CPU trong toàn bộ thời gian ADC chuyển đổi. Một platform
khác có thể cung cấp transport riêng mà không sửa core.

```c
Zssc3241Status status = ZSSC3241_StartMeasurement(
    &g_zssc3241,
    ZSSC3241_MEASUREMENT_CORRECTED);
```

Callback EXTI chỉ ghi nhận sự kiện:

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ZSSC_EOC_Pin) {
        ZSSC3241_OnEocInterrupt(&g_zssc3241);
    }
}
```

Main loop hoặc RTOS task gọi `Process()` định kỳ để đảm bảo timeout và polling dự phòng vẫn hoạt động:

```c
void App_Process(void)
{
    Zssc3241Status status = ZSSC3241_Process(&g_zssc3241);

    if (status != ZSSC3241_BUSY &&
        ZSSC3241_HasResult(&g_zssc3241)) {
        Zssc3241Measurement result;
        status = ZSSC3241_GetLatestResult(
            &g_zssc3241, &result);

        if (status == ZSSC3241_OK && result.valid) {
            /* Publish result sang application layer. */
        }
    }
}
```

Không gọi hàm I²C blocking trong ISR. Mỗi instance chỉ cho phép một command đo đang chờ; driver không tự bảo vệ mutex giữa nhiều RTOS task.

## 9. Cyclic Mode

```c
Zssc3241Status status = ZSSC3241_StartCyclicMode(&g_zssc3241);
```

Đọc chủ động:

```c
Zssc3241Measurement result;
status = ZSSC3241_ReadCyclicResult(&g_zssc3241, &result);
```

Hoặc dùng EOC:

```c
ZSSC3241_OnEocInterrupt(&g_zssc3241);
ZSSC3241_Process(&g_zssc3241);

if (ZSSC3241_HasResult(&g_zssc3241)) {
    ZSSC3241_GetLatestResult(&g_zssc3241, &result);
}
```

Thoát Cyclic Mode:

```c
ZSSC3241_StopCyclicMode(&g_zssc3241);
```

Scheduler của Cyclic Mode nằm trong NVM `0x1E` đến `0x20`. Driver chỉ điều khiển mode và đọc kết quả; nó không tự thay đổi scheduler.

## 10. Status và lỗi thiết bị

| Bit | Mask | Ý nghĩa |
|---:|---:|---|
| 6 | `0x40` | IC powered |
| 5 | `0x20` | Busy |
| 4:3 | `0x18` | Command/Cyclic/Sleep Mode |
| 2 | `0x04` | NVM checksum/memory error |
| 1 | `0x02` | Sensor connection fault |
| 0 | `0x01` | SSC math saturation |

Kết quả đo có lỗi vẫn giữ status và payload để phục vụ debug, nhưng `result.valid=false`. Theo mặc định API trả về lỗi tương ứng:

- `ZSSC3241_MEMORY_ERROR`;
- `ZSSC3241_CONNECTION_FAULT`;
- `ZSSC3241_MATH_SATURATION`.

Có thể đặt `reject_faulty_measurements=false` để `Process()` không dừng luồng ứng dụng vì fault; application vẫn phải kiểm tra `result.valid` và `result.device_status`.

## 11. Diagnostics

```c
Zssc3241Diagnostics diagnostics;
Zssc3241Status status = ZSSC3241_ReadDiagnostics(
    &g_zssc3241, &diagnostics);
```

Các mask chi tiết được định nghĩa trong `zssc3241.h`, ví dụ:

```c
if ((diagnostics.raw & ZSSC3241_DIAG_INP_OPEN) != 0U) {
    /* Mất kết nối INP. */
}

if ((diagnostics.raw & ZSSC3241_DIAG_MEMORY_ERROR) != 0U) {
    /* NVM checksum failure. */
}
```

Chạy lại diagnostics:

```c
ZSSC3241_UpdateDiagnostics(&g_zssc3241);
ZSSC3241_ReadDiagnostics(&g_zssc3241, &diagnostics);
```

`UPDATE_DIAG` có thể mất lâu hơn phép đọc thông thường vì nó chạy các check được bật trong NVM và kiểm tra CRC.

## 12. Đọc và ghi NVM

### Đọc

```c
uint16_t interface_config;

ZSSC3241_ReadNvm(
    &g_zssc3241,
    ZSSC3241_NVM_INTERFACE_CONFIG,
    &interface_config);

uint8_t address =
    (uint8_t)(interface_config & ZSSC3241_NVM_SLAVE_ADDRESS_MASK);
```

Dump toàn bộ vùng đọc được:

```c
uint16_t nvm[ZSSC3241_NVM_WORD_COUNT];
ZSSC3241_DumpNvm(
    &g_zssc3241,
    nvm,
    ZSSC3241_NVM_WORD_COUNT);
```

Nên dump và lưu backup trước mọi thay đổi NVM.

### Bảo vệ ghi

Ghi NVM bị tắt theo mặc định. Cần hai bước opt-in:

```c
Zssc3241Config config = ZSSC3241_DefaultConfig();
config.allow_nvm_write = true;

/* Sau ZSSC3241_Init(): */
ZSSC3241_UnlockNvmWrites(
    &g_zssc3241,
    ZSSC3241_NVM_UNLOCK_KEY);
```

Sau đó mới được ghi:

```c
Zssc3241Status status = ZSSC3241_WriteNvm(
    &g_zssc3241,
    address,
    new_value);
```

`ZSSC3241_WriteNvm()` thực hiện:

1. kiểm tra địa chỉ customer `0x00` đến `0x35`;
2. đọc giá trị hiện tại;
3. bỏ qua nếu giá trị không đổi;
4. từ chối thao tác set permanent lock bit;
5. ghi NVM;
6. đọc lại và so sánh nếu `verify_nvm_after_write=true`.

Ghi nhiều word rồi cập nhật checksum một lần:

```c
const uint16_t values[] = { value0, value1, value2 };

status = ZSSC3241_WriteNvmBlock(
    &g_zssc3241,
    start_address,
    values,
    sizeof(values) / sizeof(values[0]),
    true);

ZSSC3241_LockNvmWrites(&g_zssc3241);
```

NVM có tuổi thọ ghi hữu hạn, khoảng 10.000 chu kỳ. Không ghi cấu hình trong vòng lặp đo. Permanent NVM lock bit được driver cố ý bảo vệ và không cung cấp API kích hoạt vì sau reset thao tác này không thể hoàn tác.

Sau khi thay đổi địa chỉ I²C trong NVM `0x02`, địa chỉ active chỉ thay đổi sau reset/power cycle. Application phải cập nhật `device.address_7bit` phù hợp trước khi giao tiếp lại.

## 13. Shadow register

Shadow overwrite thay đổi cấu hình hoạt động mà không ghi NVM:

```c
ZSSC3241_EnterCommandMode(&g_zssc3241);

ZSSC3241_OverwriteShadow(
    &g_zssc3241,
    ZSSC3241_SHADOW_SM_CONFIG1,
    sm_config1);
```

Các giá trị shadow bị khôi phục từ NVM sau reset hoặc power-on reset.

## 14. Tính toán dữ liệu

### Ghép unsigned 24-bit

$$
Code = (B_0 \ll 16) | (B_1 \ll 8) | B_2
$$

### Sign-extension raw 24-bit

Nếu bit 23 bằng 1, driver điền các bit 31:24 bằng 1 để tạo `int32_t` âm.

```c
int32_t signed_value = ZSSC3241_DecodeSigned24(raw24);
```

### Normalized raw

Raw ADC dùng trọng số bù hai Q23:

$$
Raw_{norm}=\frac{Raw_{signed}}{2^{23}}
$$

```c
float raw_norm = ZSSC3241_RawToNormalized(raw.sensor_signed24);
```

Miền lý thuyết xấp xỉ $[-1,1)$.

### Normalized corrected

SSC output là unsigned Q23:

$$
SSC_{norm}=\frac{SSC_{code}}{2^{23}}
$$

```c
float normalized = ZSSC3241_CorrectedToNormalized(
    measurement.sensor_raw24);
```

Miền biểu diễn là $[0,2)$. Đây chưa phải Pa, bar hoặc °C.

### Chuyển sang đại lượng vật lý

Khi đã biết code tại hai đầu dải hiệu chuẩn:

$$
y=y_{min}+\frac{Code-Code_{min}}{Code_{max}-Code_{min}}
\left(y_{max}-y_{min}\right)
$$

Ví dụ ánh xạ output sang áp suất Pa:

```c
int32_t pressure_pa;

status = ZSSC3241_MapCorrected(
    measurement.sensor_raw24,
    sensor_code_min,
    sensor_code_max,
    pressure_min_pa,
    pressure_max_pa,
    &pressure_pa);
```

`code_min`, `code_max` và dải vật lý phải lấy từ cấu hình hiệu chuẩn của chính sensor. Không dùng mặc định chung cho mọi cảm biến.

## 15. Timeout

`Zssc3241Config` có các timeout riêng:

| Trường | Mặc định |
|---|---:|
| `bus_timeout_ms` | 10 ms |
| `measurement_timeout_ms` | 25 ms |
| `diagnostic_timeout_ms` | 50 ms |
| `nvm_timeout_ms` | 25 ms |
| `poll_interval_ms` | 1 ms |
| `reset_pulse_ms` | 1 ms |
| `reset_ready_ms` | 5 ms |

Với oversampling:

$$
Timeout_{effective}=Timeout_{measurement}\times N
$$

Đây là giới hạn bảo vệ phía driver, không phải thời gian chuyển đổi chính xác. Cần tinh chỉnh sau khi biết ADC resolution, auto-zero và scheduler của sản phẩm.

## 16. Kiểm tra phần cứng tối thiểu

```c
static Zssc3241Status App_Zssc3241SmokeTest(void)
{
    Zssc3241Status status = App_Zssc3241Init();
    if (status != ZSSC3241_OK) {
        return status;
    }

    Zssc3241DeviceStatus device_status;
    status = ZSSC3241_ReadStatus(&g_zssc3241, &device_status);
    if (status != ZSSC3241_OK) {
        return status;
    }

    Zssc3241Measurement measurement;
    return ZSSC3241_Measure(&g_zssc3241, &measurement);
}
```

Smoke test trên chỉ reset/probe, đọc status và thực hiện một phép đo corrected; nó
không ghi NVM. Bộ hardware-test mở rộng phải nhận một
`Zssc3241Transport` hoặc một `Zssc3241` đã được application khởi tạo, thay vì
truy cập trực tiếp `I2C_HandleTypeDef`.

## 17. Unit test trên máy tính

Nếu không có CMake trong môi trường host:

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -I. zssc3241.c tests/test_zssc3241_transport.c \
    -o zssc3241_tests

./zssc3241_tests
```

Hoặc dùng CMake/CTest:

```bash
cmake -S . -B build -DZSSC3241_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Smoke test kiểm tra contract transport, địa chỉ I²C 7-bit, probe, reset active-low
và các timing mặc định mà không cần STM32 HAL.

## 18. Giới hạn hiện tại

- Chỉ hỗ trợ I²C Standard/Fast Mode, không hỗ trợ I²C High-Speed, SPI hoặc OWI.
- Không tính coefficient hiệu chuẩn.
- Không tự cấu hình NVM measurement scheduler.
- Không cho phép kích hoạt permanent NVM lock bit.
- Core không phụ thuộc STM32 HAL. Adapter STM32 mặc định dùng I²C blocking trong `bus_timeout_ms`.
- Một instance phải được sở hữu bởi một task hoặc được application bảo vệ bằng mutex.
- Cần xác nhận trên phần cứng thật các timeout, cạnh EOC, địa chỉ và NVM image của sản phẩm.
