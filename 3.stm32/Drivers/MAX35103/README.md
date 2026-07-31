# MAX35103 Driver for STM32

This driver helps integrate the MAX35103 into STM32 projects using STM32 HAL.

The library supports:

- hardware reset, `INIT`, configuration, and MAX35103 communication checks;
- direct TOF measurement using `TOF_DIFF`;
- Event Timing operation through the `MAX_INT` pin;
- TOF and temperature result acquisition;
- PT100/PT1000 RTD conversion according to IEC 60751;
- FIFO queues for TOF and temperature results;
- blocking SPI or asynchronous DMA;
- timeout, cancellation, and recovery handling;
- shared SPI bus lock/unlock.

> The driver does not calculate flow rate, manage storage or telemetry, or write the MAX35103 configuration flash. The profile must be loaded again after every reset or power loss.

---

## 1. Library Architecture

The driver library is divided into two layers:

```text
Application
    │
    ├── max35103.c/.h               Core driver and state machine
    │
    └── max35103_stm32_hal.c/.h     STM32 HAL adapter
```

Main files:

```text
driver/
├── CMakeLists.txt
├── max35103.c
├── max35103.h
├── max35103_stm32_hal.c
└── max35103_stm32_hal.h
```

| Component | Role |
|---|---|
| `max35103.c/.h` | MAX35103 protocol, state machine, TOF, RTD, and public API |
| `max35103_stm32_hal.c/.h` | Connects the core driver to STM32 HAL |

`max35103.c/.h` does not call `HAL_*` directly, does not hard-code `hspi1`, and does not depend on the pin mapping of a specific board.

The application may manage the driver instances in `main.c`, a board layer, an application layer, or a dedicated module. The driver does not impose an overall firmware architecture.

---

## 2. Hardware Requirements

### 2.1 SPI

Configure SPI as follows:

- Master
- Full duplex
- Data size: 8 bits
- MSB first
- SPI mode 1
  - `CPOL = 0`
  - `CPHA = 1`
- Software NSS
- SPI frequency within the limits specified in the datasheet

The HAL adapter automatically drives NSS low before each SPI transaction and high after the transaction completes.

### 2.2 Required Pins

Example mapping for the STM32L433RCT6:

| Signal | STM32 Pin | Configuration |
|---|---|---|
| `MAX_NSS` | PA4 | Output push-pull, idle HIGH |
| `MAX_RST` | PC4 | Output push-pull, active LOW |
| `MAX_INT` | PC5 | Input pull-up, falling-edge EXTI |
| `MAX_CMP` | PB0 | Input, no pull |
| `MAX_WDO` | PB2 | Input pull-up, falling-edge EXTI |
| `MAX_SCK` | PA5 | GPIO_AF5_SPI1 |
| `MAX_MISO` | PA6 | GPIO_AF5_SPI1 |
| `MAX_MOSI` | PA7 | GPIO_AF5_SPI1 |

`MAX_INT` should only be used to record an event. Do not perform long blocking SPI sequences inside the EXTI callback.

### 2.3 SPI DMA on STM32L433

CubeMX configuration:

| DMA Request | Channel | Direction | Data Width | Mode |
|---|---|---|---|---|
| `SPI1_RX` | DMA1_Channel2 | Peripheral → Memory | Byte | Normal |
| `SPI1_TX` | DMA1_Channel3 | Memory → Peripheral | Byte | Normal |

Enable the following NVIC interrupts:

- `DMA1_Channel2_IRQn`
- `DMA1_Channel3_IRQn`

Initialization order:

```text
MX_GPIO_Init()
→ MX_DMA_Init()
→ MX_SPI1_Init()
```

---

## 3. Required Objects

Each MAX35103 instance requires three objects:

```c
static Max35103Stm32HalContext max_hal;
static Max35103Transport max_transport;
static Max35103Driver max_driver;
```

