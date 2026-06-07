# Tradeoff 07 — Polled vs DMA (for I²C and UART)

## Chosen path (current source)

- **I²C:** polled HAL (`HAL_I2C_Master_Receive`, `bno085.c:14-15, 28-29`).
- **UART RX:** interrupt-driven single-byte mode (`HAL_UART_Receive_IT`, `it_transport.c:54, 116, 137`).
- **UART TX:** blocking HAL (`HAL_UART_Transmit`, `it_transport.c:78`).
- DMA is configured for USART2 RX (DMA1_Stream5) but **unused** .
- DMA is configured for USART1 RX/TX (DMA2_Stream2, DMA2_Stream7) but **unused** (USART1 dead, the dead USART1 init).

## Why DMA was rejected for the active paths

### For I²C (SHTP)

DMA requires **fixed transfer lengths known at start**. SHTP is two-stage: header (4 bytes) → parse → cargo (variable). DMA can't span the two phases without an interrupt + re-arm in between — same number of interrupts as polled HAL, with more state-machine complexity.

See `design_decisions/11_polled_i2c.md` for the full analysis.

### For UART RX

The team uses **single-byte IT mode**. At 460 800 baud = 46 080 bytes/s = ~46 kHz ISR rate. Each ISR is ~5-10 µs of overhead. Total: ~0.5-1 % CPU.

DMA could batch this to 2 ISRs per buffer (half-transfer + full-transfer complete). But:
- The XRCE framing has variable message sizes; the team would need a ring-buffer DMA with periodic length checks.
- More complex state (DMA pointer, ring head/tail).

The team chose simplicity over DMA's CPU saving.

### For UART TX

Blocking — see `design_decisions/12_blocking_uart_tx.md` and `tradeoffs/08_blocking_vs_nonblocking_tx.md`.

## Side-by-side (UART RX specifically)

| Property | IT single-byte (chosen) | DMA circular ring |
|---|---|---|
| Ring buffer size | 2 KiB | 2 KiB (same) |
| ISR rate at 460 800 baud | ~46 kHz | ~2 ISRs per buffer = ~22 Hz |
| CPU cost | ~1 % | ~0.05 % |
| Code complexity | ~140 LOC | ~250 LOC |
| Overrun handling | `HAL_UART_ErrorCallback` self-heals | More complex (must check ring tail vs DMA position) |
| Buffer fragmentation | None | None (circular) |
| Setup complexity | One `HAL_UART_Receive_IT(&it_data, 1)` | DMA Init + `HAL_UART_Receive_DMA` + pointer arithmetic |
| Latency to first byte | ~5 µs (ISR entry) | Same |

## What was gained by choosing IT single-byte

- **Simplicity.** `it_transport.c` is 140 LOC including TX, open, close, and error callback.
- **Easy to reason about.** One ISR pushes one byte; one consumer pops one byte.
- **Robust overrun recovery.** The error callback fires on any ORE; the team clears + re-arms.

## What was sacrificed

- **~1 % CPU.** Negligible at 84 MHz but visible in a profiler.
- **High ISR rate.** Wastes context switches.

## When the team might switch

- **CPU budget pressure.** If a future feature needs more cycles, DMA RX saves ~1 % easily.
- **Higher baud rates** (920 800 or higher) where the ISR rate doubles.
- **Lower-power operation** where idle-time matters.

## Current cost-benefit reading

The 1 % CPU cost is acceptable. The simplicity benefit pays for that cost in maintainability. The team explicitly notes the trade-off in `it_transport.c:65-69` for TX; the RX choice follows the same philosophy.
