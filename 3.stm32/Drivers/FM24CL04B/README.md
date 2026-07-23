# Driver FM24CL04B cho STM32 HAL I²C

## 1. Giới thiệu

Module này cung cấp driver dạng blocking để STM32 truy cập bộ nhớ F-RAM
FM24CL04B thông qua ngoại vi I²C của STM32 HAL.

FM24CL04B có các đặc điểm chính:

- Dung lượng: 4 Kbit, tương đương 4096 bit hoặc 512 byte.
- Tổ chức bộ nhớ: `512 × 8 bit`.
- Giao tiếp: I²C, hỗ trợ tối đa 1 MHz.
- Điện áp hoạt động: 2,7 V đến 3,65 V.
- Ghi dữ liệu ở tốc độ bus, không cần chờ chu kỳ ghi như EEPROM.
- Không có bộ đệm ghi theo page kiểu EEPROM.
- Toàn bộ vùng nhớ có thể được bảo vệ bằng chân `WP`.

Driver cung cấp:

- Kiểm tra sự hiện diện của FM24CL04B trên bus.
- Đọc và ghi một hoặc nhiều byte.
- Tự động xử lý bit địa chỉ thứ 9 của bộ nhớ.
- Tự động chia giao dịch khi dữ liệu đi qua biên `0x0FF → 0x100`.
- Kiểm tra địa chỉ, độ dài và con trỏ dữ liệu.
- Phân biệt lỗi I/O, timeout, bus busy và write protect.
- Cho phép lựa chọn `hi2c1`, `hi2c2` hoặc một I²C handle khác qua CMake.
- Cho phép cấu hình hai chân chọn thiết bị `A2` và `A1`.

## 2. Thành phần module

```text
fram_driver/
├── CMakeLists.txt
├── fram_driver.c
├── fram_driver.h
└── README.md
```

| Tệp | Chức năng |
|---|---|
| `fram_driver.h` | Khai báo cấu hình, trạng thái lỗi và API công khai. |
| `fram_driver.c` | Hiện thực truy cập FM24CL04B bằng STM32 HAL I²C. |
| `CMakeLists.txt` | Tạo target CMake `fram_driver` và truyền cấu hình phần cứng. |
| `README.md` | Tài liệu thiết kế, tích hợp và sử dụng driver. |

## 3. Kết nối phần cứng

### 3.1 Bảng kết nối đề xuất

| Chân FM24CL04B | Kết nối | Ghi chú |
|---|---|---|
| `VDD` | 3,3 V | Phải nằm trong khoảng điện áp cho phép của IC. |
| `VSS` | GND | Nối chung mass với STM32. |
| `SDA` | STM32 I²C SDA | Cần điện trở kéo lên `VDD`. |
| `SCL` | STM32 I²C SCL | Cần điện trở kéo lên `VDD`. |
| `A2` | GND hoặc 3,3 V | Chọn địa chỉ thiết bị. |
| `A1` | GND hoặc 3,3 V | Chọn địa chỉ thiết bị. |
| `WP` | GND nếu cho phép ghi | Kéo lên `VDD` để bảo vệ toàn bộ vùng nhớ. |
| `NC` | Không kết nối | Không sử dụng. |

Nên đặt một tụ gốm `100 nF` giữa `VDD` và `VSS`, càng gần IC càng tốt.

FM24CL04B không có chân `A0`. Bit thường được gọi là `A0` trong vị trí thấp
nhất của địa chỉ slave thực chất là bit địa chỉ bộ nhớ `A8`.

### 3.2 Điện trở kéo lên SDA và SCL

Giá trị điện trở kéo lên phải thỏa mãn đồng thời giới hạn dưới và giới hạn
trên:

```text
Rpull-up(min) = (VDD - VOL(max)) / IOL

Rpull-up(max) = tr(max) / (0,8473 × Cbus)
```

Trong đó:

