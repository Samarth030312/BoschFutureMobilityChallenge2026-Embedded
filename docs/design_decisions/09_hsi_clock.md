# Decision 09 — HSI internal clock (no external crystal)

## What the source confirms

- `RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI` (`main.c:183`).
- HSI is the 16 MHz internal RC oscillator on STM32F4.
- PLL configured for 84 MHz SystemClock from HSI (`main.c:188-191`: PLLM=8, PLLN=84, PLLP=/2).
- No HSE configured in `main.c:SystemClock_Config`.

## Why HSI (not HSE)

A new engineer should understand: the Nucleo-F401RE board has hardware support for an external 8 MHz HSE source (the ST-LINK chip's MCO can be configured to output it). The team chose NOT to use it.

Reasons inferable from the code (NOT documented in source comments):

- **No external crystal soldered to the F401.** The Nucleo's HSE comes via MCO from the ST-LINK chip — requires a specific solder bridge configuration that the team may not have set up.
- **Simpler bring-up.** HSI is on by default at reset; SystemInit (CMSIS) starts it.
- **Adequate accuracy for the application.** HSI is ±1 % at room temperature. UART at 460 800 baud and PWM at 50 Hz both tolerate this.

## Drawbacks visible in the choice

- **HSI temperature drift.** Spec is ±1 % at 25°C; can degrade to ±2 % across the F401's operating range (-40 to +105°C). The competition venue's temperature swing during a race day may exceed the room-temp spec — the HSI clock source.
- **UART baud-rate margin shrinks.** At 460 800 baud the BRR error is already 0.16 % (see `06_baud_460800.md`); HSI drift adds up to ±1 % on top. Total worst case is at the edge of UART tolerance.
- **Tick-rate drift.** FreeRTOS tick (1 kHz from SysTick / TIM1) drifts with HSI. `osDelay(1)` could be 0.99 ms or 1.01 ms — fine at this scale.

## Critical observation

The `system_state_machine` ladder + Jetson safety supervisor stale-window checks are computed in **the Jetson's clock domain** — not the firmware's. So firmware clock drift doesn't directly affect those gates. What it does affect:

- UART baud-rate accuracy → potential framing errors → `HAL_UART_ErrorCallback` self-healing.
- `rmw_uros_sync_session` periodicity (10 s nominal) → drift between sync events; corrected on each sync.
- PWM frequency (50 Hz nominal) → may be 49.5 or 50.5 Hz — well within ESC/servo tolerance.

## Alternatives the choice rules out

- **HSE crystal** (8 MHz from MCO or external crystal). Requires hardware setup (solder bridges, optionally an external crystal).
- **HSE bypass mode** (use an external oscillator IC instead of crystal).
- **PLLI2S** as a clock fallback (F4 has it but it's typically used as a clock source for I²S, not system clock).

## What a future redesign might pick

- **Switch to HSE** with appropriate Nucleo solder-bridge setup. Bench-verify before competition.
- **Add HSI auto-calibration** at runtime using a known-good time reference (e.g., the micro-ROS agent's clock).
- **Lower the SystemClock** to give more BRR margin at the same baud — would reduce CPU bandwidth but improve UART reliability.

## What this decision constrains in current code

- The PLL configuration (`main.c:188-191`) is tied to a 16 MHz input.
- Voltage scale 2 + flash latency 2 are correct for 84 MHz; changing the clock would require re-checking these.
- All peripheral clock domains (APB1 timer = 84 MHz, APB1 PCLK = 42 MHz, APB2 = 84 MHz) flow from the PLL → would shift if the PLL config changes.

The current configuration is a coherent unit: HSI + PLLM=8 + PLLN=84 + PLLP=/2 + voltage scale 2 + flash latency 2 + APB1=/2 + APB2=/1. Touch any one without re-verifying the others is asking for trouble.
