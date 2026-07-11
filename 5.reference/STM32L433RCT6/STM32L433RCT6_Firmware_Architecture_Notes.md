<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Firmware Architecture Notes

## Architecture goal

The firmware should be modular enough for maintainability but small enough for 256 KB Flash and 64 KB SRAM.

Recommended structure:

```text
firmware/
├── bsp/
│   ├── board_config.h
│   ├── gpio.c
│   ├── clock.c
│   └── low_power.c
├── drivers/
│   ├── max35103/
│   ├── storage/
│   ├── uart_debug/
│   └── sensors/
├── services/
│   ├── measurement_service.c
│   ├── calibration_service.c
│   ├── diagnostics_service.c
│   ├── storage_service.c
│   └── comm_service.c
├── metrology/
│   ├── flow_calc.c
│   ├── temperature_comp.c
│   ├── filter.c
│   └── correction_table.c
└── app/
    └── app_main.c
```

## Main data flow

```text
MAX35103/MAX35101 interrupt
        │
        ▼
max35103_driver_read_results()
        │
        ▼
measurement_service_validate()
        │
        ▼
filter_outlier_reject()
        │
        ▼
calibration_apply_offsets()
        │
        ▼
flow_calc_compute()
        │
        ▼
diagnostics_update()
        │
        ├── storage_service_commit_if_needed()
        └── comm_service_publish_if_needed()
```

## Recommended module contracts

### `drivers/max35103`

Responsibilities:

- SPI opcode read/write.
- CE timing and reset sequence.
- Interrupt status read/clear.
- TOF result read.
- Temperature result read if used.
- Register configuration load.
- Low-level error codes.

Should not:

- Compute flow rate.
- Own calibration policy.
- Own communication policy.

### `measurement_service`

Responsibilities:

- Start/stop measurement mode.
- Handle measurement-ready event.
- Convert raw driver output into normalized measurement samples.
- Validate sample status and timestamps.

### `metrology/filter`

Responsibilities:

- Range check.
- Median/moving-average/IIR filtering.
- Sigma clipping/outlier rejection.
- Signal quality metrics.

### `calibration_service`

Responsibilities:

- Zero-flow offset management.
- Temperature coefficient management.
- Flow correction table lookup/interpolation.
- Calibration validity checking.
- Persisting calibration data through storage service.

### `diagnostics_service`

Responsibilities:

- Error counters.
- TOF timeout count.
- Invalid sample count.
- Power/brownout state.
- Watchdog breadcrumbs.
- Sensor fault state.

### `power_manager`

Responsibilities:

- Enter/exit Stop 2.
- Disable unused peripheral clocks.
- Coordinate communication module sleep.
- Manage wake reason.

## Superloop design

A superloop is enough for the first version:

```c
while (1) {
    if (max_int_flag) {
        max_int_flag = false;
        measurement_service_process_ready();
    }

    if (rtc_flag) {
        rtc_flag = false;
        app_process_periodic_tasks();
    }

    if (comm_flag) {
        comm_service_process();
    }

    power_manager_enter_stop2_if_idle();
}
```

## RTOS design

Use FreeRTOS only if communication/storage complexity requires it.

Minimal RTOS tasks:

```text
metrology_task      high priority, event-driven
comm_task           medium priority
storage_task        low priority
diagnostics_task    low priority/periodic
```

Avoid many small tasks because RAM is limited.

## Error handling strategy

Use explicit error codes:

```c
typedef enum {
    METER_OK = 0,
    METER_ERR_MAX_SPI,
    METER_ERR_MAX_TIMEOUT,
    METER_ERR_TOF_INVALID,
    METER_ERR_CALIB_INVALID,
    METER_ERR_STORAGE_CRC,
    METER_ERR_LOW_BATTERY,
} meter_status_t;
```

Keep counters for each error class and expose them in debug/diagnostic output.

## Calibration strategy

Start with simple, robust algorithms:

1. Fixed factory configuration.
2. Zero-flow offset correction.
3. Temperature compensation.
4. Median + IIR filtering.
5. Small flow correction table.
6. Runtime diagnostics and adaptive retuning only after stable baseline.

## Power-aware design rule

Every service should be bounded and fast. Avoid long blocking loops.

Bad:

```text
while (!measurement_ready) poll SPI forever
```

Good:

```text
sleep until INT -> read once -> process -> sleep
```

## Firmware conclusion

A compact event-driven firmware architecture is the best fit for STM32L433RCT6. Keep the MAX driver low-level, keep metrology algorithms separate, and use external storage for data that does not fit comfortably in internal Flash/RAM.
