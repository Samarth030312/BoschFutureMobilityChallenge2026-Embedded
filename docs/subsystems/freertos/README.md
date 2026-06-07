# FreeRTOS Subsystem

**Generated:** 2026-06-08 | **Phase:** 4

This subsystem inherits content from `docs/architecture/task_graph.md` (timing + cadence) and adds embedded-specific FreeRTOS configuration analysis.

## Configuration summary (`Core/Inc/FreeRTOSConfig.h`)

| Setting | Value | Source line | Implication |
|---|---|---|---|
| `configUSE_PREEMPTION` | 1 | 62 | Preemptive scheduler |
| `configENABLE_FPU` | **0** | 59 | the FPU context-save state: no per-task FPU context save — single-task assumption |
| `configENABLE_MPU` | 0 | 60 | No memory protection |
| `configSUPPORT_STATIC_ALLOCATION` | 1 | 63 | Static allocation supported |
| `configSUPPORT_DYNAMIC_ALLOCATION` | 1 | 64 | Dynamic allocation supported |
| `configUSE_IDLE_HOOK` | 0 | 65 | No idle hook |
| `configUSE_TICK_HOOK` | 0 | 66 | No tick hook |
| `configCPU_CLOCK_HZ` | SystemCoreClock = 84 MHz | 67 | Used for tick rate computation |
| `configTICK_RATE_HZ` | 1000 | 68 | 1 ms tick |
| `configMAX_PRIORITIES` | 56 | 69 | Plenty of room |
| `configMINIMAL_STACK_SIZE` | 128 words = 512 B | 70 | Idle task stack |
| `configTOTAL_HEAP_SIZE` | 32000 | 71 | the dual-heap sizing: shared constant with custom heap |
| `configMAX_TASK_NAME_LEN` | 16 | 72 | Sufficient for "microROSTask" |
| `configUSE_TRACE_FACILITY` | 1 | 73 | Enables trace + stack high-water-mark |
| `configUSE_16_BIT_TICKS` | 0 | 74 | 32-bit ticks |
| `configUSE_MUTEXES` | 1 | 75 | Mutexes available (unused by application) |
| `configQUEUE_REGISTRY_SIZE` | 8 | 76 | (unused) |
| `configUSE_RECURSIVE_MUTEXES` | 1 | 77 | Available (unused) |
| `configUSE_COUNTING_SEMAPHORES` | 1 | 78 | Available (unused) |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 0 | 79 | Conservative |
| `configUSE_TIMERS` | 1 | 91 | Timer task running |
| `configTIMER_TASK_PRIORITY` | 2 | 92 | Higher than Idle |
| `configTIMER_QUEUE_LENGTH` | 10 | 93 | (unused) |
| `configTIMER_TASK_STACK_DEPTH` | 256 words = 1 KiB | 94 | Timer task stack |
| `configPRIO_BITS` | 4 | 131 | Cortex-M priority bits |
| `configLIBRARY_LOWEST_INTERRUPT_PRIORITY` | 15 | 136 | Lowest |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | 5 | 142 | ISRs ≥5 may call *FromISR |
| `configKERNEL_INTERRUPT_PRIORITY` | 15 shifted | 146 | PendSV + SysTick |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 5 shifted | 149 | The mask register loaded by FreeRTOS |
| `configASSERT` | `taskDISABLE_INTERRUPTS(); for(;;);` | 154 | Hard hang on assert (could be `NVIC_SystemReset`) |

## `configASSERT` design choice

```c
#define configASSERT( x ) if ((x) == 0) {taskDISABLE_INTERRUPTS(); for( ;; );}
```
(`FreeRTOSConfig.h:154`)

Hangs the firmware. Same problem class as `Error_Handler` . A `NVIC_SystemReset()` would let the firmware boot back to PWM neutral.

**Recommendation:** consider changing to `NVIC_SystemReset()` for symmetry with HardFault philosophy.

## What's missing (configuration gaps)

| Setting | Value | Should be? |
|---|---|---|
| `configCHECK_FOR_STACK_OVERFLOW` | NOT DEFINED (= 0) | 2 (full method) |
| `configUSE_MALLOC_FAILED_HOOK` | NOT DEFINED (= 0) | 1 |
| `vApplicationStackOverflowHook` | not defined | should be defined → reset |
| `vApplicationMallocFailedHook` | not defined | should be defined → reset |
| `configRECORD_STACK_HIGH_ADDRESS` | NOT DEFINED | 1 (useful for debugger) |
| `configGENERATE_RUN_TIME_STATS` | NOT DEFINED | optional |

## Tasks active

| Task | Priority | Stack | Created at |
|---|---|---|---|
| `microROSTask` | 24 (Normal) | 12 KiB | `freertos.c:87` |
| Timer task | 2 | 1 KiB | kernel-created |
| Idle task | 0 | 0.5 KiB | kernel-created |

See `docs/architecture/task_graph.md` for the full per-task analysis.

## What this firmware does NOT use

- Mutexes (no inter-task contention)
- Queues (single task)
- Semaphores (single task)
- Event groups
- Stream buffers (custom UART ring instead)
- Notifications
- Static allocation (despite `configSUPPORT_STATIC_ALLOCATION = 1`, the application uses `osThreadNew` which is dynamic)

## Risks

- **the FPU context-save state** — FPU context not saved on switch. Single task → no observable harm today.
- **No stack overflow check** — silent corruption on overflow.
- **No malloc-failed hook** — silent OOM.
- **`configASSERT` hangs** — debugger-friendly but operationally unsafe.

## Recommendations (NOT MADE — review-only)

1. Set `configCHECK_FOR_STACK_OVERFLOW = 2` and define `vApplicationStackOverflowHook` → `NVIC_SystemReset`.
2. Set `configUSE_MALLOC_FAILED_HOOK = 1` and define `vApplicationMallocFailedHook` → `NVIC_SystemReset`.
3. Set `configENABLE_FPU = 1` .
4. Change `configASSERT` body to `NVIC_SystemReset()` (or print first, then reset).
5. Periodically log `uxTaskGetStackHighWaterMark(microROSTaskHandle)` for stack-margin telemetry.
