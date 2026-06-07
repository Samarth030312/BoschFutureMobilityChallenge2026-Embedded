# Decision 08 — Custom micro-ROS allocator (fork of heap_4)

## What the source confirms

- `Core/Src/custom_memory_manager.c` (455 LOC) — a fork of FreeRTOS heap_4.
- `Core/Src/microros_allocators.c` (49 LOC) — adapter from `rcl_allocator_t` signatures to the fork's entry points.
- Installed at boot: `rcutils_set_default_allocator(&freeRTOS_allocator)` (`freertos.c:139-147`).
- Reset on installation failure: `osDelay(1000); NVIC_SystemReset()` (`freertos.c:146`).
- 32 KB backing array: `static uint8_t ucHeap[configTOTAL_HEAP_SIZE]` (`custom_memory_manager.c:30`).
- Uses `vTaskSuspendAll`/`xTaskResumeAll` for critical sections (`custom_memory_manager.c:83, 207-208` etc).
- Provides `pvPortMallocMicroROS`, `vPortFreeMicroROS`, `pvPortReallocMicroROS`, `pvPortCallocMicroROS`, `getBlockSize`, `xPortGetFreeHeapSizeMicroROS`, `xPortGetMinimumEverFreeHeapSizeMicroROS`, `vPortInitialiseBlocksMicroROS`.

## Why fork heap_4 instead of using it directly

A new engineer should understand: micro-ROS allocations are routed through `rcl_allocator_t` — a function-pointer interface. The team needed an allocator that:

1. **Isolates micro-ROS allocations** from FreeRTOS internal allocations (task stacks, TCBs, queues).
2. **Provides realloc and calloc** — vendor heap_4 has neither.
3. **Allows independent sizing** — the rcl-served data has different characteristics from FreeRTOS internals.

Forking heap_4 keeps the algorithm (address-ordered free list, first-fit, bidirectional coalescing, suspend-scheduler critical sections) while adding the extra entry points.

## What's in the fork that isn't in vendor heap_4

| Function | Status in vendor heap_4 |
|---|---|
| `pvPortMallocMicroROS` | Renamed equivalent of `pvPortMalloc` |
| `vPortFreeMicroROS` | Renamed `vPortFree` |
| `pvPortReallocMicroROS` | **New** (alloc + memcpy + free) |
| `pvPortCallocMicroROS` | **New** (alloc + byte-loop zero) |
| `getBlockSize` | **New** (peek at block header) |

The vendor's heap_4 only provides malloc + free. micro-ROS needs the full malloc/free/realloc/calloc set.

## Why `vTaskSuspendAll` not a mutex

Two reasons:

1. **No priority inversion.** With one task (this firmware), a mutex is unnecessary — but if a future second task allocated, suspending the scheduler is still safer than a mutex (no priority inversion risk).
2. **Cheaper than a mutex.** `vTaskSuspendAll` increments a counter; no context-switch overhead. A mutex would involve `xSemaphoreTake` and possibly priority inheritance.

The cost: all FreeRTOS tasks pause during the critical section. With one task, that's the calling task itself — no observable harm.

## Drawbacks visible in current source

- **the dual-heap sizing:** the fork uses the same `configTOTAL_HEAP_SIZE = 32000` constant as the vendor heap. Both `ucHeap[]` arrays allocate 32 KB → 64 KB of BSS reserved for heaps out of 96 KB total RAM.
- **No MIT license header.** The file says "based on FreeRTOS heap4.c" but doesn't carry the FreeRTOS MIT notice. Open-source release blocker.
- **Telemetry exists but unused.** `xPortGetMinimumEverFreeHeapSizeMicroROS()` is exported but no code calls it. The team can't see the high-water-mark from the running firmware.
- **`int` counters in adapter** (`microros_allocators.c:5-6`). `usedMemory` and `absoluteUsedMemory` are signed `int`. Could overflow on long-running deployment (`absoluteUsedMemory` is monotonically increasing).
- **Byte-loop zero in calloc** (`custom_memory_manager.c:323-324`). `memset` would be faster.

## Alternatives the choice rules out

- **Use the vendor heap_4 directly for both.** Loses isolation; a micro-ROS OOM could destabilise FreeRTOS internals.
- **TLSF allocator** (Two-Level Segregated Fit). Lower fragmentation under varied allocation patterns. More complex; not Cube-native.
- **Static allocation (`configSUPPORT_STATIC_ALLOCATION`).** micro-ROS supports static-only modes; would eliminate OOM risk but increase code complexity (every entity needs static structs).
- **Per-pool sub-allocators.** More complex; smaller benefit at this scale.

## What a future redesign might pick

- **Introduce `configTOTAL_MICROROS_HEAP_SIZE`** as a distinct constant. Currently both heaps share `configTOTAL_HEAP_SIZE`. Splitting frees RAM if measurement shows the micro-ROS heap can be smaller.
- **Add MIT header.**
- **Promote counters to `size_t`.**
- **Add periodic high-water-mark logging** (a single diagnostic publish from the main loop every 10 s).
- **Replace byte-loop zero with `memset`.**

## What this decision constrains in current code

- The bring-up sequence (`freertos.c:139-147`) depends on this allocator being installable before any rcl entity is created.
- `microros_time.c` and `syscalls.c` (`usleep`) sit alongside the allocator adapter.
- the dual-heap sizing is a direct consequence; the heap collision is the cost of the fork choice + the constant-naming choice.