- `VDD`: điện áp kéo lên bus.
- `VOL(max)`: điện áp mức thấp lớn nhất được chấp nhận.
- `IOL`: dòng sink khi thiết bị kéo bus xuống thấp.
- `tr(max)`: thời gian sườn lên tối đa của chế độ I²C.
- `Cbus`: tổng điện dung của đường mạch, chân IC và các thiết bị trên bus.

Ví dụ với:

```text
VDD      = 3,3 V
VOL(max) = 0,4 V
IOL      = 2 mA
```

Ta có:

```text
Rpull-up(min) = (3,3 - 0,4) / 0,002
              = 1450 Ω
              ≈ 1,45 kΩ
```

Nếu chạy Fast-mode 400 kHz, giả sử:

```text
tr(max) = 300 ns
Cbus    = 100 pF
```

Thì:

```text
Rpull-up(max) = 300 × 10^-9 / (0,8473 × 100 × 10^-12)
              ≈ 3,54 kΩ
```

Trong ví dụ này có thể chọn `2,2 kΩ` hoặc `3,3 kΩ`. Nếu bus ngắn và điện dung
chỉ khoảng `50 pF`, `Rpull-up(max)` tăng lên khoảng `7,08 kΩ`, vì vậy
`4,7 kΩ` thường phù hợp. Giá trị cuối cùng cần được kiểm tra theo điện dung
thực tế, tốc độ bus và thông số GPIO I²C của STM32.

## 4. Tổ chức bộ nhớ

### 4.1 Tính dung lượng

Dung lượng được công bố là 4 Kbit:

```text
4 Kbit = 4 × 1024 bit
       = 4096 bit

Số byte = 4096 / 8
        = 512 byte
```

Do đó địa chỉ logic hợp lệ là:

```text
0 ≤ address ≤ 511
```

Hoặc dưới dạng hexadecimal:

```text
0x000 ≤ address ≤ 0x1FF
```

Hai hằng số tương ứng trong driver:

```c
#define FRAM_SIZE_BYTES         512u
#define FRAM_ADDRESS_BLOCK_SIZE 256u
```

### 4.2 Hai khối địa chỉ 256 byte

FM24CL04B sử dụng một word address dài 8 bit. Một byte word address chỉ biểu
diễn được 256 vị trí, do đó bit thứ 9 `A8` được đặt trong slave address I²C.

| Vùng địa chỉ logic | A8 | Word address | Khối |
|---|---:|---|---|
| `0x000–0x0FF` | 0 | `0x00–0xFF` | P0 |
| `0x100–0x1FF` | 1 | `0x00–0xFF` | P1 |

Tên `FRAM_PAGE_SIZE` được giữ lại để tương thích với mã cũ. Đây là kích thước
khối địa chỉ được chọn bởi `A8`, không phải giới hạn page-write của EEPROM.

## 5. Cách tính địa chỉ I²C

### 5.1 Cấu trúc địa chỉ 7 bit

Địa chỉ slave 7 bit của FM24CL04B có cấu trúc:

```text
Bit:    6 5 4 3  2  1  0
        1 0 1 0  A2 A1 A8
```

Trong đó:

- `A2`, `A1`: mức logic trên hai chân chọn thiết bị.
- `A8`: bit số 8 của địa chỉ bộ nhớ logic.

Giá trị chọn thiết bị được tính bằng:

```text
device_select = (A2 << 1) | A1
              = 2 × A2 + A1
```

Địa chỉ cơ sở:

```text
base_address = 0x50 | (device_select << 1)
```

Từ địa chỉ logic `address`, tính:

```text
A8           = (address >> 8) & 0x01
slave_7bit   = base_address | A8
word_address = address & 0xFF
```

STM32 HAL yêu cầu địa chỉ slave được dịch trái một bit:

```text
hal_device_address = slave_7bit << 1
```

### 5.2 Bảng địa chỉ theo A2 và A1