| Object | Function |
|---|---|
| `Max35103Stm32HalContext` | Stores the project SPI handle and GPIO resources |
| `Max35103Transport` | Contains transport callbacks used by the core driver |
| `Max35103Driver` | Stores state, profile, queues, mailboxes, and counters |

These objects and the profile must remain valid for the entire lifetime of the driver.

---

## 4. Create a Configuration Profile

Example:

```c
#include "max35103.h"
#include "max35103_stm32_hal.h"
#include "board_max35103_profile.h"

static const Max35103Profile max_profile = {
    .profile_id = 1U,
    .profile_version = 1U,

    .event_mode_cmd = MAX35103_CMD_EVTMG2,

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
```

The example uses:

- a `1 kΩ` reference resistor;
- a `PT100` RTD.

The `BOARD_MAX35103_TOF1...TOF7` values must contain register images validated for the actual:

- transducer;
- acoustic path;
- pipe geometry;
- hardware board.

Do not use an all-zero profile in production firmware.

---

## 5. Initialize the Driver

```c
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

    status = MAX35103_Stm32HalEnableDma(
        &max_hal,
        &max_transport);

    if (status != MAX35103_OK) {
        return status;
    }

    status = MAX35103_Init(
        &max_driver,
        &max_transport);

    if (status != MAX35103_OK) {
        return status;
    }

    status = MAX35103_ResetDevice(&max_driver);

    if (status != MAX35103_OK) {
        return status;
    }

    return MAX35103_Configure(
        &max_driver,
        &max_profile);
}
```

Call the initialization function from `main()`:

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();

    if (MAX35103_BoardInit() != MAX35103_OK) {
        Error_Handler();
    }

    for (;;) {
        /* Application loop */
    }
}
```

Standard initialization sequence:

```text
Initialize HAL/GPIO/SPI/DMA
→ MAX35103_Stm32HalInitTransport()
→ MAX35103_Stm32HalEnableDma()
→ MAX35103_Init()
→ MAX35103_ResetDevice()
→ MAX35103_Configure()
```

After a power-on reset or hardware reset, repeat:

```text
MAX35103_ResetDevice()
→ MAX35103_Configure()
```

---

## 6. Check Communication

Use `MAX35103_Probe()` to check whether the MAX35103 returns a reasonable response:

```c
Max35103Status status =
    MAX35103_Probe(&max_driver);

if (status != MAX35103_OK) {
    /* Check SPI, NSS, power, and reset. */
}
```

SPI does not provide an ACK mechanism like I²C. Therefore, `MAX35103_Probe()` relies on reasonable register readback rather than an absolute chip-ID check.

---

## 7. Direct TOF Measurement

`MAX35103_SelfCheck()` performs one blocking `TOF_DIFF` measurement.

```c
Max35103Status status =
    MAX35103_SelfCheck(&max_driver);

if (status == MAX35103_OK) {
    Max35103RawResult result;

    if (MAX35103_GetResult(
            &max_driver,
            &result) == MAX35103_OK &&
        result.valid) {

        int64_t tof_up_ps = result.tof_up_ps;
        int64_t tof_down_ps = result.tof_down_ps;
        int64_t tof_diff_ps = result.tof_diff_ps;

        /* Process the result here. */
    }
}
```

Important fields:

| Field | Description |
|---|---|
| `tof_up_ps` | UP-direction TOF in picoseconds |
| `tof_down_ps` | DOWN-direction TOF in picoseconds |
| `tof_diff_ps` | TOF difference |
| `valid_cycle_count` | Number of valid cycles |
| `tof_range` | Decoded range |
| `status_flags` | Interrupt flag snapshot |
| `timestamp_us` | Time at which `MAX_INT` was recorded |
| `valid` | Indicates whether the result is valid |

For a direct `TOF_DIFF` command, `valid_cycle_count` may be zero. During Event Timing operation, also check:

```c
result.valid_cycle_count > 0U
```

The pipe must be filled with liquid, large air bubbles should be avoided, and the transducers must be acoustically coupled correctly before evaluating TOF performance.

---

## 8. Direct Temperature Measurement

```c
Max35103TemperatureResult temperature;

