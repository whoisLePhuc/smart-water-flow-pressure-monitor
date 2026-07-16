# STM32 Water Meter — Pinout & Cấu Hình

> **MCU:** STM32L433RCT6 (Cortex-M4, LQFP64)  
> **Schematic:** Ultrasonic_WM_Slave_V5.1  
> **CubeMX:** STM32Cube FW_L4 V1.18.2  
> **System Clock:** 32 MHz (PLL từ MSI 4 MHz)

---

## 1. Power Tree

```
Battery (3.6V 2ER26500)
  └─ P-MOSFET F100 IRML6401 → V_BAT
       └─ U100 MP28301GG-Z (DC/DC) → +3.3V
            ├─ F101 BLM18_221 → +3.3V_MCU
            │    ├─ STM32L433RCT6
            │    ├─ MAX35103 (qua L400 10µH → Vcc_MAXIM)
            │    └─ FRAM + LCD + Button pull-up
            ├─ F102 BLM18_221 → +3.3V_BLE
            │    └─ nRF52810 BLE
            └─ +3.3V_485 (nguồn riêng)
                 └─ ISL32603 RS485 transceiver
```

---

## 2. Pin Map Chi Tiết

### 2.1 Nguồn & Clock

| Pin | Tên | Kết nối | Chức năng | Ghi chú |
|-----|-----|---------|-----------|---------|
| 1 | VBAT | +3.3V_MCU | Backup power | |
| 3 | PC14 | Y200 (32k_out) | LSE 32.768 kHz | C200=15pF |
| 4 | PC15 | Y200 (32k_in) | LSE 32.768 kHz | C201=15pF |
| 5 | PH0 | Y201 (OSC_IN) | HSE 8 MHz | C202=30pF |
| 6 | PH1 | Y201 (OSC_OUT) | HSE 8 MHz | C203=30pF |
| 7 | NRST | R201/C205 | Reset | |
| 60 | PH3/BOOT0 | R200 10k↓ / P200 | Boot mode | BOOT0=0: Flash |

### 2.2 Debug & Programming (SWD)

| Pin | Tên | Kết nối | Chức năng |
|-----|-----|---------|-----------|
| 46 | PA13 (SWDIO) | P200 pin 3 | SWD data |
| 49 | PA14 (SWCLK) | P200 pin 4 | SWD clock |
| — | P200 pin 5 | BOOT0 | Boot mode select |

Header P200: +3.3V_MCU, GND, SWD, SWC, BOOT0

### 2.3 MAX35103 — Ultrasonic TDC (SPI + Control)

| Pin MCU | Tên STM32 | Net Label | Pin MAX35103 | Chức năng | Config |
|---------|-----------|-----------|-------------|-----------|--------|
| 20 | PA4 | **MAX_NSS** | CE (10) | Chip select (SPI CS) | GPIO_Output, default HIGH |
| 21 | PA5 | **MAX_SCK** | SCK (11) | SPI clock | SPI1_SCK, 16 MHz |
| 22 | PA6 | **MAX_MISO** | DOUT (13) | SPI MISO | SPI1_MISO |
| 23 | PA7 | **MAX_MOSI** | DIN (12) | SPI MOSI | SPI1_MOSI |
| 24 | PC4 | **MAX_RST** | RST (14) | Hard reset | GPIO_Output, active LOW |
| 25 | PC5 | **MAX_INT** | INT (9) | Interrupt | GPIO_EXTI (Rising), **ISR: EXTI9_5** |
| 26 | PB0 | **MAX_CMP** | CMP_OUT (8) | Comparator output | GPIO_Input |
| 28 | PB2 | **MAX_WDO** | WDO (15) | Watchdog output | GPIO_Input |

**SPI1 Config:** Master, Full-Duplex, 8-bit, CPOL=0/CPHA=0 (Mode 0), 16 Mbps, NSS Software

**MAX35103 Clock:**
- Y400 = 4 MHz (X1/X2) với C410/C411 = 22pF
- Y401 = 32.768 kHz (32KX0/32KX1) với C412/C413 = 15pF

### 2.4 RS485 / Modbus (LPUART1)

| Pin MCU | Tên STM32 | Net Label | Pin ISL32603 | Chức năng | Config |
|---------|-----------|-----------|-------------|-----------|--------|
| 8 | PC0 | **485_LPUART_RX** | RO (1) | RS485 receive | LPUART1_RX |
| 9 | PC1 | **485_LPUART_TX** | DI (4) | RS485 transmit | LPUART1_TX |
| 27 | PB1 | **RS485_DE** | RE+DE (2,3) | Direction control | GPIO_Output, 1=TX, 0=RX |

**LPUART1 Config:** 9600 baud, 8N1, Asynchronous, DMA Circular RX + Normal TX

**Bus:** R302=120Ω termination, D300 TVS, R300/R304=10kΩ biasing

### 2.5 BLE — nRF52810 (USART3)

