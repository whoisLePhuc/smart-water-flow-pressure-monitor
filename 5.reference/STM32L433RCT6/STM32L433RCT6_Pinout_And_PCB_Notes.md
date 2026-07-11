<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Pinout and PCB Notes

## Package

`STM32L433RCT6` uses the LQFP package option. For `R` pin count this is **LQFP64, 10 mm x 10 mm**.

The standard STM32L433Rx LQFP64 pinout is shown in the datasheet on page 57. Do not use the external SMPS LQFP64 pinout unless the ordering code includes the `P` option.

## Recommended MAX35103/MAX35101 pin mapping

| STM32L433RCT6 pin | Function | MAX35103/MAX35101 pin | Direction | PCB notes |
|---|---|---|---|---|
| PA5 | SPI1_SCK | SCK | MCU -> MAX | Keep short, route with MOSI/MISO as SPI group |
| PA6 | SPI1_MISO | DOUT | MAX -> MCU | Add test pad for bring-up |
| PA7 | SPI1_MOSI | DIN | MCU -> MAX | Add test pad for bring-up |
| PA4 | GPIO CE | CE | MCU -> MAX | Active-low chip select, software-controlled |
| PB0 | GPIO reset | RST | MCU -> MAX | Active-low reset |
| PC13 | EXTI13 | INT | MAX -> MCU | Open-drain active-low, pull-up required |
| PB1 | GPIO input | WDO | MAX -> MCU | Optional, open-drain active-low, pull-up if used |

## Debug UART mapping

Use:

| STM32 pin | Function |
|---|---|
| PA2 | USART2_TX |
| PA3 | USART2_RX |

Avoid:

| STM32 pin | Reason |
|---|---|
| PD5 | Not available on standard STM32L433Rx LQFP64 |
| PD6 | Not available on standard STM32L433Rx LQFP64 |

## Essential pins to reserve

| Pin/group | Use | Recommendation |
|---|---|---|
| PA13 / PA14 | SWDIO / SWCLK | Always reserve for debug/programming |
| NRST | Reset | Add pull-up/cap/protection per ST recommendation |
| PH3-BOOT0 | Boot mode | Add defined pull-down or follow boot design |
| PC14 / PC15 | LSE 32.768 kHz | Reserve for RTC crystal if low-power timing accuracy matters |
| PH0 / PH1 | HSE crystal | Optional; can be left for future if board space allows |
| VDD / VSS | Digital supply | Decouple each VDD pin close to package |
| VDDA/VREF+ and VSSA/VREF- | Analog supply/reference | Tie/filter carefully, even if ADC is only used for diagnostics |
| VBAT | Backup domain | Tie to VDD if no backup battery/supercap; otherwise route backup source |
| VDDUSB | USB supply | Handle according to datasheet; if USB unused, do not leave floating |

## Power and decoupling checklist

- Place 100 nF decoupling capacitors close to each VDD pin.
- Add a bulk capacitor on the 3.3 V rail near the MCU.
- Keep VDDA clean if ADC battery/temperature diagnostics matter.
- Tie VDDA to VDD through a filter if no separate analog rail is used.
- Connect all ground pins to a low-impedance ground plane.
- Keep MAX35103 analog/acoustic traces away from fast digital/SPI/clock lines.

## LSE crystal notes

For water meter products, use LSE if accurate long-term timestamp and periodic wake scheduling are needed.

Reserve:

```text
PC14-OSC32_IN
PC15-OSC32_OUT
```

Keep the 32.768 kHz crystal and load capacitors close to the MCU. Avoid routing noisy signals near these pins.

## HSE crystal notes

HSE is optional. The MCU can run from MSI/HSI16/PLL for most firmware tasks. Add HSE footprint if:

- USB timing margin is important.
- You need a more accurate high-speed clock.
- Future communication peripherals require it.

For the MAX35103/MAX35101 interface, HSE is not strictly required because the metrology timing is handled inside the MAX IC.

## GPIO low-power policy

At reset, GPIOs are generally in analog input state. For product firmware:

- Configure unused GPIOs as analog mode with no pull unless an external circuit needs a defined state.
- Avoid floating inputs.
- Disable GPIO peripheral clocks when not needed.
- Keep MAX35103 `INT` as input with pull-up.
- Put external communication modules into shutdown before entering Stop 2.
- Avoid unnecessary high-speed GPIO output drive.

## Test points

Add test points for:

| Signal | Reason |
|---|---|
| 3.3 V | Power measurement |
| GND | Probe ground |
| NRST | Reset/debug |
| SWDIO/SWCLK | Programming/debug |
| USART2_TX/RX | Console |
| SPI1_SCK/MISO/MOSI | MAX driver validation |
| MAX CE | SPI frame validation |
| MAX INT | Interrupt validation |
| MAX RST | Reset sequencing |
| Current measurement jumper | Low-power validation |

## PCB conclusion

The proposed pinout is valid for `STM32L433RCT6` standard LQFP64 and is suitable for MAX35103/MAX35101 integration. The most important correction from the earlier STM32L476RG/LQFP64 draft is to keep debug UART on **PA2/PA3**, not PD5/PD6.
