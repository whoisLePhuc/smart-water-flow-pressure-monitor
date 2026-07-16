<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 and MAX35103 Integration Notes

## Integration role split

```text
MAX35103/MAX35101
├── Ultrasonic pulse launch
├── Analog front-end / stop detection
├── Time-to-digital conversion
├── TOF_UP / TOF_DN / TOF_DIFF operation
├── Event timing operation
└── INT/WDO signaling

STM32L433RCT6
├── SPI opcode/register access
├── Measurement scheduling policy
├── Interrupt handling
├── Flow computation
├── Calibration and compensation
├── Diagnostics
├── Storage
└── Communication / power management
```

## Recommended electrical mapping

| STM32L433RCT6 | MAX35103 | Notes |
|---|---|---|
| PA5 / SPI1_SCK | SCK | SPI clock |
| PA6 / SPI1_MISO | DOUT | MAX to MCU |
| PA7 / SPI1_MOSI | DIN | MCU to MAX |
| PA4 / GPIO | CE | Active-low chip enable |
| PB0 / GPIO | RST | Active-low reset |
| PC13 / EXTI13 | INT | Active-low open-drain; pull-up required |
| PB1 / GPIO input | WDO | Optional active-low open-drain watchdog output |

## SPI configuration

Recommended initial settings:

```c
SPI mode:          Master
Direction:         2-line full-duplex
Frame size:        8-bit
First bit:         MSB first
NSS:               Software GPIO CE
Clock polarity:    Check MAX35103 datasheet and validate with logic analyzer
Clock phase:       Check MAX35103 datasheet and validate with logic analyzer
Bring-up speed:    1 MHz
Production speed:  4-8 MHz
Timeout:           bounded, e.g. 10-100 ms depending on operation
```

Do not assume STM32 maximum SPI speed should be used. The MAX device and board signal integrity set the practical limit.

## Boot sequence

```text
1. MCU reset.
2. Configure clock at safe speed.
3. Enable GPIO clocks.
4. Set CE high, RST low initially if hardware design allows.
5. Configure SPI pins and SPI1 peripheral.
6. Configure INT as input pull-up + EXTI falling edge.
7. Release MAX RST.
8. Delay according to MAX datasheet.
9. Read a known MAX register/status to confirm SPI.
10. Write MAX configuration registers.
11. Run MAX init/calibration command if required.
12. Enable event timing or trigger manual TOF operations.
```

## Runtime measurement flow

```text
MAX event timing / TOF operation
        │
        ▼
MAX asserts INT low
        │
        ▼
PC13 EXTI wakes STM32 from Stop 2
        │
        ▼
STM32 reads MAX interrupt/status
        │
        ▼
STM32 reads TOF results / average results / temperature if configured
        │
        ▼
STM32 clears/acknowledges interrupt as required by MAX sequence
        │
        ▼
STM32 runs filtering + calibration + flow calculation
        │
        ▼
STM32 stores/transmits as needed
        │
        ▼
Return to Stop 2
```

## Interrupt handling

`INT` from MAX35103/MAX35101 is active-low open-drain. Use a pull-up.

STM32 configuration:

```text
GPIO mode: input
Pull: internal pull-up or external pull-up
EXTI trigger: falling edge for assertion
Priority: high enough to wake and set event flag, but ISR should be short
```

ISR should only set a flag:

```c
void EXTI15_10_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(MAX35103_INT_PIN) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(MAX35103_INT_PIN);
        max35103_int_flag = true;
    }
}
```

Do SPI reads in main context or a metrology task, not inside the EXTI ISR.

## Reset handling

Use MCU-controlled `RST` to recover the MAX IC from a communication or measurement fault.

Recommended fault recovery ladder:

```text
1. Retry SPI transaction.
2. Re-read status.
3. Toggle CE and retry.
4. Soft command reset/re-init if supported.
5. Hardware reset through RST.
6. Reconfigure MAX registers.
7. Mark diagnostic fault if repeated.
```

## Calibration interaction

Use MAX internal calibration features for TDC/device timing, but perform system-level calibration in STM32:

- Zero-flow offset.
- Temperature compensation.
- Pipe geometry correction.
- Transducer delay compensation.
- Flow correction table.
- Outlier rejection and signal quality tracking.

## Suggested measurement data structure

```c
typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    float tof_up_ns;
    float tof_down_ns;
    float tof_diff_ns;
    float temperature_c;
    uint16_t raw_status;
    uint16_t error_flags;
} max35103_measurement_t;
```

## Validation checklist

1. Confirm CE polarity with logic analyzer.
2. Confirm SPI mode with known register read.
3. Confirm INT pulls low after a measurement.
4. Confirm PC13 EXTI wakes MCU from Stop 2.
5. Confirm no false interrupts during idle.
6. Confirm reset sequence recovers MAX communication.
7. Confirm measurement results are stable at zero flow.
8. Confirm temperature readout and compensation path.
9. Confirm low-power current with MAX event timing active.

## Integration conclusion

The STM32L433RCT6 has enough SPI/GPIO/EXTI/low-power capability to control MAX35103/MAX35101 well. Keep the integration interrupt-driven, avoid continuous polling, and make MAX reset/reconfiguration a first-class part of the driver.
