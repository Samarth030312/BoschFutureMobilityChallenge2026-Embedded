# Tradeoff 08 — Blocking vs non-blocking UART TX

## Chosen path (current source)

**Blocking `HAL_UART_Transmit`** with 100 ms timeout (`it_transport.c:78`).

## Alternative considered (non-blocking IT or DMA TX)

`HAL_UART_Transmit_IT(uart, buf, len)` or `HAL_UART_Transmit_DMA(uart, buf, len)` — return immediately, signal completion via `HAL_UART_TxCpltCallback`.

## Side-by-side

| Property | Blocking (chosen) | IT / DMA TX |
|---|---|---|
| Task control | Blocks until done (~1.9 ms for typical 80-byte XRCE message, more for IMU publish) | Returns immediately |
| gState race | Eliminated by serialisation | Real — multiple TX requests can race |
| Lock requirement | None | Software lock at the transport layer |
| Code complexity | Trivial (one HAL call) | More (lock + completion callback + queue) |
| CPU saving | None during TX | Significant (CPU free during the ms-scale TX) |
| Recovery on TX error | Returns 0; XRCE retransmits | Same; plus the completion callback might fire on error |
| Deterministic timing | Yes (bounded) | Less so |

## The "gState race"

`HAL_UART_Transmit_IT` checks the UART's internal `gState`. If a previous IT-mode TX is still in flight, it returns `HAL_BUSY`. The XRCE library calls `cubemx_transport_write` from multiple sites (ping, sync, publish, callback responses). If two of these happen close together with non-blocking TX, one can get `HAL_BUSY` and silently fail.

The source's own comment (`it_transport.c:65-69`) confirms the team saw this race ("Eliminates the gState race entirely") and chose blocking specifically to avoid it.

## What was gained by choosing blocking

- **No gState race.** XRCE writes serialise by definition.
- **Predictable timing.** Each `cubemx_transport_write` takes a known amount of time.
- **Simpler code.** No locks, no completion callback, no state machine.

## What was sacrificed

- **CPU time during TX.** The task is blocked in `HAL_UART_Transmit` for the duration. At 460 800 baud, ~6 ms for an IMU publish packet (~280 bytes XRCE). Multiply by 100 Hz publish rate: ~600 ms/s of "task busy in TX." Real but bounded.
- **No background TX.** Can't overlap TX with other work.

## When the team might switch

- **Higher publish rates** (1 kHz IMU) where 6 ms × 1000 = 6 s/s > 1 s. Wouldn't fit.
- **Adding diagnostic / log publishers** that compete with the IMU publish — non-blocking TX would let them queue.
- **Adding a second sensor** publishing to ROS in parallel.

## Switch cost

The CPU saving is real but conditional. Non-blocking requires:
- Software lock at the transport layer.
- Completion callback wiring.
- Possibly a TX queue if multiple writers can pre-empt each other.

The team avoided this complexity.

## Current cost-benefit reading

Blocking TX is correct for **this** firmware at **this** publish rate. The CPU cost is recovered during idle (`osDelay(1)`); the main loop has comfortable margin. The simplicity is the durable benefit.
