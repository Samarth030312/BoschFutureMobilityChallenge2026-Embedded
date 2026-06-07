# Decision 13 — Fault handlers call `NVIC_SystemReset()` (not infinite loop)

## What the source confirms

`stm32f4xx_it.c`:

- `HardFault_Handler` → `NVIC_SystemReset()` (line 105).
- `MemManage_Handler` → `NVIC_SystemReset()` (line 120).
- `BusFault_Handler` → `NVIC_SystemReset()` (line 135).
- `UsageFault_Handler` → `NVIC_SystemReset()` (line 150).
- `NMI_Handler` → infinite loop (line 89-91).
- `DebugMon_Handler` → empty (debugger-only).

## Why reset on hard fault (and not hang)

The source's own comment (`stm32f4xx_it.c:101-104`):

> "Trapping forever here would freeze the firmware while TIM2 keeps outputting the last PWM duty — i.e. ESC stays at whatever throttle was commanded at the moment of the fault. NVIC_SystemReset() clears the chip and brings up boot, where TIM2 is forced back to neutral."

This is the **last line of defense** below the FreeRTOS deadman:
- Cortex-M fault → reset → boot path runs → TIM2 init → CCR1=CCR2=1500 µs (boot-time neutral, `freertos.c:213-214`).

Reset is the only way to **simultaneously** stop the runaway code AND restore actuator neutral.

## Why NMI hangs (and not resets)

NMI on Cortex-M signals catastrophic events:
- Clock failure (HSI failure detection if enabled).
- Brown-out (if NMI is used for it).

Resetting wouldn't help: the underlying problem (clock dead, power below spec) persists across reset. Trapping forever lets a debugger inspect the cause. Manual power-cycle / NRST is the only recovery anyway.

## Why DebugMon is empty

`DebugMon` only triggers when a debugger is attached and monitor-mode debugging is enabled. The team's code doesn't use this; the empty body is benign.

## What about `Error_Handler` and `configASSERT`?

These DO NOT follow the fault-handler philosophy :

- `Error_Handler` (`main.c:241-249`): `__disable_irq(); while(1){}` — hangs.
- `configASSERT` body (`FreeRTOSConfig.h:154`): `taskDISABLE_INTERRUPTS(); for(;;);` — hangs.

Both have the **same problem the fault handlers were designed to avoid**: TIM2 keeps last PWM. The inconsistency is documented in `SECURITY.md` under "Known safety gaps".

## Drawbacks visible in current source

- **Reset loses runtime state** — heap contents, IMU samples, last command. Boot starts fresh. Calibration re-runs (2 s blocking window). Sound for safety, costly for diagnostic.
- **No reset reason logging.** The chip's RCC reset-source flags (PORRSTF, SFTRSTF, IWDGRSTF, etc.) are not read at boot. A post-mortem of "why did we reset?" requires guessing.
- **No fault information saved.** A real embedded post-mortem would capture the faulting PC, SP, stack frame, and CFSR/HFSR registers in a persistent buffer. None of that exists.

## Alternatives the choice rules out

- **Trap forever.** Documented as the wrong choice for this firmware.
- **Disable interrupts + spin** (`Error_Handler` style). Same issue.
- **Persistent error log** (RAM-backed across reset via BKPSRAM — F401 doesn't have it; via watchdog-protected SRAM region). More complex.

## What a future redesign might pick

- **Read RCC reset flags at boot** (`__HAL_RCC_GET_FLAG`). Log the reason via a one-shot diagnostic publish.
- **Save CFSR/HFSR/MMFAR/BFAR + faulting PC** to a persistent BSS region; publish post-boot.
- **Make `Error_Handler` and `configASSERT` reset** (the Error_Handler hang behaviour fix). Match the fault-handler philosophy.

## What this decision constrains in current code

- The boot-time PWM neutral (`freertos.c:213-214`) is load-bearing as the post-reset safety state. Without it, reset wouldn't restore safety.
- The firmware is **expected** to occasionally reset on fault and come back up. No code anticipates "long uptime without reset" requirements.
- The Cortex-M fault philosophy + the FreeRTOS deadman + the IWDG (currently disabled — the IWDG-disabled state) form a three-layer hang/fault recovery story. Two of three are active.