Max35103Status status =
    MAX35103_MeasureTemperature(
        &max_driver,
        &temperature);

if (status == MAX35103_OK &&
    temperature.valid &&
    temperature.rtd1_temperature_valid) {

    int32_t temperature_millicelsius =
        temperature.rtd1_temperature_millicelsius;

    uint32_t resistance_milliohm =
        temperature.rtd1_resistance_milliohm;

    /* Process the result here. */
}
```

Check the following fields:

| Field | Description |
|---|---|
| `selected_port_mask` | Temperature ports selected by the profile |
| `valid_port_mask` | Ports with valid timing data |
| `open_circuit_mask` | Ports showing a possible open circuit |
| `short_circuit_mask` | Ports showing a possible short circuit |
| `rtd1_valid` / `rtd2_valid` | Valid RTD/reference ratios |
| `rtd1_temperature_valid` | RTD1 temperature conversion succeeded |
| `rtd2_temperature_valid` | RTD2 temperature conversion succeeded |
| `valid` | The complete result satisfies the validity conditions |

Set either of the following values to zero when only raw T1–T4 timing values are required:

```c
.reference_resistance_milliohm
.rtd_nominal_resistance_milliohm
```

---

## 9. Run Event Timing with DMA

### 9.1 Record `MAX_INT`

```c
static volatile bool max_int_pending;

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == MAX_INT_Pin) {
        max_int_pending = true;
    }
}
```

Do not perform blocking SPI operations inside this callback.

### 9.2 Forward DMA Completion to the Adapter

```c
void HAL_SPI_TxRxCpltCallback(
    SPI_HandleTypeDef *hspi)
{
    MAX35103_Stm32HalOnDmaIrq(
        &max_hal,
        true);
}

void HAL_SPI_ErrorCallback(
    SPI_HandleTypeDef *hspi)
{
    MAX35103_Stm32HalOnDmaIrq(
        &max_hal,
        false);
}
```

### 9.3 Main Loop

```c
static uint64_t Board_MonotonicUs(void)
{
    return (uint64_t)HAL_GetTick() *
           UINT64_C(1000);
}
```

Start Event Timing:

```c
if (MAX35103_StartEventTiming(
        &max_driver) != MAX35103_OK) {
    Error_Handler();
}
```

Superloop:

```c
for (;;) {
    uint64_t now_us = Board_MonotonicUs();

    if (max_int_pending) {
        __disable_irq();
        max_int_pending = false;
        __enable_irq();

        MAX35103_OnInt(
            &max_driver,
            now_us);
    }

    (void)MAX35103_Stm32HalProcessDmaCompletion(
        &max_hal,
        &max_driver);

    (void)MAX35103_Process(
        &max_driver,
        now_us);

    Max35103RawResult tof;

    while (MAX35103_ResultPop(
               &max_driver,
               &tof) == MAX35103_OK) {

        if (tof.valid) {
            /* Process the TOF result. */
        }
    }

    Max35103TemperatureResult temperature;

    while (MAX35103_TemperatureResultPop(
               &max_driver,
               &temperature) == MAX35103_OK) {

        if (temperature.valid) {
            /* Process the temperature result. */
        }
    }

    if (!max_driver.event_timing_active &&
        MAX35103_GetState(&max_driver) ==
            MAX35103_STATE_IDLE) {

        (void)MAX35103_StartEventTiming(
            &max_driver);
    }
}
```

Processing flow:

```text
MAX_INT
→ MAX35103_OnInt()
→ read INT_STATUS
→ create a pending SPI request
→ start DMA
→ HAL SPI callback
→ MAX35103_Stm32HalOnDmaIrq()
→ MAX35103_Stm32HalProcessDmaCompletion()
→ MAX35103_OnSpiDone()
→ decode the result
→ push the result into the FIFO queue
```

---

## 10. FIFO Queues

The driver provides two FIFO queues:

- TOF queue;
- temperature queue.

Default capacity:

```c
MAX35103_RESULT_QUEUE_CAPACITY
```

The default value is eight entries.

Select the overflow policy:

```c
MAX35103_SetQueueOverflowPolicy(
    &max_driver,
    MAX35103_QUEUE_DROP_OLDEST);
