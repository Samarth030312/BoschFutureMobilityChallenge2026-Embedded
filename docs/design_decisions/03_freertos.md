# Decision 03 — FreeRTOS

## What the source confirms

- FreeRTOS 10.3.1 kernel under MIT license (`FreeRTOSConfig.h` header).
- CMSIS-RTOS V2 wrapper used (the `osThreadNew`, `osDelay`, `osKernelStart` API; `freertos.c:87, 145, 151`).
- Heap_4 vendor backend (`Makefile:65`: `Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c`).
- ARM_CM4F port (`Makefile:66`).
- 1 ms tick (`FreeRTOSConfig.h:68`).
- 32 KB heap (`FreeRTOSConfig.h:71`).
- One application task (`microROSTask` in `freertos.c:45-50, 87`).
- Preemption enabled (`FreeRTOSConfig.h:62`).

## Why an RTOS at all

A new engineer should understand: this firmware has **one application task**. So why an RTOS?

- **micro-ROS expects RTOS primitives.** `usleep` is implemented via `osDelay` (`syscalls.c:192-203`). `clock_gettime` uses FreeRTOS tick (`microros_time.c`). Without an RTOS, these helpers would have to be reimplemented against bare-metal SysTick.
- **CubeMX integration.** The team's `.ioc` enabled FreeRTOS; CubeMX-generated code is wired around its primitives.
- **Cooperative `osDelay(1)` cadence.** The main loop yields every 1 ms via `osDelay`. Without a scheduler, this would be a tight CPU-burning wait or a bespoke SysTick countdown.
- **Future-proofing.** If a second task is ever needed (e.g., diagnostics, encoder polling), the infrastructure exists.

## What the current code does NOT use (despite enabling)

- Mutexes (`configUSE_MUTEXES = 1`) — no inter-task contention to mutex against.
- Queues — single task; nothing to queue between.
- Semaphores (counting/binary) — same.
- Event groups, stream buffers, notifications — same.
- Software timers (`configUSE_TIMERS = 1`) — the Timer task runs but no timers are created.

The CMSIS-RTOS feature bloat (`FreeRTOSConfig.h:96-102`) is CubeMX defaults — not load-bearing for this firmware.

## Drawbacks visible in current source

- **`configENABLE_FPU = 0`** . FreeRTOS port does NOT save FPU registers on context switch. Safe with one task; a future second task using floats would silently corrupt FPU state.
- **No stack-overflow detection.** `configCHECK_FOR_STACK_OVERFLOW` is not defined. Stack overflow → silent BSS/heap corruption.
- **No malloc-failed hook.** `configUSE_MALLOC_FAILED_HOOK` is not defined. OOM is silent.
- **`configASSERT` hangs.** Failed assert → `taskDISABLE_INTERRUPTS(); for(;;);` (`FreeRTOSConfig.h:154`). Same risk class as the Error_Handler hang behaviour `Error_Handler`.
- **Dual-heap collision.** `configTOTAL_HEAP_SIZE` is shared between the vendor heap_4 and the custom micro-ROS heap .

## Alternatives the choice rules out

- **Bare-metal (no RTOS).** Would require reimplementing `osDelay`, `usleep`, `clock_gettime`, and the cooperative tick. micro-ROS becomes harder to integrate.
- **Zephyr.** Larger footprint, different build tooling, more complex for a single-task workload.
- **ChibiOS.** Smaller but less common — would require porting away from CubeMX.
- **NuttX.** Linux-like API; overkill at this scale.

For a single-task firmware, FreeRTOS is on the heavy side of what's needed. The justification is integration with micro-ROS (which has FreeRTOS port utilities) and CubeMX (which configures it for you).

## What a future redesign might pick

- **Keep FreeRTOS, fix the gaps** — set `configENABLE_FPU = 1`, add overflow + malloc hooks, replace `configASSERT` body with `NVIC_SystemReset()`. None of these are architectural changes.
- **Consider Zephyr** if the project moves toward broader robotics ecosystem support (Zephyr has CAN, Bluetooth, more drivers).
- **Bare-metal** if the team ever wants to shave RAM aggressively (would save the heap + scheduler overhead).

## What this decision constrains in current code

- The `osDelay(1)` cadence (`freertos.c:436`) is a FreeRTOS construct.
- `osThreadNew` allocates the task stack from FreeRTOS heap (`freertos.c:87`).
- ISR priorities at 5 must respect `configMAX_SYSCALL_INTERRUPT_PRIORITY` (FreeRTOS rule).
- The custom heap fork (`custom_memory_manager.c`) uses `vTaskSuspendAll` — a FreeRTOS API.

Removing FreeRTOS would touch all of these.
