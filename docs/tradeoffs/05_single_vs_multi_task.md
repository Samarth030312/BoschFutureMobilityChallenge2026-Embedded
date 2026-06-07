# Tradeoff 05 — Single-task vs multi-task

## Chosen path (current source)

**Single task: `microROSTask`** (priority `osPriorityNormal`, 12 KB stack, `freertos.c:45-50, 87`).

All sensor reads, ROS publishes, executor spins, and actuation in one loop body.

## Alternative considered (2-3 tasks)

A producer-consumer split:
- **IMU task:** poll BNO085 INTN flag, do SHTP read + parse, place data in a queue.
- **ROS task:** dequeue IMU data, publish; receive `/control/command` callbacks; write into a shared command struct.
- **Actuation task:** wake periodically, read command struct, write PWM CCR, run deadman check.

## Side-by-side

| Property | Single task (chosen) | Multi-task |
|---|---|---|
| Lines of code | ~440 (freertos.c) | ~600-800 (with queues + sync) |
| RAM cost (stacks) | 1 × 12 KB | 2 × 4 KB + 1 × 4 KB = 12 KB; or 3 × 8 KB = 24 KB if conservative |
| Synchronization | None needed (same context) | Queue + possibly mutex |
| Priority inversion risk | None (one task) | Real (with mutex) |
| FPU context save | Skipped (`configENABLE_FPU = 0`) | Required (`configENABLE_FPU = 1`) |
| Decoupling under stall | None — I²C stall blocks publish + actuation | I²C stall blocks IMU task only; publish + actuation continue |
| Easier to debug | Yes — sequential | Harder — interleaved |
| Easier to add diagnostic publisher | No — must interleave into main loop | Yes — just another low-priority task |
| Deterministic timing | Yes — fixed cadence | Variable — depends on scheduler |
| Easier to reason about | Yes | Less |

## What was gained by choosing single-task

- **Simplicity.** One loop body. One context. No race conditions to debug.
- **Minimal heap usage.** Only one task stack (12 KB) on the FreeRTOS heap.
- **`configENABLE_FPU = 0` is safe.** No context save means task switches are faster.
- **No queues / no mutexes.** Less code.

## What was sacrificed

- **I²C stall blocks everything.** A 100 ms HAL_I2C timeout × 2 (header + cargo) = 200 ms freezes publish + actuation update. Deadman tolerates up to 500 ms.
- **No producer/consumer pipeline.** Can't overlap sensor read with previous publish.
- **Difficult to add diagnostic telemetry.** Would need to interleave into the main loop.
- **Single-point-of-failure for any task-level bug.** A bug here halts everything.

## When the team might switch

- **Adding any second I²C sensor.** Would benefit from a producer task isolating it.
- **Adding diagnostic publishers** for heap/stack/error telemetry.
- **Adding higher-rate actuation** that can't tolerate the I²C stall window.

## Switch cost

The big cost: **`configENABLE_FPU` must change from 0 to 1** . The team's BNO085 quaternion math uses the FPU; adding a second task that also uses floats requires per-task FPU save/restore.

Plus heap budgeting (the dual-heap sizing needs splitting), queue allocations, debugging.

## Current cost-benefit reading

Single-task is correctly chosen for this workload. The decoupling that multi-task would provide is not needed at 100 Hz IMU + 50 Hz PWM + 20 Hz `/control/command`. The team's main loop has comfortable margin.

The architecture is **load-bearing on this choice** — see `design_decisions/10_single_task.md`.