| A2 | A1 | `FRAM_DEVICE_SELECT` | P0 | P1 | Địa chỉ truyền cho HAL |
|---:|---:|---:|---:|---:|---|
| 0 | 0 | 0 | `0x50` | `0x51` | `0xA0`, `0xA2` |
| 0 | 1 | 1 | `0x52` | `0x53` | `0xA4`, `0xA6` |
| 1 | 0 | 2 | `0x54` | `0x55` | `0xA8`, `0xAA` |
| 1 | 1 | 3 | `0x56` | `0x57` | `0xAC`, `0xAE` |

### 5.3 Ví dụ tính địa chỉ

Giả sử `A2 = 0`, `A1 = 0` và cần truy cập địa chỉ logic `0x17A`:

```text
address      = 0x17A
A8           = (0x17A >> 8) & 1 = 1
base_address = 0x50
slave_7bit   = 0x50 | 1 = 0x51
word_address = 0x17A & 0xFF = 0x7A
HAL address  = 0x51 << 1 = 0xA2
```

Lệnh HAL tương ứng sử dụng:

```c
HAL_I2C_Mem_Read(&hi2c1,
                 0xA2,
                 0x7A,
                 I2C_MEMADD_SIZE_8BIT,
                 buffer,
                 length,
                 timeout);
```

Không truyền trực tiếp `0x50` hoặc `0x51` cho STM32 HAL vì HAL cần địa chỉ đã
dịch trái một bit. Driver tự thực hiện phép dịch này.

## 6. Kiểm tra vùng nhớ và chia giao dịch

### 6.1 Điều kiện vùng nhớ hợp lệ

Một giao dịch có độ dài lớn hơn 0 hợp lệ khi:

```text
address < 512

length ≤ 512 - address
```

Ví dụ:

```text
address = 500
length  = 12

512 - 500 = 12 → hợp lệ
```

Nhưng:

```text
address = 500
length  = 13

13 > 512 - 500 → FRAM_OUT_OF_RANGE
```

### 6.2 Công thức tính kích thước từng chunk

Tại mỗi vòng lặp, driver tính:

```text
word_address = current_address & 0xFF

bytes_to_boundary = 256 - word_address

chunk = min(remaining, bytes_to_boundary)
```

Ví dụ ghi 20 byte từ địa chỉ 250:

```text
address             = 250 = 0x0FA
word_address        = 250
bytes_to_boundary   = 256 - 250 = 6 byte
first_chunk         = min(20, 6) = 6 byte
second_chunk        = 20 - 6 = 14 byte
```

Driver thực hiện:

```text
Giao dịch 1: slave 0x50, word address 0xFA, length 6
Giao dịch 2: slave 0x51, word address 0x00, length 14
```

Việc chia này làm rõ ràng quan hệ giữa `A8` và slave address, đồng thời tránh
phụ thuộc vào cách HAL hoặc phần cứng xử lý một giao dịch đi qua biên khối.

## 7. Các tham số cấu hình

### 7.1 `FRAM_DEVICE_SELECT`

Mã hóa mức logic hai chân `A2` và `A1`:

```text
FRAM_DEVICE_SELECT = (A2 << 1) | A1
```

Giá trị mặc định:

```c
#define FRAM_DEVICE_SELECT 0u
```

Tương ứng `A2 = GND`, `A1 = GND` và hai địa chỉ 7 bit là `0x50`, `0x51`.

### 7.2 `FRAM_I2C_HANDLE`

Chọn I²C handle do CubeMX tạo. Giá trị mặc định là:

```c
#define FRAM_I2C_HANDLE hi2c1
```

Có thể đổi thành `hi2c2`, `hi2c3` hoặc handle phù hợp với MCU đang sử dụng.

### 7.3 `FRAM_TIMEOUT_MS`

Timeout áp dụng cho từng lời gọi STM32 HAL:

```c
#define FRAM_TIMEOUT_MS 100u
```

Đây không phải thời gian chờ ghi của F-RAM. Nó là thời gian tối đa driver chờ
một giao dịch I²C blocking hoàn thành.

### 7.4 `FRAM_READY_TRIALS`

Số lần HAL thử địa chỉ khi kiểm tra thiết bị:

```c
#define FRAM_READY_TRIALS 3u
```

