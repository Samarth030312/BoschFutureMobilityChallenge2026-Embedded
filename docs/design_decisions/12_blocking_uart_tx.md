# Decision 12 — Blocking `HAL_UART_Transmit` (not IT or DMA TX)

## What the source confirms

- `cubemx_transport_write` calls `HAL_UART_Transmit(uart, buf, len, 100u)` (`it_transport.c:78`).
- 100 ms timeout.
- Returns `len` on HAL_OK; `0` on any error.

## Why blocking TX

The source's own comment (`it_transport.c:65-69`):

> "Blocking TX. ~80-byte XRCE message at 460800 baud = ~1.9 ms. Well inside the urosTransport task's 100 ms ping budget and the 5 ms spin loop delay. Eliminates the gState race entirely."

The "gState race" the comment refers to: HAL UART driver tracks state via the `gState` field. If a non-blocking TX (`HAL_UART_Transmit_IT` or `_DMA`) is in flight when something else queries state, you can hit a `HAL_BUSY` return. Some operations don't tolerate that.

Blocking TX is synchronous: enter the function, return when the buffer is fully transmitted (or timeout). No state race.

## Cost analysis

At 460 800 baud, 10 bits per byte → 21.7 µs per byte.

XRCE message sizes (estimated from frame contents — NOT PROVEN):
- IMU publish: ~280 bytes payload + ~10 bytes XRCE framing = ~290 bytes → ~6.3 ms
- Control command receive: ~30 bytes + framing = ~40 bytes (received, not transmitted)
- Session control / ping / sync: 10-50 bytes → ~0.2-1.1 ms

The source comment says ~1.9 ms for an "80-byte XRCE message." That's the typical XRCE control packet, not the IMU publish.

For an IMU publish, blocking ~6 ms per 100 Hz publish = 600 ms/s of CPU time spent in `HAL_UART_Transmit`. At 84 MHz that's a small fraction; in wall-clock terms, the task is "busy in TX" for 60 % of its tick budget — but this happens during the `osDelay`-equivalent window where the task would otherwise be idle anyway.

## Drawbacks visible in current source

- **Block during TX.** While in `HAL_UART_Transmit`, the task can't poll the BNO085 EXTI flag, can't service an inbound `/control/command` callback, can't update PWM. Bounded by the timeout but visible.
- **No diagnostic on TX failure.** If `HAL_UART_Transmit` returns error (e.g., timeout), the team's code logs nothing — just returns 0 from the transport. The XRCE layer above retransmits.
- **No DMA TX.** Would offload the byte-by-byte transmission and free the CPU for other work. Trade-off: more state complexity.

## Why the gState race matters here

micro-ROS calls the transport's write at multiple points:
- During `rmw_uros_ping_agent`.
- During `rmw_uros_sync_session`.
- During `rcl_publish`.

If two of these happen close together (or if a publish is in progress when a callback fires), an IT TX could race with the start of a new one. Blocking TX serialises them by definition.

## Alternatives the choice rules out

- **HAL_UART_Transmit_IT**: non-blocking, but the gState race. Would need a software lock at the transport layer.
- **HAL_UART_Transmit_DMA**: same risk plus DMA stream setup. DMA channel for USART2 TX is not currently configured (only RX side is, and that's also dead — the unused USART2 RX DMA).
- **Custom register-level UART driver**: bypass HAL, manage state manually. More work; no clear benefit.

## What a future redesign might pick

- **DMA TX** with explicit software lock + state tracking. Frees CPU during the ~6 ms TX window.
- **Diagnostic counter on TX failures.** Log them or publish a counter via diagnostic topic.
- **Adaptive timeout** that scales with payload size — for very large XRCE control packets (rare), 100 ms might be tight.

## What this decision constrains in current code

- `cubemx_transport_write` cannot easily be non-blocking without lock additions.
- The main loop tolerates the ~6 ms TX time because nothing else has a tighter requirement.
- The 100 ms HAL timeout is also the bound on `HAL_UART_Transmit` (`it_transport.c:78`); if XRCE ever sends a single packet > ~46 KB, the timeout becomes a problem (it doesn't).

The blocking-TX model is a deliberate simplicity-over-throughput trade. It works at this firmware's load.