| Pin MCU | Tên STM32 | Net Label | Pin nRF52810 | Chức năng | Config |
|---------|-----------|-----------|-------------|-----------|--------|
| 51 | PC10 | **UART3_TX** | P0.08 (10) | BLE UART TX | USART3_TX, 115200 |
| 52 | PC11 | **UART3_RX** | P0.06 (8) | BLE UART RX | USART3_RX, 115200 |
| 53 | PC12 | **MCU_BLE** | P0.15 (18) | BLE control | GPIO_Output |

**USART3 Config:** 115200 baud, 8N1, Asynchronous  
**Series resistors:** R501/R502/R503 = 22Ω  
**BLE Clock:** X500 = 32 MHz, Y500 = 32.768 kHz

### 2.6 FRAM — FM24CL04B (I2C1)

| Pin MCU | Tên STM32 | Net Label | Pin FM24CL04B | Chức năng | Config |
|---------|-----------|-----------|-------------|-----------|--------|
| 58 | PB6 | **FRAM_SCL** | SCL (6) | I2C clock | I2C1_SCL, pull-up 10kΩ |
| 59 | PB7 | **FRAM_SDA** | SDA (5) | I2C data | I2C1_SDA, pull-up 10kΩ |

**I2C1 Config:** 7-bit address, 100 kHz (timing=0x00B07CB4)  
**Device Address:** 0x50 (A1/A2 = GND)  
**WP:** GND (write enabled)  
**Capacity:** 4 Kbit = 512 bytes

### 2.7 LCD — Segment Glass OST26067TWPRP-P

| MCU Pin | Tín hiệu | LCD Pin | Ký hiệu LCD |
|---------|---------|---------|------------|
| PA8 | LCD_COM0 | 4 | COM4 |
| PA9 | LCD_COM1 | 3 | COM3 |
| PA10 | LCD_COM2 | 2 | COM2 |
| PB9 | LCD_COM3 | 1 | COM1 |
| PB3 | LCD_SEG7 | 17 | 7F_G_E_D |
| PB4 | LCD_SEG8 | 18 | 7A_B_C-P3 |
| PB5 | LCD_SEG9 | 19 | 8F_G_E_D |
| PB10 | LCD_SEG10 | 14 | 5A_B_C-P |
| PB11 | LCD_SEG11 | 13 | 5F_G_E_D |
| PB12 | LCD_SEG12 | 12 | 4A_B_C-P1 |
| PB13 | LCD_SEG13 | 11 | 4F_G_E_D |
| PB14 | LCD_SEG14 | 10 | 3A_B_C-S1 |
| PB15 | LCD_SEG15 | 9 | 3F_G_E_D |
| PB8 | LCD_SEG16 | 20 | 8A_B_C-T |
| PA15 | LCD_SEG17 | 15 | 6F_G_E_D |
| PC6 | LCD_SEG24 | 8 | 2A_B_C-S2 |
| PC7 | LCD_SEG25 | 7 | 2F_G_E_D |
| PC8 | LCD_SEG26 | 6 | 1A_B_C-S |
| PC9 | LCD_SEG27 | 5 | 1F_G_E_D |
| PD2 | LCD_SEG43 | 16 | 6A_B_C-P2 |
| PC3 | LCD_VLCD | — | Voltage reference |

**LCD Config:** 1/4 Duty, 1/3 Bias, Internal Voltage Source, Contrast Level 0  
**Tổng cộng:** 4 COM × 20 SEG = 80 segments được nối

### 2.8 Debug UART (USART2)

| Pin MCU | Tên STM32 | Net Label | Kết nối | Config |
|---------|-----------|-----------|--------|--------|
| 16 | PA2 | **UART2_TX_DBG** | J200 (Debug header) | USART2_TX, 115200 |
| 17 | PA3 | **UART2_RX_DBG** | J200 (Debug header) | USART2_RX, 115200 |

### 2.9 GPIO — Inputs & Outputs

| Pin MCU | Tên STM32 | Net Label | Chức năng | Loại | Ghi chú |
|---------|-----------|-----------|----------|------|---------|
| 9 | PA1 | **LED** | Status LED (D200) | Output PP | Qua R202=1kΩ |
| 45 | PA12 | **Button** | User button (SW300) | Input | Pull-up 470kΩ, active LOW |
| 10 | PC2 | **PULSE_OUTPUT** | Opto-isolated pulse (P300) | Output PP | Qua R311=200k → LTV-816 |
| 40 | PC12 | **MCU_BLE** | BLE wake/control | Output PP | → nRF52810 P0.15 |

### 2.10 ADC — Battery Measurement

| Pin MCU | Tên STM32 | Net Label | Đo | Config |
|---------|-----------|-----------|------|--------|
| 14 | PA0 | **ADC_VBAT** | Battery voltage | ADC1_IN5, 12-bit, 640.5 cycles |

**Divider:** R203=10MΩ + R204=10MΩ → tỉ lệ 1/2  
**Công thức:** `Vbat = (ADC_value × 3.3 / 4096) × 2`

---

## 3. System Clock Tree