`FRAM_ProbeStatus()` kiểm tra hai địa chỉ P0 và P1. Trong trường hợp bus không
phản hồi, thời gian chờ xấu nhất có thể xấp xỉ:

```text
2 address blocks × 3 trials × 100 ms = 600 ms
```

Thời gian thực tế phụ thuộc cách phiên bản STM32 HAL xử lý timeout và số trial.

## 8. Ước lượng thời gian truyền

Mỗi byte I²C cần 8 bit dữ liệu và 1 bit ACK/NACK, tương đương khoảng 9 xung SCL.

### 8.1 Ghi N byte

Một giao dịch ghi bộ nhớ gồm xấp xỉ:

```text
Slave address + ACK = 9 clock
Word address  + ACK = 9 clock
N data byte         = 9 × N clock

Total_write ≈ 18 + 9N clock
```

Thời gian lý tưởng:

```text
twrite ≈ (18 + 9N) / fSCL
```

Ví dụ ghi 16 byte ở 400 kHz:

```text
Clock count = 18 + 9 × 16 = 162 clock
twrite      = 162 / 400000
            = 405 µs
```

### 8.2 Đọc ngẫu nhiên N byte

`HAL_I2C_Mem_Read()` cần pha đặt word address và pha đọc:

```text
Slave write + ACK = 9 clock
Word address + ACK = 9 clock
Slave read + ACK  = 9 clock
N data byte       = 9 × N clock

Total_read ≈ 27 + 9N clock
```

Thời gian lý tưởng:

```text
tread ≈ (27 + 9N) / fSCL
```

Ví dụ đọc 16 byte ở 400 kHz:

```text
Clock count = 27 + 9 × 16 = 171 clock
tread       = 171 / 400000
            = 427,5 µs
```

Các công thức trên chưa tính thời gian chạy HAL, START/STOP, lập lịch RTOS,
ngắt và thời gian CPU xử lý giữa các chunk.

### 8.3 Ước lượng giao dịch 256 byte

Ở 400 kHz:

```text
Write 256 byte ≈ (18 + 9 × 256) / 400000 ≈ 5,81 ms
Read  256 byte ≈ (27 + 9 × 256) / 400000 ≈ 5,83 ms
```

Ở 100 kHz, thời gian tương ứng khoảng `23,2 ms` cho mỗi chunk 256 byte. Do đó
timeout mặc định `100 ms` cho từng chunk có biên an toàn tương đối lớn ở cả
100 kHz và 400 kHz.

## 9. Mã trạng thái

```c
typedef enum {
    FRAM_OK = 0,
    FRAM_OUT_OF_RANGE,
    FRAM_IO_ERROR,
    FRAM_INVALID_PARAM,
    FRAM_WRITE_PROTECTED,
    FRAM_TIMEOUT,
    FRAM_BUSY
} FramStatus;
```

| Trạng thái | Ý nghĩa | Nguyên nhân thường gặp |
|---|---|---|
| `FRAM_OK` | Thao tác thành công. | Thiết bị ACK và HAL hoàn thành giao dịch. |
| `FRAM_OUT_OF_RANGE` | Vùng truy cập vượt `0x000–0x1FF`. | Sai địa chỉ hoặc độ dài. |
| `FRAM_IO_ERROR` | Lỗi I²C không thuộc các nhóm riêng. | Mất ACK, nhiễu bus, sai địa chỉ, thiết bị mất nguồn. |
| `FRAM_INVALID_PARAM` | Con trỏ dữ liệu không hợp lệ. | `buffer == NULL` trong giao dịch có `length > 0`. |
| `FRAM_WRITE_PROTECTED` | Giao dịch ghi bị WP chặn. | Chân `WP` đang ở mức cao. |
| `FRAM_TIMEOUT` | HAL hết thời gian chờ. | Bus bị giữ thấp, tốc độ/cấu hình sai hoặc thiết bị không phản hồi. |
| `FRAM_BUSY` | HAL báo ngoại vi đang bận. | I²C đang được task hoặc driver khác sử dụng. |

