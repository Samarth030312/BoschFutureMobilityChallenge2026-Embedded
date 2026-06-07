# Memory Subsystem

**Generated:** 2026-06-08 | **Phase:** 6

Cross-reference: `docs/architecture/memory_graph.md` (runtime view), `docs/architecture/memory_layout.md` (linker view).

## Three heaps

| Heap | Backing | Size | Used by | Where |
|---|---|---|---|---|
| FreeRTOS heap_4 (vendor) | `static uint8_t ucHeap[]` | 32 000 bytes | Task stacks, TCBs, queues | `Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c` |
| Custom heap_4 fork | `static uint8_t ucHeap[]` | 32 000 bytes | micro-ROS allocations | `Core/Src/custom_memory_manager.c:30` |
| newlib sbrk | dynamic in `._user_heap_stack` | 512 bytes min | `printf` etc. | `Core/Src/sysmem.c` |

**the dual-heap sizing:** the two heap_4 instances both use the same `configTOTAL_HEAP_SIZE` constant, double-reserving RAM.

## Custom allocator design

`custom_memory_manager.c` is a fork of FreeRTOS `heap_4.c`. Same algorithm:

- Address-ordered free list.
- First-fit allocation.
- Bidirectional coalescing on free.
- Top-bit-of-size flag for ownership.
- `vTaskSuspendAll`/`xTaskResumeAll` critical section (not a mutex).

What's different from vendor heap_4:
- Renamed entry points: `pvPortMallocMicroROS`, `vPortFreeMicroROS`, `pvPortReallocMicroROS`, `pvPortCallocMicroROS`.
- Adds `getBlockSize()` accessor.
- Adds `pvPortReallocMicroROS` (vendor heap_4 has no realloc).
- Adds `pvPortCallocMicroROS` (vendor heap_4 has no calloc).

## Allocator adapter (`microros_allocators.c`)

49 lines. Wraps the four functions into `rcl_allocator_t` signatures:

```c
void * microros_allocate(size_t size, void * state) {
    (void) state;
    absoluteUsedMemory += size;
    usedMemory += size;
    return pvPortMallocMicroROS(size);
}
```

Plus `microros_deallocate`, `microros_reallocate`, `microros_zero_allocate`.

**Issue:** `usedMemory` and `absoluteUsedMemory` are `int` — signed-overflow risk over long runtimes. Should be `size_t`.

## Why fork instead of two instances of heap_4

You can't trivially run two instances of vendor heap_4 — its symbols are file-static. The fork solves this by renaming all entry points.

**Trade-off:** the fork diverges from upstream. Bug fixes in upstream heap_4 need to be ported manually.

## Lifetime analysis

| Allocator | Boot-time | Steady state |
|---|---|---|
| FreeRTOS heap | task stacks created (12 KB + 1 KB + 0.5 KB), TCBs (~few hundred B) | unchanged |
| Custom heap | rcl_init_options, rcl_context, node, publisher, subscriber, executor, type support | per-publish XRCE buffers (alloc + free per call); some realloc churn possible |
| sbrk | none (printf unused) | minimal |

After boot, the **FreeRTOS heap is essentially stable**. The custom heap sees churn from XRCE serialisation buffers.

## Fragmentation analysis

The custom heap's churn pattern:
- Per-publish: allocate ~300 B XRCE buffer, free after transmit. Same size each time → coalesce easily.
- Per-receive: similar, smaller.
- No interleaved long-lived + short-lived allocations of different sizes → fragmentation low.

NOT PROVEN with measurement. Risk is theoretical, not observed.

## Allocation ownership

| Allocation | Owned by | Freed at |
|---|---|---|
| rcl_init_options | once at init | never (process lifetime) |
| rcl_context | once | never |
| node | once | never |
| publisher state | once | never |
| subscriber state | once | never |
| executor | once | never |
| type support pointers | once | never |
| per-publish XRCE buffer | rcl_publish | within rcl_publish |
| per-receive XRCE buffer | executor spin | within spin |

The "never freed" entries are intentional — process lifetime = firmware lifetime.

## Recommendations (NOT MADE — review-only)

1. **the dual-heap sizing:** introduce a separate `configTOTAL_MICROROS_HEAP_SIZE` constant.
2. **Periodic high-water-mark log:** call `xPortGetMinimumEverFreeHeapSizeMicroROS()` every 10 s; publish via diagnostic topic OR encode in LED.
3. **Replace `int` counters in `microros_allocators.c` with `size_t`.**
4. **Replace `calloc` byte loop with `memset` (`custom_memory_manager.c:322-324`).**
5. **Add MIT license header to `custom_memory_manager.c`** preserving FreeRTOS attribution.