```

Available policies:

| Policy | Behavior |
|---|---|
| `MAX35103_QUEUE_DROP_OLDEST` | Discard the oldest sample and retain the newest sample |
| `MAX35103_QUEUE_DROP_NEWEST` | Retain the oldest samples and discard the new sample |

Read the TOF queue:

```c
Max35103RawResult result;

while (MAX35103_ResultPop(
           &max_driver,
           &result) == MAX35103_OK) {
    /* Process the result. */
}
```

Read the temperature queue:

```c
Max35103TemperatureResult result;

while (MAX35103_TemperatureResultPop(
           &max_driver,
           &result) == MAX35103_OK) {
    /* Process the result. */
}
```

Clear all unread results:

```c
MAX35103_ClearResultQueues(&max_driver);
```

The legacy mailbox APIs are retained for backward compatibility:

```c
MAX35103_HasResult()
MAX35103_GetResult()
MAX35103_HasTemperatureResult()
MAX35103_GetTemperatureResult()
```

---

## 11. Commonly Used APIs

### Initialization and Configuration

| API | Function |
|---|---|
| `MAX35103_Init()` | Initializes a driver instance |
| `MAX35103_ResetDevice()` | Resets the device, sends `INIT`, and waits for completion |
| `MAX35103_ValidateProfile()` | Validates a profile without accessing the IC |
| `MAX35103_Configure()` | Writes and read-verifies the profile |
| `MAX35103_Probe()` | Checks the device using register readback |

### Measurement and Control

| API | Function |
|---|---|
| `MAX35103_SelfCheck()` | Performs a blocking `TOF_DIFF` measurement |
| `MAX35103_MeasureTemperature()` | Measures T1–T4 and performs RTD conversion |
| `MAX35103_StartEventTiming()` | Starts the configured EVTMG mode |
| `MAX35103_Halt()` | Stops Event Timing inside the IC |
| `MAX35103_Cancel()` | Cancels the MCU-side read operation |

### State Machine

| API | Function |
|---|---|
| `MAX35103_OnInt()` | Records a `MAX_INT` event |
| `MAX35103_Process()` | Advances the state machine by at most one SPI request |
| `MAX35103_GetPendingSpiRequest()` | Retrieves a request for an external IRQ/DMA owner |
| `MAX35103_OnSpiDone()` | Reports completion using the request token |
| `MAX35103_ExecuteSpi()` | Executes a request through the blocking transport |
| `MAX35103_OnTimeout()` | Forces timeout handling for the current operation |

### Result Access

| API | Function |
|---|---|
| `MAX35103_ResultAvailable()` | Returns the number of TOF results in the queue |
| `MAX35103_ResultPop()` | Pops the oldest TOF result |
| `MAX35103_TemperatureResultAvailable()` | Returns the number of temperature results |
| `MAX35103_TemperatureResultPop()` | Pops the oldest temperature result |
| `MAX35103_ReadResult()` | Reads one blocking TOF snapshot |
| `MAX35103_ReadReg()` | Reads a 16-bit register |
| `MAX35103_WriteReg()` | Writes a 16-bit register |
| `MAX35103_WriteVerifyReg()` | Writes a register and verifies the readback |

---

## 12. Status Codes

| Status | Description |
|---|---|
| `MAX35103_OK` | Operation completed successfully |
| `MAX35103_BUSY` | The driver is busy with another operation |
| `MAX35103_TIMEOUT` | The operation exceeded its deadline |
| `MAX35103_INVALID_ARG` | An argument or callback is invalid |
| `MAX35103_NOT_READY` | The device is not ready |
| `MAX35103_SPI_ERROR` | The SPI transport failed |
| `MAX35103_DEVICE_ERROR` | The device or returned data is invalid |
| `MAX35103_CONFIG_ERROR` | Register readback does not match the profile |
| `MAX35103_NO_RESULT` | No result is available |
| `MAX35103_STALE` | The completion token belongs to an old request |
| `MAX35103_OUT_OF_RANGE` | The RTD resistance is outside the supported range |

After an unexpected power-on reset, the project must reset and configure the device again.

---

## 13. CMake Integration

```cmake
add_subdirectory(
    path/to/max35103_driver/driver
)

