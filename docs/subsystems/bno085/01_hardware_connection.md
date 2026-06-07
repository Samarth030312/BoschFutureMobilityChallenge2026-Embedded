# BNO085 — 01: Hardware Connection

## Pin mapping

| BNO085 pin | STM32 pin | Function | Mode |
|---|---|---|---|
| VDD / VDDIO | (3.3 V) | Power | — |
| GND | GND | — | — |
| SCL | PB8 | I²C clock | Alt-fn 4, open-drain, very-high speed, no pull |
| SDA | PB9 | I²C data | Alt-fn 4, open-drain, very-high speed, no pull |
| INTN | PB4 | Active-low data-ready interrupt | Input, falling-edge EXTI, internal pull-up |
| BOOTN | (3.3 V) | Bootloader select (HIGH = normal app boot) | hardware-tied HIGH |
| nRESET | (3.3 V via RC?) | Hardware reset (NOT PROVEN whether GPIO-driven) | NOT PROVEN |
| PS0/PS1 | (interface select; defaults for I²C addr 0x4A) | configured externally on breakout | hardware-tied |

The team's source confirms only PB8, PB9, and PB4 are driven from STM32. The other BNO085 pins (BOOTN, nRESET, PS0/PS1) are NOT actively driven — they're hardware-defaulted on the breakout PCB.

## I²C addressing

```c
#define BNO085_I2C_ADDR (0x4A << 1)  // = 0x94
```
(`bno085.h:9`)

7-bit address `0x4A` (selected by ADR pin tied LOW). Shifted left by 1 to form the HAL 7-bit transmit address (HAL expects the address pre-shifted in the read/write direction bit position).

If a future hardware revision ties ADR HIGH, the address becomes `0x4B << 1 = 0x96`. Single-line change.

## I²C bus configuration

| Parameter | Value | Source |
|---|---|---|
| Peripheral | I2C1 | `i2c.c:27, 40` |
| Clock speed | 400 000 Hz (Fast-mode) | `i2c.c:41` |
| Duty cycle | I2C_DUTYCYCLE_2 (16:9 high:low ratio for 400 kHz) | `i2c.c:42` |
| Addressing | 7-bit | `i2c.c:44` |
| Dual addressing | disabled | `i2c.c:45` |
| Own address 1 | 0 (master mode) | `i2c.c:43` |
| General call | disabled | `i2c.c:47` |
| No-stretch | DISABLE (clock stretching is ENABLED) | `i2c.c:48` |

The BNO085 may clock-stretch during SHTP cargo preparation; the team's choice to leave stretch enabled is correct.

## Why 400 kHz (not 100 kHz, not 1 MHz)

- 100 kHz Standard-mode would cap throughput at ~10 KB/s effective; the team's 3 KB/s average (header + cargo at 100 Hz) leaves slack but borderline if all reports activate.
- 400 kHz Fast-mode is the BNO085 max supported speed per datasheet.
- 1 MHz Fast-mode-plus is not supported by the BNO085.

## INTN line behavior

The BNO085 INTN is **open-drain active-LOW**:

- At rest (no packet pending): INTN floats HIGH (released by the BNO085).
- When a packet is ready: BNO085 pulls INTN LOW.
- After the host reads the packet (and the BNO085's internal FIFO drains): BNO085 releases INTN; it returns HIGH.

**Critical race:** if a second packet becomes ready while the host is reading the first, INTN stays LOW continuously. The falling-edge EXTI captures NO new edge. A flag-only gate (without pin-LOW fallback) would stall.

The team's fix: combine `bno085_int_flag` (set by EXTI ISR on falling edge) with a direct `HAL_GPIO_ReadPin(PB4) == LOW` check. See `08_error_handling.md` for the full pattern.

## Internal pull-up on PB4

```c
GPIO_InitStruct.Pin   = GPIO_PIN_4;
GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;
GPIO_InitStruct.Pull  = GPIO_PULLUP;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
```
(`gpio.c:64-67`)

STM32 internal pull-up provides the HIGH bias. Without it, the open-drain INTN line would float when released — EXTI would see spurious noise.

**Note:** the BNO085 datasheet recommends external pull-up (typically 4.7-10 kΩ) for high reliability. The team relies on the STM32 internal pull-up (~40-50 kΩ typical), which is enough for short cable runs but may need an external pull-up for longer wiring.

## Power

The BNO085 module typically has its own 1.8 V LDO from 3.3 V input. Verify with the specific breakout used. Typical current ~14 mA in run mode.

## What is NOT connected

- **nRESET:** the STM32 cannot hardware-reset the BNO085. A stuck BNO085 can only be recovered by power cycling the entire vehicle. The 50-error auto-recovery (`freertos.c:315-326`) re-initializes the BNO085 driver state but cannot reset the chip.
- **PS0/PS1:** interface select pins are hardware-strapped on the breakout for I²C mode. No runtime switching.
- **CLKSEL0:** clock select pin; defaults to internal oscillator.

## Reference

- BNO085 datasheet (Bosch): SHTP and SH-2 reference manuals.
- CEVA SH-2 Reference Manual: report IDs and formats.
- The team's source `bno085.h` and `bno085.c` are the canonical documentation of the implemented subset.