Việc nhận diện `FRAM_WRITE_PROTECTED` là theo cơ chế NACK dữ liệu rồi probe lại
thiết bị. Nếu thiết bị vẫn ACK địa chỉ sau lỗi ACK-failure, driver xem đây là
trường hợp write protect. Trong môi trường nhiễu mạnh, nên kiểm tra thêm trạng
thái chân WP ở tầng board support.

## 10. Chức năng các hàm công khai

### 10.1 `FRAM_Init()`

```c
FramStatus FRAM_Init(void);
```

Chức năng:

- Gọi `FRAM_ProbeStatus()`.
- Kiểm tra cả địa chỉ P0 và P1.
- Trả về nguyên nhân lỗi chi tiết.

Yêu cầu:

- Clock và GPIO đã được khởi tạo.
- `MX_I2C1_Init()` hoặc hàm khởi tạo I²C tương ứng đã chạy.
- Hai chân `A2`, `A1` khớp với `FRAM_DEVICE_SELECT`.

Ví dụ:

```c
MX_I2C1_Init();

FramStatus status = FRAM_Init();
if (status != FRAM_OK) {
    Error_Handler();
}
```

### 10.2 `FRAM_ProbeStatus()`

```c
FramStatus FRAM_ProbeStatus(void);
```

Chức năng:

- Gửi kiểm tra ready tới P0 và P1.
- Trả về `FRAM_OK`, `FRAM_TIMEOUT`, `FRAM_BUSY` hoặc `FRAM_IO_ERROR`.

Nên dùng hàm này khi cần ghi log hoặc chẩn đoán nguyên nhân thiết bị không được
phát hiện.

### 10.3 `FRAM_Probe()`

```c
bool FRAM_Probe(void);
```

Đây là phiên bản đơn giản của `FRAM_ProbeStatus()`:

```text
true  → cả hai địa chỉ ACK
false → có ít nhất một địa chỉ không ACK hoặc HAL báo lỗi
```

Ví dụ:

```c
if (!FRAM_Probe()) {
    /* Kiểm tra nguồn, dây SDA/SCL, A2/A1 và điện trở pull-up. */
}
```

### 10.4 `FRAM_Read()`

```c
FramStatus FRAM_Read(uint16_t address,
                     uint8_t *buffer,
                     uint16_t length);
```

Tham số:

| Tham số | Ý nghĩa |
|---|---|
| `address` | Địa chỉ logic bắt đầu, từ `0x000` đến `0x1FF`. |
| `buffer` | Bộ đệm nhận dữ liệu. |
| `length` | Số byte cần đọc. |

Hành vi:

- Kiểm tra tham số và vùng nhớ.
- Tính slave address và word address.
- Tự chia giao dịch khi đi qua biên 256 byte.
- Dừng ngay khi một chunk gặp lỗi.
- `length == 0` là no-op nếu địa chỉ hợp lệ.

Ví dụ:

```c
uint8_t data[32];

FramStatus status = FRAM_Read(0x0080u, data, (uint16_t)sizeof(data));
if (status != FRAM_OK) {
    /* Xử lý lỗi. */
}
```

### 10.5 `FRAM_Write()`

```c
FramStatus FRAM_Write(uint16_t address,
                      const uint8_t *data,
                      uint16_t length);
```

Tham số:

| Tham số | Ý nghĩa |
|---|---|
| `address` | Địa chỉ logic bắt đầu, từ `0x000` đến `0x1FF`. |
| `data` | Bộ đệm chứa dữ liệu cần ghi. |
| `length` | Số byte cần ghi. |

Hành vi:

- Kiểm tra tham số và vùng nhớ.
- Tự chia dữ liệu khi qua biên `0x0FF/0x100`.
- Không delay và không ACK-polling sau khi ghi thành công.
- Phát hiện write-protect theo ACK-failure và probe lại thiết bị.
- Dừng ngay khi một chunk gặp lỗi; các chunk trước đó có thể đã được ghi.

Ví dụ:

