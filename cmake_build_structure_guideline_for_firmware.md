# CMake Build Structure Guideline for Firmware

## 1. Mục tiêu của tài liệu

Tài liệu này mô tả quy chuẩn tổ chức các file `CMakeLists.txt` trong một dự án firmware C/C++ có nhiều module.

Nguyên tắc chính được sử dụng trong tài liệu này là:

> **Root `CMakeLists.txt` chỉ điều phối project. Mỗi module hoặc thư mục chức năng nên có `CMakeLists.txt` riêng và tự khai báo target của nó.**

Cách tổ chức này giúp firmware:

- Dễ đọc và dễ bảo trì.
- Dễ thêm, sửa, xóa module.
- Dễ tách riêng từng phần để unit test.
- Dễ tái sử dụng driver/service cho project khác.
- Dễ build trên nhiều môi trường khác nhau, ví dụ embedded target, PC simulation, unit test, hoặc hardware-in-the-loop test.
- Giảm phụ thuộc chéo không kiểm soát giữa các tầng phần mềm.

Tài liệu này phù hợp cho các firmware có cấu trúc nhiều tầng như:

- `app`
- `services`
- `drivers`
- `protocols`
- `platform`
- `bsp`
- `middleware`
- `tests`
- `simulation`

---

## 2. Vấn đề khi gom toàn bộ CMake vào một file root

Với project nhỏ, ví dụ chỉ có vài file `.c`, việc gom tất cả source vào `CMakeLists.txt` ở root là chấp nhận được.

Ví dụ:

```cmake
add_executable(firmware.elf
    main.c
    app/app.c
    drivers/gpio.c
    services/storage.c
)
```

Tuy nhiên, khi firmware phát triển lớn hơn, cách này sẽ tạo ra nhiều vấn đề.

### 2.1. Root CMakeLists.txt trở nên quá dài

Khi project có nhiều module, root `CMakeLists.txt` sẽ chứa quá nhiều thông tin chi tiết:

- Danh sách toàn bộ file `.c`.
- Danh sách toàn bộ include path.
- Compile definition của từng module.
- Compile option riêng cho từng phần.
- Dependency giữa các module.
- Logic build cho test, simulation, target thật.

Điều này làm root file khó đọc và khó review.

### 2.2. Khó biết file source thuộc module nào

Ví dụ:

```cmake
add_executable(firmware.elf
    app/app_event_loop.c
    app/app_state_machine.c
    services/measurement_manager.c
    services/storage_service.c
    services/calibration_service.c
    drivers/max35103/max35103.c
    drivers/fram/fm24cl04b.c
    protocols/modbus/modbus_service.c
    platform/stm32_hal_port.c
)
```

Nhìn vào đây rất khó biết:

- Module nào phụ thuộc module nào.
- Header nào là public API.
- Header nào là nội bộ.
- Driver nào có thể tái sử dụng.
- Module nào có thể test độc lập.

### 2.3. Dễ tạo phụ thuộc lộn xộn

Khi mọi include path được khai báo global, module nào cũng có thể include header của module khác mà không bị kiểm soát.

Ví dụ:

```cmake
include_directories(
    app
    services
    drivers
    protocols
    platform
)
```

Khi đó, `driver` có thể vô tình include `app`, hoặc `platform` có thể include `services`. Đây là điều không nên xảy ra trong firmware có kiến trúc phân tầng.

### 2.4. Khó tách unit test hoặc simulation

Nếu tất cả source được gom trực tiếp vào `firmware.elf`, rất khó build riêng một module để test trên PC.

Ví dụ, nếu muốn test `flow_computation_service`, ta phải kéo theo toàn bộ firmware, bao gồm cả STM32 HAL, startup file, linker script, và driver phần cứng. Điều này làm unit test nặng và khó triển khai.

---

## 3. Nguyên tắc tổ chức CMake được đề xuất

Nguyên tắc chính:

```text
Root CMakeLists.txt
    ├── Khai báo project
    ├── Cấu hình toolchain, compiler, build option chung
    ├── add_subdirectory(...) cho các module
    └── Tạo firmware executable cuối cùng

Module CMakeLists.txt
    ├── Tự khai báo source của module
    ├── Tự khai báo include directory
    ├── Tự khai báo dependency
    ├── Tự khai báo compile definition nếu cần
    └── Xuất ra một target rõ ràng
```

Tức là root không nên biết quá nhiều chi tiết bên trong từng module.

Root chỉ nên biết rằng project có các khối như:

- `app`
- `services`
- `drivers`
- `protocols`
- `platform`

Còn mỗi khối có bao nhiêu file `.c`, cần include path nào, phụ thuộc module nào thì để chính module đó tự khai báo.

