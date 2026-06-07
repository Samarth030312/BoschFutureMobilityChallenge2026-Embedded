# Tradeoff 10 — Static vs dynamic micro-ROS allocation

## Chosen path (current source)

**Dynamic allocation** via the custom heap fork (`custom_memory_manager.c`) installed as the rcl default allocator (`freertos.c:139-147`).

`configSUPPORT_STATIC_ALLOCATION = 1` and `configSUPPORT_DYNAMIC_ALLOCATION = 1` are both set in `FreeRTOSConfig.h:63-64` — both modes are *available*, but the team uses dynamic.

## Alternative considered (static-only)

micro-ROS supports a fully static memory model where every entity's storage is pre-declared at compile time:

```c
static char publisher_storage[PUB_SIZE];
static char subscriber_storage[SUB_SIZE];
static char node_storage[NODE_SIZE];
...
```

No runtime allocations. The custom heap could be eliminated.

## Side-by-side

| Property | Dynamic (chosen) | Static-only |
|---|---|---|
| RAM usage at boot | Allocations happen during bring-up | All RAM reserved at link time |
| Custom heap required? | Yes (32 KB BSS — the dual-heap sizing) | No |
| Code complexity | Adapter + heap fork | More boilerplate per entity |
| OOM risk | Real (silent NULL-return) | None |
| Fragmentation risk | Real (small in practice) | None |
| Build-time RAM analysis | Hard (run-time bound) | Easy (linker `.map`) |
| Flexibility (add publishers at runtime) | Yes | No (static = compile-time only) |
| Per-publish allocation | Yes (XRCE buffer per call) | More tricky — typically uses pre-allocated buffers from a pool |

## What was gained by dynamic

- **Simpler code.** No per-entity static-storage declarations.
- **Standard rcl/rclc usage.** The team's `freertos.c:175-205` reads like ordinary micro-ROS code.
- **Easier to add publishers later** — just call `rclc_publisher_init_default` again.

## What was sacrificed

- **the dual-heap sizing dual-heap collision.** 32 KB of BSS for the custom heap; collides with FreeRTOS internal heap.
- **OOM is silent.** Out-of-memory in micro-ROS returns NULL → publish fails → no error path.
- **Less deterministic.** Allocation timing depends on heap state.
- **Fragmentation possibility.** The custom heap's bidirectional coalescing mitigates but doesn't eliminate.

## When the team might switch

- **RAM pressure.** If the dual-heap sizing is closed by splitting heap sizes (recommended), dynamic stays viable. If RAM gets even tighter (e.g., adding diagnostic publishers + telemetry), static may save ~32 KB.
- **Safety-critical certification.** ASIL/IEC 61508 typically require static allocation.
- **Deterministic profile.** A real-time application that can't tolerate allocator jitter would benefit from static.

## Switch cost

Going from dynamic to static requires:
- Per-entity static-storage declarations.
- Switching from `rclc_publisher_init_default` to the explicit alloc-buffer variant.
- Pre-declaring per-publish CDR buffers.
- Code becomes verbose; each entity adds ~20-50 lines.

The team avoided this complexity.

## Current cost-benefit reading

Dynamic is the standard micro-ROS pattern. The team's custom heap is the right backing — it isolates micro-ROS from FreeRTOS internals. The the dual-heap sizing issue is a sizing problem, not an architectural problem.

For a future cycle: keep dynamic, split the heap size constants, add OOM telemetry. That gives the best of both.