```c
static const uint8_t config[] = {
    0x46, 0x52, 0x41, 0x4D,
    0x01, 0x00, 0x00, 0x00
};

FramStatus status = FRAM_Write(0x0020u,
                               config,
                               (uint16_t)sizeof(config));
if (status != FRAM_OK) {
    /* Xử lý lỗi. */
}
```

## 11. Tích hợp với STM32CubeMX

### 11.1 Cấu hình cơ bản

Trong STM32CubeMX:

1. Bật ngoại vi I²C cần sử dụng, ví dụ `I2C1`.
2. Chọn chế độ I²C 7-bit.
3. Chọn tốc độ ban đầu 100 kHz hoặc 400 kHz.
4. Bật analog filter nếu thiết kế không có yêu cầu đặc biệt.
5. Kiểm tra chân SDA/SCL được cấu hình alternate-function open-drain.
6. Lắp điện trở kéo lên ngoài phù hợp cho SDA và SCL.
7. Generate code và gọi `MX_I2C1_Init()` trước `FRAM_Init()`.

Driver sử dụng API blocking nên không yêu cầu bật I²C event/error IRQ. Nếu một
module khác dùng HAL interrupt hoặc DMA trên cùng ngoại vi, cần phối hợp quyền
sở hữu bus và trạng thái HAL cẩn thận.

### 11.2 Khai báo I²C handle

Driver mặc định tham chiếu tới:

```c
extern I2C_HandleTypeDef hi2c1;
```

Handle thực tế phải được định nghĩa đúng một lần trong mã do CubeMX tạo, thường
là `main.c` hoặc `i2c.c`.

## 12. Tích hợp CMake

Giả sử module được đặt tại:

```text
src/drivers/fram/
```

Trong `CMakeLists.txt` gốc:

```cmake
add_subdirectory(src/drivers/fram)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    fram_driver
)
```

Cấu hình cho `A2 = 0`, `A1 = 0`, sử dụng `hi2c1`:

```bash
cmake -S . -B build \
  -DFRAM_DEVICE_SELECT=0 \
  -DFRAM_I2C_HANDLE=hi2c1
```

Nếu FM24CL04B nối `A2 = 1`, `A1 = 0` và dùng `I2C2`:

```bash
cmake -S . -B build \
  -DFRAM_DEVICE_SELECT=2 \
  -DFRAM_I2C_HANDLE=hi2c2
```

Cũng có thể đặt cache variable trước `add_subdirectory()`:

```cmake
set(FRAM_DEVICE_SELECT "0" CACHE STRING "" FORCE)
set(FRAM_I2C_HANDLE "hi2c1" CACHE STRING "" FORCE)

add_subdirectory(src/drivers/fram)
```

## 13. Ví dụ sử dụng hoàn chỉnh

Ví dụ sau ghi 20 byte từ địa chỉ `0x0FA`. Vùng dữ liệu đi qua biên
`0x0FF → 0x100`, do đó driver tự chia thành hai giao dịch.

```c
#include "fram_driver.h"
#include <string.h>

static bool fram_example(void)
{
    uint8_t tx[20];
    uint8_t rx[20];

    for (size_t i = 0u; i < sizeof(tx); ++i) {
        tx[i] = (uint8_t)(0xA0u + i);
    }

    memset(rx, 0, sizeof(rx));

    FramStatus status = FRAM_Init();
    if (status != FRAM_OK) {
        return false;
    }

    status = FRAM_Write(0x00FAu, tx, (uint16_t)sizeof(tx));
    if (status != FRAM_OK) {
        return false;
    }

    status = FRAM_Read(0x00FAu, rx, (uint16_t)sizeof(rx));
    if (status != FRAM_OK) {
        return false;
    }

    return memcmp(tx, rx, sizeof(tx)) == 0;
}
```

Không cần `HAL_Delay()` giữa `FRAM_Write()` và `FRAM_Read()`.

## 14. Lưu số nguyên nhiều byte

FM24CL04B chỉ lưu các byte. Khi cần lưu một số 16-bit hoặc 32-bit, firmware phải
quy định thứ tự byte.

