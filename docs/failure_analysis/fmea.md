# FMEA — Failure Analysis

**Generated:** 2026-06-08 | **Phase:** 12

Consolidated FMEA covering STM32 firmware + boundary interfaces. Cross-references the workspace FMEA in `bfmc-ws-backup-jetson/audit_2026_06_08/fmea.md`.

## Severity scale (S, 1-5)

- 1: Cosmetic — no functional impact
- 2: Observability — degraded telemetry; no safety issue
- 3: Degraded operation — vehicle behaves sub-optimally but safely
- 4: Loss of function — vehicle stops or behaves unsafely; recovery possible
- 5: Vehicle damage / personal injury risk

## Likelihood scale (L, 1-5)

- 1: Theoretical
- 2: Occasional under stress
- 3: Possible in normal operation
- 4: Observed during testing
- 5: Always present

## Per-subsystem FMEA

### Perception (camera, etc.) — Jetson side (cross-reference)

See `bfmc-ws-backup-jetson/audit_2026_06_08/fmea.md` Section A. Camera dropout RPN 20 is the headline. STM32 firmware response: deadman engages within 500 ms of `/control/command` silence; PWM to 1500 µs neutral.

### Localization & Fusion — Jetson side (cross-reference)

Workspace audit Section B. STM32 firmware contributes by publishing trusted IMU data with the right covariance contract; failure modes BNO-3, BNO-4 from the BNO085 subsystem feed into this.

### microROS / STM32-Jetson link

| ID | Failure | Cause | Detection | Recovery | S | L | RPN |
|---|---|---|---|---|---|---|---|
| M-1 | USB cable unplugged | Mechanical / operator | XRCE timeouts on Jetson; STM32 publishes go unacknowledged | Jetson supervisor zeros within 250 ms; STM32 deadman engages within 500 ms | 4 | 2 | 8 |
| M-2 | XRCE-DDS agent crashes | Docker issue, OOM | STM32 publishes silently fail; subscribed callbacks stop firing | None automatic | 4 | 1 | 4 |
| M-3 | UART overrun at 460 800 baud | CPU spike, ISR latency | `HAL_UART_ErrorCallback` clears flags + re-arms (it_transport.c:123-138) | Self-heal | 2 | 3 | 6 |
| M-4 | RX ring full → byte drop | Sustained inbound burst | drop-on-full preserves XRCE frame integrity | XRCE retransmits | 2 | 1 | 2 |
| M-5 | Custom heap OOM during publish | XRCE buffer accumulation under disconnect | `rcl_publish` returns error | Silent; subsequent publishes try again | 3 | 1 | 3 |
| M-6 | Domain ID mismatch | Firmware hardcoded 42; Jetson env different | Silent no-discovery | Operator fix | 3 | 1 | 3 |
| M-7 | Custom heap fragmentation | Long-duration mixed allocation patterns | Eventual OOM; same as M-5 | None | 3 | 1 | 3 |

### STM32 firmware (boot + run)

| ID | Failure | Cause | Detection | Recovery | S | L | RPN |
|---|---|---|---|---|---|---|---|
| F-1 | Cortex-M HardFault | Memory access fault, stack overflow, divide-by-zero | `HardFault_Handler` | `NVIC_SystemReset()` → reboot → PWM neutral (`stm32f4xx_it.c:105`) | 3 | 1 | 3 |
| F-2 | MemManage / BusFault / UsageFault | Same class | Respective handler | Same — `NVIC_SystemReset()` | 3 | 1 | 3 |
| F-3 | NMI | Clock fault, brown-out | NMI_Handler | Infinite loop (deliberate, NOT reset) | 4 | 1 | 4 |
| F-4 | **`Error_Handler` invoked**  | Peripheral init failure | `Error_Handler` body: `__disable_irq(); while(1){}` | **NO RECOVERY** — TIM2 keeps last PWM | 4 | 1 | 4 (but **operationally severe**) |
| F-5 | **IWDG hangs**  | Pure software hang without fault | IWDG would reset, but it's disabled | None | 4 | 1 | 4 (mitigated by other paths but **single-failure deadlock unrecoverable**) |
| F-6 | Stack overflow on microROSTask | Recursive allocation, deep call chain | `configCHECK_FOR_STACK_OVERFLOW` NOT set | Silent corruption of next-in-RAM region (likely the heap) | 4 | 1 | 4 |
| F-7 | FreeRTOS heap OOM (vendor heap_4) | Task creation, queue creation under unforeseen pressure | `vApplicationMallocFailedHook` NOT defined | Silent NULL-return | 3 | 1 | 3 |
| F-8 | Custom heap OOM | XRCE buffer accumulation | NULL-return; same as M-5 | None | 3 | 1 | 3 |
| F-9 | **the FPU context-save state silent FPU corruption** if multi-task | Multi-task addition with FPU use | None | Crash or undefined behavior | 5 (if triggered) | 1 (single-task today) | 5 |
| F-10 | `configASSERT` triggered | Internal FreeRTOS sanity check fails | configASSERT body: hang | None | 4 | 1 | 4 |
| F-11 | Clock startup fails | HSI not stable, PLL not locking | `Error_Handler` (F-4 mode) | None | 3 | 1 | 3 |
| F-12 | Flash latency wrong | (CubeMX defaults) | Memory corruption | Reset chain | 3 | 1 | 3 |
| F-13 | HSI drifts at temperature | Environmental | Baud rate drift; UART errors → ErrorCallback recovers | Self-heal until drift exceeds tolerance | 2 | 2 | 4 |