target_link_libraries(
    your_application_target
    PRIVATE
        driver_max35103_core
        driver_max35103_stm32_hal
)
```

The `driver_max35103_stm32_hal` target is created only when the `stm32cubemx` target already exists.

---

## 14. Integration Checklist

Before running the driver:

- [ ] SPI mode 1 is configured
- [ ] NSS is idle HIGH
- [ ] Reset is active LOW
- [ ] `MAX_INT` uses a pull-up and falling-edge EXTI
- [ ] `MX_DMA_Init()` runs before `MX_SPI1_Init()`
- [ ] `MAX35103_Stm32HalEnableDma()` has been called when DMA is used
- [ ] The profile contains a valid register image
- [ ] The profile lifetime is longer than the driver lifetime
- [ ] No other module reads `INT_STATUS`
- [ ] No blocking SPI operation is performed inside EXTI
- [ ] The consumer drains the FIFO queues fast enough
- [ ] The device is configured again after every reset
- [ ] The pipe is filled with liquid before evaluating TOF
- [ ] The transducers are acoustically coupled correctly

---

## 15. Troubleshooting

### `MAX35103_NOT_READY`

Check whether:

- `MAX35103_Init()` has been called;
- the device has been reset;
- the profile has been configured;
- the reset callback is valid.

### `MAX35103_CONFIG_ERROR`

Check:

- SPI mode;
- NSS behavior;
- register images;
- reserved bits;
- `DPL`;
- `PL`;
- measurement delay;
- register readback.

### `MAX35103_DEVICE_ERROR` During TOF Measurement

Check whether:

- the pipe is filled with water;
- large air bubbles are present;
- the transducers are acoustically coupled correctly;
- the profile matches the acoustic path;
- `valid_cycle_count` is zero.

A dry pipe may still produce a valid temperature result while the TOF result remains invalid.

### No DMA Result Is Received

Check:

- DMA RX and TX configuration;
- NVIC configuration;
- SPI callbacks;
- `MAX35103_Stm32HalProcessDmaCompletion()`;
- `MAX35103_Process()`;
- shared-bus lock/unlock;
- whether NSS is released after a timeout.

### FIFO Overflow

Check:

- Event Timing frequency;
- consumer processing rate;
- `MAX35103_RESULT_QUEUE_CAPACITY`;
- overflow policy;
- `dropped_*` counters.

---

## 16. Current Limitations

- The driver does not automatically compensate for 4 MHz clock error.
- RTD conversion does not automatically compensate for lead-wire resistance.
- FIFO queues have finite capacity.
- Asynchronous DMA requires a shared-bus lock.
- `MAX35103_Cancel()` does not stop Event Timing inside the IC.
- The driver does not manage storage.
- The driver does not manage telemetry.
- The driver does not calculate flow rate.

---

## 17. References

- [MAX35103 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf)
- [Configuring the MAX35101/MAX35103 for an ultrasonic water meter](https://www.analog.com/en/resources/app-notes/configuring-the-max35101-timetodigital-converter-as-an-ultrasonic-water-meter.html)
