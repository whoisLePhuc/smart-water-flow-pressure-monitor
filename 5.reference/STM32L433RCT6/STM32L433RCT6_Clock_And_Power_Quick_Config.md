<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Clock and Power Quick Config

This is a quick implementation-oriented checklist.

## Bring-up

```text
SYSCLK: MSI/HSI16
SPI1: 1 MHz
USART2: 115200 baud on PA2/PA3
Low-power: disabled until SPI + INT are stable
```

## Prototype normal mode

```text
SYSCLK: PLL to 80 MHz when processing
RTC: LSE 32.768 kHz
SPI1: 4 MHz
Sleep: Stop 2 after each sample
Wake: PC13 EXTI from MAX INT
```

## Product mode

```text
Idle: Stop 2
Wake sources: MAX INT, RTC, optional LPUART1
SPI: enable only during readout
External comm: power gated or sleep by default
Logging: external FRAM/EEPROM
```

## Avoid

- Polling MAX status continuously.
- Keeping UART/communication module always on.
- Using internal flash for high-frequency logs.
- Assuming datasheet current equals board current.