### BNO085 sensor

See `docs/subsystems/bno085/09_failure_modes.md` for the per-mode catalogue. Top items:

| ID | Failure | RPN |
|---|---|---|
| BNO-1 | Silent calibration failure | 8 (should be raised) |
| BNO-3 | EMI quaternion corruption | 12 (mitigated by sanity gate + adaptive R) |
| BNO-4 | EMI accel/gyro corruption | 9 (no sanity gate exists) |
| BNO-10 | I²C bus wedged | 4 (unrecoverable without IWDG) |
| BNO-11 | Vehicle moved during calibration | 6 |

### Control actuation

| ID | Failure | Cause | Detection | Recovery | S | L | RPN |
|---|---|---|---|---|---|---|---|
| C-1 | TIM2 init fails | Peripheral fault | `Error_Handler`  → hang | No recovery (vehicle in pre-PWM state) | 4 | 1 | 4 |
| C-2 | TIM2 PWM_Start fails | Peripheral fault after init | Caller (`freertos.c:209-210`) does not check return value | Silent failure | 3 | 1 | 3 |
| C-3 | CCR write target wrong (typo / bug) | Code regression | None | None | 4 | 1 | 4 |
| C-4 | Deadman never triggers (clock stuck) | TIM1 (HAL tick) fails | None | None — but TIM1 stuck would manifest as `osDelay` not returning, scheduler frozen | 4 | 1 | 4 |
| C-5 | Brake command = 1.0 → full reverse on Quicrun | Quicrun ESC bug | Jetson supervisor caps brake at 0.5 (race_day launch); firmware ESC mapping at brake=1.0 still produces 2000 µs | Defensive | 4 | 1 | 4 |

### Watchdog (IWDG)

| ID | Failure | Cause | Detection | Recovery | S | L | RPN |
|---|---|---|---|---|---|---|---|
| W-1 | **IWDG disabled**  | Deliberate at boot | None | None | 4 | 5 (always present) | **20** |

This is the highest-RPN STM32-firmware finding.

## Top-10 punch list

| Rank | Issue | RPN | Mitigation path |
|---|---|---|---|
| 1 | IWDG disabled (the IWDG-disabled state, W-1) | 20 | Uncomment `IWDG_Init_Direct()` after bench verification |
| 2 | BNO085 EMI quat corruption (BNO-3) | 12 (mitigated) | Already handled — sanity gate + adaptive R |
| 3 | BNO085 silent calibration fail (BNO-1) | 8-12 | Loud fail path (LED pattern, diagnostic) |
| 4 | BNO085 EMI accel/gyro (BNO-4) | 9 | Add sanity gate on accel/gyro |
| 5 | USB unplug (M-1) | 8 | Deadman already handles |
| 6 | UART overrun (M-3) | 6 | Already self-healing |
| 7 | BNO085 wedged bus (BNO-10) | 4 (low likelihood, hi severity) | IWDG fix (#1) + SDA-pulse recovery |
| 8 | `Error_Handler` hang (F-4 / the Error_Handler hang behaviour) | 4 | Replace body with `NVIC_SystemReset()` |
| 9 | Stack overflow (F-6) | 4 | `configCHECK_FOR_STACK_OVERFLOW = 2` + hook → reset |
| 10 | FPU multi-task corruption if added (F-9) | 5 (conditional) | `configENABLE_FPU = 1` |

## Single points of failure

1. **The BNO085 (single sensor).** No backup IMU. Lose the BNO085 → no orientation data → Jetson EKF degrades.
2. **The USB-CDC link (single channel).** Disconnect → vehicle parks via deadman.
3. **The 84 MHz PLL (single clock source).** Fault → NMI → infinite loop (deliberate).
4. **The 3.3V supply.** Brown-out → undefined; could cause PWM glitches.
5. **The firmware itself (no redundancy).** A bug in `freertos.c` → fault → reset; if persistent → no boot.

Defense-in-depth chain (in order of failure):
1. Cortex-M faults → `NVIC_SystemReset()` → boot → PWM neutral.
2. 50-error auto-recovery → BNO085 re-init.
3. 500 ms deadman → PWM neutral.
4. IWDG (currently disabled — the IWDG-disabled state).
5. NMI (deliberate hang for unrecoverable clock fault).

## Mitigation roadmap (NOT MADE — review)

| Priority | Action | Effort | Closes |
|---|---|---|---|
| 1 | Enable IWDG  | 1-line change + bench verify | W-1 |
| 2 | Replace `Error_Handler` body with `NVIC_SystemReset()`  | 1-line change | F-4 |
| 3 | Set `configENABLE_FPU = 1`  | 1-line change | F-9 conditional |
| 4 | Define `configCHECK_FOR_STACK_OVERFLOW = 2` + hook | 5-line change | F-6 |
| 5 | Define `configUSE_MALLOC_FAILED_HOOK = 1` + hook | 5-line change | F-7, F-8 |
| 6 | Add sanity gates on accel/gyro | 10-line change | BNO-4 |
| 7 | Loud fail on calibration silent failure | 20-line change | BNO-1 |
| 8 | Implement I²C SDA-pulse-recovery | ~30 lines | BNO-10 |
| 9 | Telemetry of heap high-water-mark | 20 lines | observability |
| 10 | Split heap size constants  | 5-line change | budget |

These are presented as findings; the spec rules forbid modification without explicit approval. Approval per item requested separately.