---

## 4. Cấu trúc thư mục khuyến nghị

Một cấu trúc phù hợp cho firmware module hóa:

```text
firmware/
├── CMakeLists.txt
├── cmake/
│   ├── arm-gcc-toolchain.cmake
│   ├── compiler_options.cmake
│   ├── firmware_size.cmake
│   └── stm32_linker.cmake
│
├── app/
│   ├── CMakeLists.txt
│   ├── app_event_loop.c
│   ├── app_state_machine.c
│   ├── command_dispatcher.c
│   └── app_events.h
│
├── services/
│   ├── CMakeLists.txt
│   ├── measurement_manager.c
│   ├── flow_computation_service.c
│   ├── calibration_service.c
│   ├── error_detection_service.c
│   ├── data_repository.c
│   ├── config_repository.c
│   ├── volume_accumulator.c
│   ├── storage_service.c
│   ├── modbus_service.c
│   ├── power_manager.c
│   └── health_monitor.c
│
├── protocols/
│   ├── CMakeLists.txt
│   └── modbus/
│       ├── CMakeLists.txt
│       ├── modbus_rtu.c
│       ├── modbus_register_map.c
│       └── modbus_rtu.h
│
├── drivers/
│   ├── CMakeLists.txt
│   ├── max35103/
│   │   ├── CMakeLists.txt
│   │   ├── max35103.c
│   │   └── max35103.h
│   ├── fram/
│   │   ├── CMakeLists.txt
│   │   ├── fm24cl04b.c
│   │   └── fm24cl04b.h
│   └── rs485/
│       ├── CMakeLists.txt
│       ├── rs485_port.c
│       └── rs485_port.h
│
├── platform/
│   ├── CMakeLists.txt
│   ├── stm32_hal_port.c
│   ├── board_config.h
│   └── system_clock.c
│
├── bsp/
│   ├── CMakeLists.txt
│   ├── stm32l433_board.c
│   └── stm32l433_board.h
│
├── tests/
│   ├── CMakeLists.txt
│   └── test_flow_computation.c
│
└── main.c
```

Cấu trúc trên có thể được điều chỉnh tùy project, nhưng tư tưởng chính là:

- Mỗi module có trách nhiệm tự mô tả cách build chính nó.
- Root chỉ điều phối.
- Các file dùng chung cho build system được để trong thư mục `cmake/`.

---

## 5. Vai trò của root `CMakeLists.txt`

Root `CMakeLists.txt` là điểm vào chính của project.

Nó nên chịu trách nhiệm cho các việc sau:

1. Khai báo phiên bản CMake tối thiểu.
2. Khai báo tên project.
3. Khai báo ngôn ngữ sử dụng, ví dụ `C`, `CXX`, `ASM`.
4. Include các file cấu hình CMake dùng chung.
5. Thiết lập chuẩn C/C++.
6. Thêm các module bằng `add_subdirectory`.
7. Tạo executable cuối cùng, ví dụ `firmware.elf`.
8. Link các target module vào firmware.
9. Cấu hình linker script, memory map, output `.hex`, `.bin` nếu cần.

Root `CMakeLists.txt` không nên chứa:

- Danh sách chi tiết toàn bộ source của từng module.
- Include path nội bộ của từng module.
- Compile definition riêng của từng module.
- Logic build quá cụ thể của driver/service.
- Phụ thuộc chi tiết giữa các source file bên trong module.

### 5.1. Ví dụ root `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)

project(smart_meter_firmware C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/compiler_options.cmake)
include(cmake/stm32_linker.cmake)
include(cmake/firmware_size.cmake)

add_subdirectory(platform)
add_subdirectory(bsp)
add_subdirectory(drivers)
add_subdirectory(protocols)
add_subdirectory(services)
add_subdirectory(app)

add_executable(firmware.elf
    main.c
    startup_stm32l433xx.s
)

target_link_libraries(firmware.elf
    PRIVATE
        app
        services
        protocols
        drivers
        bsp
        platform
        project_compiler_options
)

target_linker_script(firmware.elf
    ${CMAKE_SOURCE_DIR}/STM32L433RCTx_FLASH.ld
)

create_firmware_outputs(firmware.elf)
print_firmware_size(firmware.elf)
```

### 5.2. Root chỉ link target, không quản lý source chi tiết

Không nên viết như sau trong root:

```cmake
add_executable(firmware.elf
    main.c
    app/app_event_loop.c
    app/app_state_machine.c
    services/measurement_manager.c
    services/flow_computation_service.c
    services/calibration_service.c
    drivers/max35103/max35103.c
    drivers/fram/fm24cl04b.c
    protocols/modbus/modbus_rtu.c
    platform/stm32_hal_port.c
)
```

Nên viết:

```cmake
add_executable(firmware.elf
    main.c
    startup_stm32l433xx.s
)

