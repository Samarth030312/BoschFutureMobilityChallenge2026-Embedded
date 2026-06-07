# Task Graph

**Generated:** 2026-06-08 | **Phase:** 1 + 4

## Tasks active in this firmware

| Task | Created at | Priority | Stack | Body |
|---|---|---|---|---|
| `microROSTask` | `freertos.c:87` (`osThreadNew`) | `osPriorityNormal` (24, mid-range of 0-56) | 3000 × 4 = **12 KiB** | `StartDefaultTask` (`freertos.c:98-438`) |
| **FreeRTOS Timer task** (if `configUSE_TIMERS=1`) | created by kernel | `configTIMER_TASK_PRIORITY = 2` | `configTIMER_TASK_STACK_DEPTH = 256` words = **1 KiB** | `prvTimerTask` (FreeRTOS internal) |
| `IDLE` task | created by kernel | 0 (idle) | `configMINIMAL_STACK_SIZE = 128` words = **0.5 KiB** | `prvIdleTask` (FreeRTOS internal) |

Three tasks total. The application has exactly one user task.

## Why one user task

See `system_overview.md` §"Single-task model — why it works" and Chapter 16 of the prior `bfmc-ws-backup-jetson` audit. The decision summarised:

- Only one sensor (BNO085) needs polling.
- Only one publish topic (`/microros/imu_raw`) and one subscribe topic (`/control/command`).
- One actuator stripe (TIM2 CH1 + CH2).
- The 1 kHz `osDelay(1)` cadence covers everything.

A two-task design (one for IMU read + parse, one for ROS + actuation) was deliberately not adopted. Trade-offs documented in `docs/tradeoffs/05_single_vs_multi_task.md`.

## Task graph diagram

```mermaid
flowchart LR
    K[FreeRTOS kernel]
    K --> IDLE[Idle task<br/>priority 0]
    K --> TMR[Timer task<br/>priority 2]
    K --> URT[microROSTask<br/>priority 24]

    subgraph "microROSTask body"
        direction TB
        BOOT[Boot:<br/>BNO085 + transport + allocator + entities + calibration]
        BOOT --> LOOP["Infinite loop (1 kHz cadence)"]
        LOOP --> SP[rclc_executor_spin_some]
        SP --> SY[sync session every 10 s]
        SY --> EXT[Read INTN gate<br/>+ BNO085_GetData]
        EXT --> AUR[Auto-recovery on 50 errors]
        AUR --> PUB[data_valid → publish<br/>(quat normalise + Hamilton baseline)]
        PUB --> LED[10 Hz LED toggle]
        LED --> ACT[Deadman + TIM2 CCR write]
        ACT --> WT[osDelay(1)]
        WT --> SP
    end

    URT --> BOOT
    IDLE -.->|"runs when no task ready"| TMR
    TMR -.->|"timer callbacks"| URT
```

## Task communication

### Inputs to `microROSTask`

| Source | Mechanism | Where |
|---|---|---|
| EXTI4 ISR | `volatile bool bno085_int_flag` | polled in main loop |
| USART2 RX ISR | 2 KiB SPSC ring | polled by `cubemx_transport_read` (called by rcl/rclc layer) |
| HAL tick (TIM1) | `uwTick` 32-bit volatile | read by `osDelay`, `HAL_GetTick` |
| Jetson `/control/command` | XRCE-DDS over UART → rcl → executor → `control_cmd_callback` | invoked from `rclc_executor_spin_some` |

### Outputs from `microROSTask`

| Destination | Mechanism | Where |
|---|---|---|
| Jetson `/microros/imu_raw` | `rcl_publish` → XRCE serialize → `cubemx_transport_write` → `HAL_UART_Transmit` (blocking) | every loop iteration when `data_valid` |
| Steering servo | `TIM2->CCR1` direct register write | every loop iteration |
| Quicrun ESC | `TIM2->CCR2` direct register write | every loop iteration |
| PA5 LED | `HAL_GPIO_TogglePin` every 50 iterations (= 10 Hz) | every 50 loop iterations |
| (`IWDG->KR`) | `IWDG_Refresh_Direct` is exported but NOT called from this loop in the version read this pass | NOT CALLED |

### Inter-task (kernel)

No queues. No semaphores. No mutexes. No event groups. No stream buffers. No notifications.

The CMSIS-RTOS2 wrapper enables them all (per `FreeRTOSConfig.h:96-102`) but the application doesn't use them — because there's nothing to synchronise.

## Timing & cadence

### `microROSTask` per-iteration breakdown (steady state)

| Block | Typical wall-clock | Notes |
|---|---|---|
| `rclc_executor_spin_some(executor, 0)` | <100 µs when no callback; up to 5 ms when `control_cmd_callback` fires | callback writes `target_cmd` |
| `rmw_uros_sync_session(10)` | only every 1000 ticks (~10 s) → ~10 ms once per 10 s | clock discipline |
| INTN gate + `BNO085_GetData` | 0 (gate false) → ~5 ms (I²C read 30 bytes at 400 kHz) | flow-controlled by sensor rate |
| Quaternion normalise + Hamilton + IMU msg build | ~100 µs (FPU hard) | every publish |
| `rcl_publish` → XRCE serialize → blocking `HAL_UART_Transmit` | ~1-2 ms (80-120 byte XRCE message at 460800) | every publish |
| LED toggle | <1 µs | every 50 iterations |
| Deadman + CCR write | <1 µs | every iteration |
| `osDelay(1)` | 1 ms (FreeRTOS tick-quantised) | every iteration |