```
MSI (4 MHz, Range 6)
│
└── PLL
│    ├─ PLLM = 1
│    ├─ PLLN = 16
│    ├─ PLLR = /2
│    └─ SYSCLK = 4 × 16 / 2 = 32 MHz
│
├── HCLK = SYSCLK / 1 = 32 MHz        (AHB)
├── PCLK1 = HCLK / 1 = 32 MHz        (APB1: I2C, USART2/3, LPUART1)
├── PCLK2 = HCLK / 1 = 32 MHz        (APB2: SPI1)
│
├── PLLSAI1 (riêng cho ADC)
│    ├─ PLLSAI1M = 1, PLLSAI1N = 16
│    ├─ PLLSAI1Q = /2 → 32 MHz  (ADC clock)
│    └─ ADC CLOCK = 32 MHz, Async DIV1
│
├── LSI = 32 KHz                     (IWDG)
├── LSE = 32.768 KHz                 (RTC, LCD)
└── HSE = 8 MHz                      (dự phòng)
```

---

## 4. DMA Configuration

| Kênh | Ngoại vi | Mode | Direction | Mục đích |
|------|----------|------|-----------|---------|
| **DMA1_Ch1** | ADC1 | **Circular** | Peripheral → Memory | Đo pin tự động |
| **DMA2_Ch6** | LPUART1_TX | **Normal** | Memory → Peripheral | Gửi Modbus |
| **DMA2_Ch7** | LPUART1_RX | **Circular** | Peripheral → Memory | Nhận Modbus |

---

## 5. NVIC Interrupts

| Interrupt | Priority | Preempt | Sub | Chức năng |
|-----------|----------|---------|-----|-----------|
| EXTI9_5 | 0 | 0 | 0 | **MAX_INT** — MAX35103 measurement ready |
| DMA1_Ch1 | 0 | 0 | 0 | ADC DMA complete |
| DMA2_Ch6 | 0 | 0 | 0 | LPUART1 TX DMA |
| DMA2_Ch7 | 0 | 0 | 0 | LPUART1 RX DMA |
| LPUART1 | 0 | 0 | 0 | LPUART1 error/idle |
| SysTick | 15 | 15 | 0 | HAL 1ms tick |

---

## 6. Watchdog (IWDG)

| Tham số | Giá trị |
|---------|---------|
| Clock | LSI = 32 KHz |
| Prescaler | 32 |
| Reload | 4000 |
| **Timeout** | 4000 × 32 / 32000 ≈ **4 giây** |

---

## 7. Memory Map

| Vùng | Địa chỉ | Kích thước | Sử dụng |
|------|---------|-----------|---------|
| **FLASH** | 0x08000000 | 256 KB | Code + hằng số |
| **RAM1** | 0x20000000 | 48 KB | Data + Stack (1 KB) + Heap (512 B) |
| **RAM2** | 0x10000000 | 16 KB | (không dùng) |

---

## 8. Danh Sách Ngoại Vi Đã Cấu Hình

| STT | Ngoại vi | Giao thức | Tốc độ | Thiết bị | Ghi chú |
|-----|----------|-----------|--------|---------|---------|
| 1 | **ADC1** | 12-bit SAR | 32 MHz | Battery (PA0) | Sampling 640.5 cycles |
| 2 | **I2C1** | I2C | ~100 kHz | FRAM FM24CL04B | 7-bit addr 0x50 |
| 3 | **SPI1** | SPI Mode 0 | 16 Mbps | MAX35103 TDC | CS = PA4 GPIO |
| 4 | **LPUART1** | UART | 9600 | RS485 / Modbus | DMA + DE control |
| 5 | **USART2** | UART | 115200 | Debug console | |
| 6 | **USART3** | UART | 115200 | nRF52810 BLE | |
| 7 | **LCD** | Segment | 32KHz LSE | OST26067TWPRP-P | 4 COM × 20 SEG |
| 8 | **IWDG** | — | 32KHz LSI | — | ~4s timeout |
| 9 | **GPIO** | — | — | MAX_RST/INT/CMP/WDO, RS485_DE, LED, Button, BLE, Pulse | |

---

## 9. Thứ Tự Khởi Tạo Trong main()

```
HAL_Init()
  └─ SystemClock_Config()      → MSI 4MHz → PLL → 32 MHz
       └─ MX_GPIO_Init()       → RS485_DE=0, MAX_RST=1, PULSE=0, ...
            └─ MX_DMA_Init()   → DMA1_Ch1 (ADC), DMA2_Ch6/7 (LPUART)
                 └─ MX_ADC1_Init()    → Ch5 (PA0), 640.5 cycles
                      └─ MX_I2C1_Init()    → FRAM timing 0x00B07CB4
                           └─ MX_LCD_Init()       → 1/4 duty, 1/3 bias
                                └─ MX_SPI1_Init()       → 16 Mbps, Mode 0
                                     └─ MX_USART2_Init()     → 115200 debug
                                          └─ MX_USART3_Init()     → 115200 BLE
                                               └─ MX_LPUART1_Init()   → 9600 Modbus
                                                    └─ MX_IWDG_Init() → ~4s timeout
                                                         └─ HAL_ADC_Start_DMA()
                                                              └─ while(1):
                                                                   ├─ HAL_IWDG_Refresh()
                                                                   └─ Xử lý MAX_INT, Modbus, BLE, LCD
```