Ví dụ lưu `uint32_t` theo little-endian:

```c
static FramStatus fram_write_u32_le(uint16_t address, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)(value >> 0u),
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 24u)
    };

    return FRAM_Write(address, bytes, (uint16_t)sizeof(bytes));
}

static FramStatus fram_read_u32_le(uint16_t address, uint32_t *value)
{
    if (value == NULL) {
        return FRAM_INVALID_PARAM;
    }

    uint8_t bytes[4];
    FramStatus status = FRAM_Read(address,
                                  bytes,
                                  (uint16_t)sizeof(bytes));
    if (status != FRAM_OK) {
        return status;
    }

    *value = ((uint32_t)bytes[0] << 0u)
           | ((uint32_t)bytes[1] << 8u)
           | ((uint32_t)bytes[2] << 16u)
           | ((uint32_t)bytes[3] << 24u);

    return FRAM_OK;
}
```

Tránh phụ thuộc hoàn toàn vào việc ghi trực tiếp `struct` nếu dữ liệu phải tồn
tại qua nhiều phiên bản firmware, vì padding, alignment, endianness và cách bố
trí trường có thể thay đổi.

## 15. Gợi ý thiết kế memory map

Một ví dụ phân vùng 512 byte:

| Vùng | Kích thước | Nội dung gợi ý |
|---|---:|---|
| `0x000–0x00F` | 16 byte | Magic number, version, trạng thái dữ liệu. |
| `0x010–0x07F` | 112 byte | Cấu hình hệ thống. |
| `0x080–0x0FF` | 128 byte | Dữ liệu calibration. |
| `0x100–0x1FF` | 256 byte | Counter hoặc nhật ký vòng. |

Nên định nghĩa memory map bằng macro hoặc enum tại một module cấp cao hơn thay
vì rải địa chỉ số trực tiếp trong application code.

Ví dụ:

```c
enum {
    FRAM_ADDR_HEADER      = 0x000u,
    FRAM_ADDR_CONFIG      = 0x010u,
    FRAM_ADDR_CALIBRATION = 0x080u,
    FRAM_ADDR_LOG         = 0x100u
};
```

## 16. Độ toàn vẹn dữ liệu và mất nguồn

F-RAM ghi từng byte ngay trong giao dịch I²C. Tuy nhiên, một giao dịch nhiều
byte vẫn có thể bị dừng giữa chừng nếu hệ thống mất nguồn hoặc bus gặp lỗi.
Điều đó có nghĩa là một phần đầu dữ liệu có thể đã được cập nhật trong khi phần
còn lại chưa được ghi.

Đối với cấu hình quan trọng, nên sử dụng:

- Magic number.
- Version của cấu trúc dữ liệu.
- Length.
- CRC.
- Hai slot A/B.
- Sequence counter.
- Valid marker được ghi sau cùng.

Quy trình ghi an toàn gợi ý:

1. Chọn slot cũ hơn hoặc slot không hoạt động.
2. Ghi payload mới.
3. Ghi CRC và sequence counter.
4. Đọc lại để xác minh nếu ứng dụng yêu cầu.
5. Ghi valid marker sau cùng.
6. Khi khởi động, chọn slot hợp lệ có sequence counter mới nhất.

## 17. RTOS, ngắt và quyền sở hữu bus

`FRAM_Read()`, `FRAM_Write()` và các hàm probe sử dụng STM32 HAL blocking.

Do đó:

- Không gọi các hàm này từ ISR.
- Nếu nhiều task dùng cùng I²C, phải bảo vệ toàn bộ giao dịch bằng mutex.
- Không bắt đầu HAL DMA/IT trên cùng I²C khi một giao dịch blocking đang chạy.
- `FRAM_BUSY` thường cho biết ngoại vi đang được tác vụ khác sử dụng hoặc HAL
  chưa trở lại trạng thái ready.

Driver không tự tạo mutex vì cơ chế đồng bộ phụ thuộc hệ điều hành và kiến trúc
firmware của ứng dụng.