target_link_libraries(firmware.elf
    PRIVATE
        app
        services
        drivers
        protocols
        platform
)
```

Khi thêm file mới vào module `services`, ta chỉ sửa `services/CMakeLists.txt`, không cần sửa root.

---

## 6. Vai trò của `CMakeLists.txt` trong từng module

Mỗi module nên có `CMakeLists.txt` riêng.

File này có trách nhiệm:

1. Khai báo module là một target CMake.
2. Khai báo danh sách source của module.
3. Khai báo include directory public/private.
4. Khai báo dependency với module khác.
5. Khai báo compile definition nếu module cần.
6. Giữ logic build của module nằm gần source code của module.

### 6.1. Ví dụ module `services`

`services/CMakeLists.txt`:

```cmake
add_library(services STATIC
    measurement_manager.c
    flow_computation_service.c
    calibration_service.c
    error_detection_service.c
    data_repository.c
    config_repository.c
    volume_accumulator.c
    storage_service.c
    modbus_service.c
    power_manager.c
    health_monitor.c
)

target_include_directories(services
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(services
    PRIVATE
        drivers
        protocols
        platform
)
```

Ý nghĩa:

- `services` được build thành một static library.
- Các module khác có thể link với `services`.
- Header trong thư mục `services/` được xem là public API của module.
- `services` phụ thuộc vào `drivers`, `protocols`, và `platform`.

### 6.2. Ví dụ module `app`

`app/CMakeLists.txt`:

```cmake
add_library(app STATIC
    app_event_loop.c
    app_state_machine.c
    command_dispatcher.c
)

target_include_directories(app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(app
    PRIVATE
        services
)
```

Ý nghĩa:

- `app` là tầng điều phối logic ứng dụng.
- `app` được phép gọi `services`.
- `app` không nên gọi trực tiếp driver phần cứng nếu không cần thiết.

### 6.3. Ví dụ module driver MAX35103

`drivers/max35103/CMakeLists.txt`:

```cmake
add_library(driver_max35103 STATIC
    max35103.c
)

target_include_directories(driver_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_max35103
    PRIVATE
        platform
)
```

Ý nghĩa:

- Driver MAX35103 là một target độc lập.
- Driver này phụ thuộc vào `platform` để dùng SPI/GPIO/delay abstraction.
- Driver không nên phụ thuộc vào `services` hoặc `app`.

### 6.4. Ví dụ module driver FRAM

`drivers/fram/CMakeLists.txt`:

```cmake
add_library(driver_fram STATIC
    fm24cl04b.c
)

target_include_directories(driver_fram
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_fram
    PRIVATE
        platform
)
```

### 6.5. Ví dụ module `drivers` gom nhiều driver con

`drivers/CMakeLists.txt`:

```cmake
add_subdirectory(max35103)
add_subdirectory(fram)
add_subdirectory(rs485)

add_library(drivers INTERFACE)

target_link_libraries(drivers
    INTERFACE
        driver_max35103
        driver_fram
        driver_rs485
)
```

Ở đây `drivers` là một target gom nhóm.

Các module khác chỉ cần link với:

```cmake
target_link_libraries(services
    PRIVATE
        drivers
)
```

Thay vì phải biết cụ thể bên trong có bao nhiêu driver.

---

## 7. Nên dùng target-based CMake

CMake hiện đại nên được viết theo kiểu target-based.

Tức là ưu tiên dùng:

```cmake
target_include_directories(...)
target_link_libraries(...)
target_compile_definitions(...)
target_compile_options(...)
```

Không nên dùng global command như:

```cmake
include_directories(...)
add_definitions(...)
link_directories(...)
```

### 7.1. Lý do nên tránh global command

Các lệnh global làm cấu hình bị áp dụng cho toàn bộ project.

Ví dụ:

```cmake
include_directories(drivers/max35103)
```

Khi dùng cách này, mọi module đều có thể include header của MAX35103, kể cả các module không nên biết đến driver này.

Điều này làm kiến trúc bị phá vỡ dần theo thời gian.

### 7.2. Cách đúng

Nên để driver tự khai báo include path:

```cmake
target_include_directories(driver_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
```

Sau đó module nào cần dùng driver thì link rõ ràng:

```cmake
target_link_libraries(services
    PRIVATE
        driver_max35103
)
```

Cách này giúp dependency rõ ràng hơn.

---

## 8. Ý nghĩa của `PRIVATE`, `PUBLIC`, `INTERFACE`

Ba từ khóa `PRIVATE`, `PUBLIC`, `INTERFACE` là phần rất quan trọng trong CMake hiện đại.

### 8.1. `PRIVATE`

Dùng khi dependency hoặc include path chỉ cần cho chính target hiện tại.

Ví dụ:

```cmake
target_link_libraries(services
    PRIVATE
        driver_max35103
)
```

Nghĩa là:

- `services` cần `driver_max35103`.
- Module nào link với `services` không nhất thiết được truyền tiếp dependency này.

Dùng `PRIVATE` khi header public của `services` không expose type hoặc header từ `driver_max35103`.

### 8.2. `PUBLIC`

Dùng khi dependency cần cho target hiện tại và cũng cần truyền tiếp cho target sử dụng nó.

Ví dụ:

```cmake
target_include_directories(protocol_modbus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
```

Nếu module khác link với `protocol_modbus`, module đó cũng cần thấy header public của Modbus.

Hoặc:

```cmake
target_link_libraries(app
    PUBLIC
        services
)
```

Dùng khi header public của `app` có sử dụng type hoặc API từ `services`.

### 8.3. `INTERFACE`

Dùng cho target không có source, chỉ dùng để truyền cấu hình hoặc gom nhóm dependency.

Ví dụ target chứa compiler warning chung:

```cmake
add_library(project_compiler_options INTERFACE)

target_compile_options(project_compiler_options
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
)
```

Ví dụ target gom nhóm driver:

```cmake
add_library(drivers INTERFACE)

target_link_libraries(drivers
    INTERFACE
        driver_max35103
        driver_fram
        driver_rs485
)
```

### 8.4. Quy tắc nhớ nhanh

```text
PRIVATE   = chỉ target hiện tại cần
PUBLIC    = target hiện tại cần, target dùng nó cũng cần
INTERFACE = bản thân target không build source, chỉ truyền cấu hình/phụ thuộc
```

---

## 9. Quy tắc phân tầng dependency trong firmware

Một firmware có kiến trúc tốt nên có dependency đi một chiều.

Ví dụ:

```text
app
 ↓
services
 ↓
protocols / drivers
 ↓
platform / bsp
 ↓
vendor HAL / CMSIS
```

### 9.1. Tầng `app`

Tầng `app` chịu trách nhiệm:

- Điều phối luồng chương trình.
- Chứa state machine cấp ứng dụng.
- Chứa event loop.
- Dispatch command.
- Gọi các service để thực hiện nghiệp vụ.

Tầng `app` nên phụ thuộc vào:

```text
services
```

Tầng `app` không nên phụ thuộc trực tiếp vào:

```text
STM32 HAL
driver cụ thể
protocol low-level
```

Ví dụ không nên:

```c
#include "stm32l4xx_hal.h"
#include "max35103.h"
```

trực tiếp trong `app_state_machine.c`, trừ khi có lý do đặc biệt.

### 9.2. Tầng `services`

Tầng `services` chịu trách nhiệm:

- Measurement manager.
- Flow computation.
- Calibration.
- Error detection.
- Data repository.
- Config repository.
- Storage service.
- Power manager.
- Health monitor.

Tầng này có thể phụ thuộc vào:

```text
drivers
protocols
platform abstraction
```

Nhưng không nên phụ thuộc ngược lên:

```text
app
```

### 9.3. Tầng `drivers`

Tầng `drivers` chịu trách nhiệm giao tiếp với IC/peripheral cụ thể:

- MAX35103 / MAX35101.
- FM24CL04B FRAM.
- RS485 transceiver.
- LCD.
- Sensor.
- EEPROM.
- ADC frontend.

Driver nên phụ thuộc vào abstraction từ `platform`, ví dụ:

- `platform_spi_transfer`
- `platform_i2c_read`
- `platform_gpio_write`
- `platform_delay_ms`

Driver không nên gọi trực tiếp service hoặc app.

### 9.4. Tầng `protocols`

Tầng `protocols` chịu trách nhiệm xử lý giao thức:

- Modbus RTU.
- Register map.
- Frame parser.
- CRC.
- Protocol command policy.

Protocol có thể phụ thuộc vào `platform` nếu cần UART abstraction, nhưng nên hạn chế phụ thuộc trực tiếp vào hardware cụ thể.

### 9.5. Tầng `platform`

Tầng `platform` là lớp thích nghi với phần cứng và SDK cụ thể:

- STM32 HAL wrapper.
- GPIO abstraction.
- SPI abstraction.
- I2C abstraction.
- UART abstraction.
- Timer abstraction.
- Critical section.
- System tick.
- Board pin mapping.

Tầng này có thể phụ thuộc vào:

```text
STM32 HAL
CMSIS
BSP
```

Nhưng không nên phụ thuộc vào:

```text
app
services
business logic
```

---

## 10. Ví dụ dependency hợp lý trong CMake

### 10.1. Root

```cmake
target_link_libraries(firmware.elf
    PRIVATE
        app
        services
        protocols
        drivers
        platform
)
```

### 10.2. App

```cmake
target_link_libraries(app
    PRIVATE
        services
)
```

### 10.3. Services

```cmake
target_link_libraries(services
    PRIVATE
        drivers
        protocols
        platform
)
```

### 10.4. Drivers

```cmake
target_link_libraries(driver_max35103
    PRIVATE
        platform
)
```

### 10.5. Protocols

```cmake
target_link_libraries(protocol_modbus
    PRIVATE
        platform
)
```

### 10.6. Platform

```cmake
target_link_libraries(platform
    PUBLIC
        stm32_hal
        cmsis
)
```

---

## 11. Quy ước đặt tên target

Nên đặt tên target rõ ràng và nhất quán.

### 11.1. Target cấp module lớn

```text
app
services
drivers
protocols
platform
bsp
```

### 11.2. Target driver cụ thể

```text
driver_max35103
driver_fram
driver_rs485
driver_lcd
driver_temperature_sensor
```

### 11.3. Target protocol cụ thể

```text
protocol_modbus
protocol_mbus
protocol_lorawan
```

### 11.4. Target service cụ thể nếu cần tách nhỏ

Nếu `services` quá lớn, có thể tách thành nhiều target nhỏ:

```text
service_measurement
service_calibration
service_storage
service_power
service_health
```

Sau đó gom lại:

```cmake
add_library(services INTERFACE)

target_link_libraries(services
    INTERFACE
        service_measurement
        service_calibration
        service_storage
        service_power
        service_health
)
```

---

## 12. Khi nào nên dùng một target lớn, khi nào nên tách target nhỏ?

### 12.1. Dùng một target lớn khi module còn nhỏ

Ví dụ:

```cmake
add_library(services STATIC
    measurement_manager.c
    flow_computation_service.c
    calibration_service.c
    storage_service.c
)
```

Cách này phù hợp khi:

- Số lượng file chưa quá nhiều.
- Các file trong module có liên quan chặt chẽ.
- Chưa cần unit test từng service độc lập.
- Project đang ở giai đoạn MVP hoặc prototype.

### 12.2. Tách target nhỏ khi module lớn dần

Khi `services` phát triển lớn, nên tách:

```text
services/
├── CMakeLists.txt
├── measurement/
│   ├── CMakeLists.txt
│   └── measurement_manager.c
├── calibration/
│   ├── CMakeLists.txt
│   └── calibration_service.c
├── storage/
│   ├── CMakeLists.txt
│   └── storage_service.c
└── health/
    ├── CMakeLists.txt
    └── health_monitor.c
```

Ví dụ `services/CMakeLists.txt`:

```cmake
add_subdirectory(measurement)
add_subdirectory(calibration)
add_subdirectory(storage)
add_subdirectory(health)

add_library(services INTERFACE)

target_link_libraries(services
    INTERFACE
        service_measurement
        service_calibration
        service_storage
        service_health
)
```

Cách này phù hợp khi:

- Cần unit test từng service riêng.
- Cần mock dependency.
- Cần build một phần service cho simulation.
- Cần kiểm soát dependency chi tiết.

---

## 13. Quy ước include directory

Nên tách rõ header public và private nếu module đủ lớn.

### 13.1. Module nhỏ

Với module nhỏ, có thể để header cùng thư mục:

```text
drivers/max35103/
├── CMakeLists.txt
├── max35103.c
└── max35103.h
```

CMake:

```cmake
target_include_directories(driver_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
```

### 13.2. Module lớn

Với module lớn, nên tách `include/` và `src/`:

```text
drivers/max35103/
├── CMakeLists.txt
├── include/
│   └── max35103.h
└── src/
    └── max35103.c
```

CMake:

```cmake
add_library(driver_max35103 STATIC
    src/max35103.c
)

target_include_directories(driver_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
```

Ý nghĩa:

- `include/` là API public cho module khác sử dụng.
- `src/` là phần nội bộ, không nên bị module khác include trực tiếp.

---

## 14. Quy ước file trong thư mục `cmake/`

Thư mục `cmake/` nên chứa các file cấu hình hoặc helper dùng chung cho toàn project.

Ví dụ:

```text
cmake/
├── arm-gcc-toolchain.cmake
├── compiler_options.cmake
├── stm32_linker.cmake
├── firmware_size.cmake
├── static_analysis.cmake
├── code_coverage.cmake
└── unit_test.cmake
```

### 14.1. `arm-gcc-toolchain.cmake`

Dùng để cấu hình cross-compiler:

```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

### 14.2. `compiler_options.cmake`

Dùng để gom compile option chung:

```cmake
add_library(project_compiler_options INTERFACE)

target_compile_options(project_compiler_options
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -ffunction-sections
        -fdata-sections
)

target_compile_definitions(project_compiler_options
    INTERFACE
        USE_HAL_DRIVER
        STM32L433xx
)
```

### 14.3. `stm32_linker.cmake`

Dùng để cấu hình linker script:

```cmake
function(target_linker_script target linker_script)
    target_link_options(${target}
        PRIVATE
            -T${linker_script}
            -Wl,--gc-sections
            -Wl,-Map=${target}.map
    )
endfunction()
```

### 14.4. `firmware_size.cmake`

Dùng để tạo command in kích thước firmware:

```cmake
function(print_firmware_size target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
    )
endfunction()
```

### 14.5. `firmware_outputs.cmake`

Dùng để tạo file `.bin` và `.hex`:

```cmake
function(create_firmware_outputs target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${target}>
                ${target}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${target}>
                ${target}.bin
    )
endfunction()
```

---

## 15. Hỗ trợ unit test và PC simulation

Một lợi ích lớn của CMake module hóa là dễ build cùng source cho nhiều môi trường.

Ví dụ:

```text
firmware/
├── services/
│   └── flow_computation_service.c
├── platform/
│   ├── platform_stm32.c
│   └── platform_linux_mock.c
└── tests/
    └── test_flow_computation.c
```

### 15.1. Build cho target thật

```cmake
target_link_libraries(firmware.elf
    PRIVATE
        app
        services
        platform_stm32
)
```

### 15.2. Build cho unit test trên PC

```cmake
add_executable(test_flow_computation
    test_flow_computation.c
)

target_link_libraries(test_flow_computation
    PRIVATE
        services
        platform_mock
)
```

Nhờ dependency được khai báo rõ, ta có thể thay `platform_stm32` bằng `platform_mock` khi test.

### 15.3. Khuyến nghị cho firmware có simulation

Nếu project có mục tiêu mô phỏng trước khi có phần cứng thật, nên thiết kế `platform` theo dạng abstraction.

Ví dụ:

```text
platform/
├── include/
│   └── platform_spi.h
├── stm32/
│   ├── CMakeLists.txt
│   └── platform_spi_stm32.c
└── linux_mock/
    ├── CMakeLists.txt
    └── platform_spi_linux_mock.c
```

Khi build firmware thật:

```cmake
target_link_libraries(firmware.elf
    PRIVATE
        platform_stm32
)
```

Khi build simulation:

```cmake
target_link_libraries(simulator
    PRIVATE
        platform_linux_mock
)
```

---

## 16. Gợi ý cấu trúc CMake cho firmware STM32

Một cấu trúc thực tế có thể dùng như sau:

```text
firmware/
├── CMakeLists.txt
├── cmake/
│   ├── arm-gcc-toolchain.cmake
│   ├── compiler_options.cmake
│   ├── stm32_linker.cmake
│   └── firmware_outputs.cmake
│
├── vendor/
│   ├── CMakeLists.txt
│   ├── cmsis/
│   └── stm32_hal/
│
├── platform/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── platform_gpio.h
│   │   ├── platform_spi.h
│   │   ├── platform_i2c.h
│   │   └── platform_uart.h
│   └── stm32/
│       ├── platform_gpio_stm32.c
│       ├── platform_spi_stm32.c
│       ├── platform_i2c_stm32.c
│       └── platform_uart_stm32.c
│
├── drivers/
│   ├── CMakeLists.txt
│   ├── max35103/
│   ├── fram/
│   └── rs485/
│
├── protocols/
│   ├── CMakeLists.txt
│   └── modbus/
│
├── services/
│   ├── CMakeLists.txt
│   ├── measurement/
│   ├── calibration/
│   ├── storage/
│   └── health/
│
├── app/
│   ├── CMakeLists.txt
│   ├── app_event_loop.c
│   └── app_state_machine.c
│
└── main.c
```

---

## 17. Ví dụ hoàn chỉnh tối giản

### 17.1. Root `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)

project(smart_meter_firmware C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/compiler_options.cmake)
include(cmake/stm32_linker.cmake)
include(cmake/firmware_outputs.cmake)

add_subdirectory(vendor)
add_subdirectory(platform)
add_subdirectory(drivers)
add_subdirectory(protocols)
add_subdirectory(services)
add_subdirectory(app)

add_executable(firmware.elf
    main.c
    startup_stm32l433xx.s
)

target_link_libraries(firmware.elf
    PRIVATE
        app
        services
        protocols
        drivers
        platform
        vendor_stm32
        project_compiler_options
)

target_linker_script(firmware.elf
    ${CMAKE_SOURCE_DIR}/STM32L433RCTx_FLASH.ld
)

create_firmware_outputs(firmware.elf)
```

### 17.2. `app/CMakeLists.txt`

```cmake
add_library(app STATIC
    app_event_loop.c
    app_state_machine.c
    command_dispatcher.c
)

target_include_directories(app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(app
    PRIVATE
        services
)
```

### 17.3. `services/CMakeLists.txt`

```cmake
add_library(services STATIC
    measurement_manager.c
    flow_computation_service.c
    calibration_service.c
    error_detection_service.c
    data_repository.c
    config_repository.c
    volume_accumulator.c
    storage_service.c
    modbus_service.c
    power_manager.c
    health_monitor.c
)

target_include_directories(services
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(services
    PRIVATE
        drivers
        protocols
        platform
)
```

### 17.4. `drivers/CMakeLists.txt`

```cmake
add_subdirectory(max35103)
add_subdirectory(fram)
add_subdirectory(rs485)

add_library(drivers INTERFACE)

target_link_libraries(drivers
    INTERFACE
        driver_max35103
        driver_fram
        driver_rs485
)
```

### 17.5. `drivers/max35103/CMakeLists.txt`

```cmake
add_library(driver_max35103 STATIC
    max35103.c
)

target_include_directories(driver_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_max35103
    PRIVATE
        platform
)
```

### 17.6. `protocols/CMakeLists.txt`

```cmake
add_subdirectory(modbus)

add_library(protocols INTERFACE)

target_link_libraries(protocols
    INTERFACE
        protocol_modbus
)
```

### 17.7. `protocols/modbus/CMakeLists.txt`

```cmake
add_library(protocol_modbus STATIC
    modbus_rtu.c
    modbus_register_map.c
    modbus_crc.c
)

target_include_directories(protocol_modbus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(protocol_modbus
    PRIVATE
        platform
)
```

### 17.8. `platform/CMakeLists.txt`

```cmake
add_library(platform STATIC
    stm32_hal_port.c
    system_clock.c
)

target_include_directories(platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(platform
    PUBLIC
        vendor_stm32
)
```

---

## 18. Các lỗi thường gặp

### 18.1. Lỗi 1: Root chứa quá nhiều source file

Không nên:

```cmake
add_executable(firmware.elf
    main.c
    app/app_event_loop.c
    services/measurement_manager.c
    drivers/max35103/max35103.c
)
```

Nên:

```cmake
add_executable(firmware.elf
    main.c
)

target_link_libraries(firmware.elf
    PRIVATE
        app
        services
        drivers
)
```

### 18.2. Lỗi 2: Dùng `include_directories()` global

Không nên:

```cmake
include_directories(
    app
    services
    drivers/max35103
    platform
)
```

Nên:

```cmake
target_include_directories(driver_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
```

### 18.3. Lỗi 3: Driver phụ thuộc ngược lên service

Không nên:

```cmake
target_link_libraries(driver_max35103
    PRIVATE
        services
)
```

Vì driver không nên biết business logic.

Nên:

```cmake
target_link_libraries(driver_max35103
    PRIVATE
        platform
)
```

### 18.4. Lỗi 4: Lạm dụng `PUBLIC`

Không nên dùng `PUBLIC` cho mọi dependency.

Ví dụ:

```cmake
target_link_libraries(services
    PUBLIC
        driver_max35103
)
```

Nếu header public của `services` không expose `max35103.h`, thì nên dùng:

```cmake
target_link_libraries(services
    PRIVATE
        driver_max35103
)
```

### 18.5. Lỗi 5: Dùng `file(GLOB ...)` thiếu kiểm soát

Một số project dùng:

```cmake
file(GLOB SOURCES "*.c")
```

Cách này tiện, nhưng có nhược điểm:

- Khi thêm file mới, CMake có thể không tự reconfigure trong một số trường hợp.
- Danh sách source không rõ ràng khi review.
- Dễ build nhầm file thử nghiệm hoặc file backup.

Với firmware production, nên khai báo source rõ ràng:

```cmake
add_library(services STATIC
    measurement_manager.c
    calibration_service.c
    storage_service.c
)
```

Có thể dùng `GLOB` cho prototype, nhưng không nên là mặc định cho firmware nghiêm túc.

---

## 19. Checklist khi thêm module mới

Khi thêm một module mới, nên kiểm tra các điểm sau:

### 19.1. Cấu trúc thư mục

```text
new_module/
├── CMakeLists.txt
├── new_module.c
└── new_module.h
```

Hoặc nếu module lớn:

```text
new_module/
├── CMakeLists.txt
├── include/
│   └── new_module.h
└── src/
    └── new_module.c
```

### 19.2. CMakeLists.txt của module

Module phải có target riêng:

```cmake
add_library(new_module STATIC
    new_module.c
)
```

Hoặc:

```cmake
add_library(new_module STATIC
    src/new_module.c
)
```

### 19.3. Include directory

```cmake
target_include_directories(new_module
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
```

Hoặc:

```cmake
target_include_directories(new_module
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
```

### 19.4. Dependency

Chỉ khai báo dependency thật sự cần:

```cmake
target_link_libraries(new_module
    PRIVATE
        platform
)
```

Không link bừa toàn bộ project.

### 19.5. Thêm module vào parent CMakeLists.txt

Ví dụ nếu module nằm trong `drivers/`:

```cmake
add_subdirectory(new_driver)

target_link_libraries(drivers
    INTERFACE
        driver_new
)
```

---

## 20. Checklist review CMake

Khi review CMake trong project firmware, nên kiểm tra:

- Root `CMakeLists.txt` có đang quá dài không?
- Root có chứa source file chi tiết của module không?
- Mỗi module đã có target riêng chưa?
- Có đang dùng `include_directories()` global không?
- Có đang dùng `add_definitions()` global không?
- Dependency có đi đúng chiều kiến trúc không?
- Driver có phụ thuộc lên service/app không?
- App có gọi trực tiếp driver không?
- Có lạm dụng `PUBLIC` không?
- Header public/private đã rõ chưa?
- Có thể build unit test cho service độc lập không?
- Có thể thay platform thật bằng platform mock không?
- Tên target có nhất quán không?
- Có thể thêm driver mới mà không sửa quá nhiều file không?

---

## 21. Quy ước khuyến nghị cho firmware trong project này

Đối với firmware dạng smart ultrasonic water meter, nên áp dụng quy ước sau:

### 21.1. Root

Root chỉ điều phối:

```text
firmware/CMakeLists.txt
```

Chứa:

- Project name.
- C standard.
- Toolchain/common options.
- `add_subdirectory`.
- `firmware.elf`.
- Linker script.
- Output `.bin`, `.hex`.

### 21.2. App

```text
app/CMakeLists.txt
```

Target:

```text
app
```

Phụ thuộc:

```text
services
```

### 21.3. Services

```text
services/CMakeLists.txt
```

Target:

```text
services
```

Phụ thuộc:

```text
drivers
protocols
platform
```

### 21.4. Drivers

```text
drivers/CMakeLists.txt
```

Target gom nhóm:

```text
drivers
```

Driver con:

```text
driver_max35103
driver_fram
driver_rs485
driver_lcd
```

Phụ thuộc:

```text
platform
```

### 21.5. Protocols

```text
protocols/CMakeLists.txt
```

Target gom nhóm:

```text
protocols
```

Protocol con:

```text
protocol_modbus
```

Phụ thuộc:

```text
platform
```

### 21.6. Platform

```text
platform/CMakeLists.txt
```

Target:

```text
platform
```

Phụ thuộc:

```text
STM32 HAL
CMSIS
BSP
```

---

## 22. Kết luận

Cách tổ chức CMake được khuyến nghị cho firmware module hóa là:

```text
Root CMakeLists.txt chỉ điều phối project.
Mỗi module hoặc thư mục chức năng có CMakeLists.txt riêng.
Mỗi module tự khai báo target, source, include path và dependency của nó.
```

Cách này tốt hơn việc gom toàn bộ vào root vì:

- Cấu trúc build phản ánh đúng kiến trúc phần mềm.
- Dễ bảo trì khi project lớn.
- Dễ thêm module mới.
- Dễ unit test.
- Dễ simulation trên PC.
- Dễ kiểm soát dependency giữa các tầng.
- Giảm rủi ro phá vỡ kiến trúc firmware.

Quy tắc thực hành nên dùng:

```text
1. Root chỉ add_subdirectory và link target lớn.
2. Mỗi module tự add_library.
3. Không dùng include_directories global.
4. Dùng target_include_directories.
5. Dùng target_link_libraries.
6. Phân biệt rõ PRIVATE, PUBLIC, INTERFACE.
7. Dependency phải đi một chiều từ app xuống platform.
8. Driver không phụ thuộc service/app.
9. Service không phụ thuộc app.
10. Platform không chứa business logic.
```

Với cách này, hệ thống CMake sẽ trở thành một phần của kiến trúc firmware, thay vì chỉ là một file build script tạm thời.