### Cadence analysis

- The BNO085 emits batched packets at ~100 Hz (10 ms intervals).
- The task's `osDelay(1)` polls every 1 ms.
- So on most iterations the INTN gate is false; the task quickly moves through executor → LED → actuation → delay.
- Every ~10th iteration, a BNO085 packet is ready; the task reads (~5 ms), publishes (~1-2 ms), and delays.

Net steady-state CPU utilisation: ~10-15 % (dominated by the 10 % of iterations doing the BNO085 + publish work).

### Worst-case execution time (per loop iteration)

| Scenario | WCET |
|---|---|
| Happy path (no BNO085 packet, no command) | ~150 µs |
| BNO085 packet + publish | ~7-8 ms |
| BNO085 read with I²C timeout (100 ms per receive × 2 = 200 ms) | **~200 ms** |
| `rmw_uros_sync_session` execution | ~10 ms |
| Single-burst worst case (everything together: sync + I²C timeout + publish queue full) | ~220 ms |

The 220 ms worst case is the upper bound the **firmware deadman (500 ms)** is designed to tolerate. Safe margin.

## CPU utilisation estimate

| Block | Time per 1 s | Notes |
|---|---|---|
| `osDelay(1)` sleeps | ~850 ms | task idle, idle task runs |
| BNO085 read + publish | ~70-100 ms | 100 events × ~1 ms publish + occasional 5 ms I²C |
| Executor spin (no callback) | ~10 ms | trivial polls |
| Sync session (rare) | ~1 ms amortised | every 10 s |
| LED + actuation | ~1 ms | trivial |
| Total CPU usage | **~10-12 %** | conservative estimate |

This is NOT PROVEN with real measurement (no `uxTaskGetRunTimeStats` capture in repo). The estimate is order-of-magnitude.

## Stack margin estimate

12 KiB allocated for `microROSTask`. Stack consumption is dominated by:

- ~512 bytes baseline FreeRTOS + interrupt-frame.
- `BNO085_GetData` call chain: ~256 bytes.
- `BNO085_PerformStartupCalibration`: includes a 2 s loop with ~10-20 floats on stack — ~256 bytes.
- `rclc_executor_spin_some` → CDR deserialisation: NOT PROVEN exact depth. Typical micro-ROS executor stack frames are 1-2 KiB.
- `rcl_publish` → CDR serialisation: same magnitude.

Estimated worst-case: ~4-5 KiB. Margin: ~7-8 KiB. **Reasonable.**

**Verification recommendation:** Add a one-shot startup log line that calls `uxTaskGetStackHighWaterMark(microROSTaskHandle)` and prints the remaining word count. Currently NOT DONE.

## Priority inversion analysis

With one task, classical priority inversion is impossible. The task is always the highest-priority Ready task.

The Timer task at priority 2 and the Idle task at priority 0 only run when `microROSTask` is in `osDelay`. They cannot block resources the `microROSTask` needs.

If a future maintainer adds a second task:

- A higher-priority task waiting on a shared resource (e.g., I²C bus) held by `microROSTask` would block until the lower task released — classical priority inversion.
- FreeRTOS mutexes with priority inheritance (`configUSE_MUTEXES = 1` per `FreeRTOSConfig.h:75`) would mitigate.
- The team's custom heap uses `vTaskSuspendAll` instead of mutex — which **disables** priority inheritance during the heap critical section. Acceptable today (one task); could become a problem with two tasks.

## Deadlock analysis

No mutexes or semaphores in use → no classical deadlock possible. The `vTaskSuspendAll` in the custom heap is suspend-counted and matched.

## Starvation analysis

Idle task: runs in `osDelay(1)` windows. Reaches ~85 % CPU utilisation (Idle is the steady-state CPU consumer when the task is sleeping). No starvation risk.

Timer task: at priority 2, preemptible by `microROSTask` at priority 24. With `configUSE_TIMERS = 1` and `configTIMER_QUEUE_LENGTH = 10`, the Timer task could theoretically be starved if `microROSTask` never sleeps — but `microROSTask` sleeps in every loop iteration (`osDelay(1)`), so the Timer task gets ~1 ms windows constantly. **No starvation.**

`microROSTask`: only Ready task at its priority. Never starved.

## What this firmware does NOT have (intentional gaps)

| Mechanism | Not used | Reason |
|---|---|---|
| Task notifications | No producer/consumer split | One task |
| Queues | Same | One task |
| Semaphores | Same | One task |
| Stream buffers | UART RX uses custom ring instead | Custom transport for XRCE-DDS |
| Mutexes | Custom heap uses `vTaskSuspendAll` | No multi-task contention |
| Event groups | No multi-signal aggregation | Single linear flow |

The breadth of these in `FreeRTOSConfig.h:96-102` reflects the CubeMX default; the application doesn't need them.

## Notes for the next maintainer

If you add a second task that touches the FPU, **immediately set `configENABLE_FPU = 1`** in `FreeRTOSConfig.h:59` . Otherwise FPU register corruption across context switches is silent and catastrophic.

If you add a second task that allocates from the FreeRTOS heap (not the custom micro-ROS heap), reconsider the heap size constant . Currently both heaps reserve 32 KB; total 64 KB.

If you add a queue or mutex for inter-task communication, audit the heap usage — queues/mutexes come out of the FreeRTOS heap.