## 18. Chẩn đoán lỗi

### `FRAM_Init()` trả về `FRAM_IO_ERROR`

Kiểm tra:

1. Nguồn 3,3 V và GND.
2. SDA/SCL có điện trở kéo lên.
3. SDA và SCL không bị đảo.
4. `A2`, `A1` khớp `FRAM_DEVICE_SELECT`.
5. Đã gọi `MX_I2C1_Init()`.
6. `FRAM_I2C_HANDLE` đúng với ngoại vi đang nối.
7. Logic analyzer có thấy địa chỉ `0x50/0x51` hoặc cặp địa chỉ tương ứng hay
   không.

### `FRAM_Write()` trả về `FRAM_WRITE_PROTECTED`

- Kiểm tra chân `WP`.
- Muốn ghi: nối `WP` xuống GND.
- Muốn khóa toàn bộ bộ nhớ: nối `WP` lên VDD.

### Driver trả về `FRAM_TIMEOUT`

- Kiểm tra SDA hoặc SCL có bị giữ thấp không.
- Kiểm tra giá trị pull-up.
- Giảm tốc độ I²C để thử nghiệm.
- Kiểm tra nguồn thiết bị.
- Kiểm tra cấu hình timing I²C do CubeMX sinh ra.

### Driver trả về `FRAM_BUSY`

- Tìm giao dịch I²C khác đang chạy.
- Kiểm tra task có giữ mutex quá lâu không.
- Kiểm tra DMA/interrupt transfer chưa kết thúc.

### Dữ liệu sai khi đi qua `0x0FF/0x100`

- Chạy test ghi/đọc bắt đầu tại `0x0FA`, length 20.
- Quan sát hai slave address liên tiếp bằng logic analyzer.
- Kiểm tra P0 dùng địa chỉ có `A8 = 0`, P1 dùng địa chỉ có `A8 = 1`.

## 19. Checklist kiểm thử phần cứng

- [ ] `FRAM_Init()` trả về `FRAM_OK`.
- [ ] Probe đúng cả P0 và P1.
- [ ] Ghi/đọc một byte tại `0x000`.
- [ ] Ghi/đọc một byte tại `0x0FF`.
- [ ] Ghi/đọc một byte tại `0x100`.
- [ ] Ghi/đọc một byte tại `0x1FF`.
- [ ] Ghi/đọc một block qua biên `0x0FF/0x100`.
- [ ] Ghi/đọc toàn bộ 512 byte.
- [ ] Kiểm tra `FRAM_OUT_OF_RANGE` tại địa chỉ 512.
- [ ] Kiểm tra dữ liệu sau khi tắt và bật nguồn.
- [ ] Kéo WP lên cao và xác nhận thao tác ghi bị từ chối.
- [ ] Kéo WP xuống thấp và xác nhận ghi hoạt động lại.
- [ ] Chạy lặp nhiều lần để kiểm tra tính ổn định của bus.

## 20. Giới hạn hiện tại

- API là blocking, chưa hỗ trợ HAL interrupt hoặc DMA.
- Driver sử dụng một I²C handle được chọn tại thời điểm biên dịch.
- Driver không cung cấp mutex nội bộ.
- Driver không cung cấp CRC, serialization hoặc memory-map cấp ứng dụng.
- Driver không tự phục hồi bus khi SDA/SCL bị giữ thấp.

Các chức năng trên nên được đặt ở tầng cao hơn hoặc mở rộng thành một phiên bản
driver bất đồng bộ khi hệ thống yêu cầu.

## 21. Tài liệu tham khảo

- [Infineon FM24CL04B product page](https://www.infineon.com/part/FM24CL04B-G)
- [Infineon AN96578 — Designing with I²C F-RAM](https://www.infineon.com/assets/row/public/documents/10/42/infineon-an96578-designing-with-i2c-f-ramtm-applicationnote-en.pdf)
- STM32 HAL I²C documentation tương ứng với dòng MCU đang sử dụng.
