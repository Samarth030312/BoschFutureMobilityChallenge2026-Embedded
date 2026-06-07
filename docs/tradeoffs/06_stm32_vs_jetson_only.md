# Tradeoff 06 — STM32 + Jetson vs Jetson-only

## Chosen path (current source)

**STM32F401RE as a sensor/actuation co-processor + Jetson Orin Nano as the autonomy compute.**

Two devices, two roles. STM32 handles BNO085 + PWM + deadman. Jetson handles perception, fusion, planning.

## Alternative considered (Jetson-only)

Plug the BNO085 directly into the Jetson's I²C bus, drive the ESC/servo from a Jetson GPIO with hardware PWM (Jetson has it), eliminate the STM32 entirely.

## Side-by-side

| Property | STM32 + Jetson (chosen) | Jetson-only |
|---|---|---|
| BNO085 read latency | ~5 ms I²C bus time + ~1 ms FreeRTOS scheduling | Variable (Linux scheduling, USB hub jitter) |
| PWM jitter | TIM2 hardware-generated → < 1 µs jitter | Linux user-space PWM susceptible to scheduling jitter (µs–ms range) |
| Worst-case PWM dropout under Linux load | N/A (STM32 keeps emitting) | Linux scheduling delays could miss PWM updates entirely |
| Deadman implementation | Sub-millisecond response, hardware-timer-based | Linux user-space deadman is jittery; would need RT kernel |
| RTOS guarantee | Yes (FreeRTOS, bounded) | Only with PREEMPT_RT kernel + carefully tuned threads |
| Boot time | Both must boot; Jetson takes ~45 s | Jetson must be up before any actuation possible |
| Safety floor | Cortex-M fault reset + FreeRTOS deadman + boot-time PWM neutral | Linux Oops → kernel panic → reboot or hang; no defined safe state |
| Hardware count | 2 devices | 1 device |
| Power consumption | ~50 mA STM32 + Jetson | Just Jetson |
| Wiring complexity | More (sensor wires to STM32, plus USB to Jetson) | Less (sensor wires direct to Jetson) |
| Failure isolation | STM32 can park vehicle even if Jetson is down | No isolation; Jetson failure = total failure |

## What was gained by choosing STM32 + Jetson

- **Hard real-time floor.** STM32 + FreeRTOS gives deterministic 1 kHz cadence and sub-ms PWM response. Linux on Jetson cannot match this without significant tuning.
- **Safety isolation.** STM32 keeps actuators at neutral if the Jetson dies. Jetson autonomy crashes are not vehicle-crashes.
- **Boot-time safety.** STM32 boots in seconds; PWM is at neutral from ~500 ms post-power. Jetson takes ~45 s — during which the STM32 is the only thing protecting actuators.
- **Hardware fault recovery.** STM32 reset → boot path → PWM neutral. Independent of Jetson state.

## What was sacrificed

- **More hardware.** Two devices, two power requirements, USB cable in between.
- **Firmware to maintain.** STM32 firmware adds a code surface.
- **Some latency.** Round-trip from BNO085 to Jetson goes through STM32 + UART instead of direct Jetson I²C.

## When the team might switch

- **If the Jetson runs a PREEMPT_RT kernel** with verified bounded scheduling — could match the STM32 floor.
- **If a single-device design is cost-critical** (mass production at scale where the STM32 BOM is meaningful).
- **If a future Jetson SOM** has hardware-timer PWM with guaranteed jitter (some do).

## Current cost-benefit reading

STM32 + Jetson is **the correct safety architecture** for a competition vehicle. The hard real-time floor + safety isolation cannot be replicated on Linux without substantial RT work. The two-device cost is paid for in vehicle safety.

Cross-references: `architecture/system_overview.md` §"Defense-in-depth", `failure_analysis/fmea.md`.
