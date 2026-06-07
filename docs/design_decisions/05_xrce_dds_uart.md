# Decision 05 — XRCE-DDS over UART

## What the source confirms

- USART2 used for micro-ROS transport (`freertos.c:130` binds `&huart2` as transport args).
- 460 800 baud (`usart.c:75`).
- USB-CDC bridge via Nucleo ST-LINK USB → Jetson's `/dev/ttyACM*`.
- IT-mode RX into 2 KiB SPSC ring (`it_transport.c:31, 109-117`).
- Blocking TX via `HAL_UART_Transmit` (`it_transport.c:70-80`).
- ErrorCallback re-arms after ORE/NE/FE/PE (`it_transport.c:123-138`).
- Custom transport using `rmw_uros_set_custom_transport(true, ...)` — the `true` indicates framing-on (XRCE adds STX + length + checksum).

## Why UART (not USB, not Ethernet)

A new engineer should understand: the Nucleo board has a built-in USB-CDC bridge — plug in USB to the Jetson, the Jetson sees `/dev/ttyACM0`. The bridge is implemented by the ST-LINK chip and presents whatever the STM32 puts on USART2.

So:
- **No USB device stack needed on the STM32.** USART2 is the simplest possible interface.
- **No network stack.** Ethernet on F401 would require external PHY + LwIP/uIP — orders of magnitude more code.
- **No WiFi.** No on-board WiFi; requires external module + driver — out of scope.

UART is the path of least resistance for a Nucleo-to-host link.

## Why XRCE-DDS

XRCE-DDS = "eXtensible and Reduced Capability Endpoint for DDS."

- **A protocol for resource-constrained DDS clients.**
- micro-ROS uses XRCE-DDS as its wire protocol; the agent translates to native DDS.
- Built into the micro-ROS library — no additional code to write.

The team's role is just to provide a transport. The XRCE protocol itself is handled by `libmicroros.a`.

## The custom transport (4 callbacks)

The team's `it_transport.c` implements:

```c
bool cubemx_transport_open(struct uxrCustomTransport *);
bool cubemx_transport_close(struct uxrCustomTransport *);
size_t cubemx_transport_write(struct uxrCustomTransport *, const uint8_t *, size_t, uint8_t *);
size_t cubemx_transport_read(struct uxrCustomTransport *, uint8_t *, size_t, int, uint8_t *);
```

These are pluggable — any transport (UART, UDP, USB) could provide them.

## Why IT-mode RX (not DMA)

- **IT mode** drains one byte at a time via `HAL_UART_RxCpltCallback`. Simple; tracks the ring's `it_tail`.
- **DMA mode** would batch bytes via circular DMA, signalling at half/full transfer interrupts. Lower ISR rate but more complex bookkeeping around partial-buffer reads.

The team's choice (IT mode) is simpler. At 460 800 baud, the ISR rate is ~46 kHz — measurable CPU cost but not dominant.

For the rationale on **why blocking TX** instead of IT/DMA TX, see Decision 12.

## Why this specific UART + framing

- **`rmw_uros_set_custom_transport(true, ...)`** — the `true` enables XRCE framing (STX + length + CRC). Without it, a single byte loss would corrupt the entire packet without detection.
- **2 KiB ring buffer.** Source comment (`it_transport.c:27-28`): "absorbs ~40 ms of sustained 460800-baud traffic. RAM budget is tight (97 % used) so larger isn't feasible right now."
- **Drop-on-full** (`it_transport.c:111-115`). When the ring is full, the new byte is dropped — preserves the integrity of the message already in the ring (XRCE retransmits on detection).

## Drawbacks visible in current source

- **No CAN bus.** ROS 2 over CAN exists (canopen_402 stack) but would require an external transceiver.
- **No flow control.** USART2 has no RTS/CTS in the team's config (`usart.c:80`: `HwFlowCtl = NONE`). Drop-on-full handles overrun at the application layer.
- **Cable disconnect is silent.** XRCE protocol detects session loss eventually; the firmware's `rmw_uros_ping_agent` is only called at init.
- **Single point of failure.** One USB cable; no redundant link.

## Alternatives the choice rules out

- **CAN bus** (would require external transceiver).
- **Ethernet** (would require external PHY).
- **WiFi** (external module + significant driver code).
- **USB CDC native on STM32** (replaces the ST-LINK bridge; bypass — pulls in USB device stack).

## What a future redesign might pick

- **Add a CAN secondary link** for redundancy. STM32F4 has CAN peripheral; an external SN65HVD230 transceiver + ~200 lines of code.
- **Move to USB CDC native** to eliminate the ST-LINK bridge. Frees USART2 for telemetry.
- **Move to UDP over Ethernet** for higher-throughput / lower-latency link, if ever needed (it isn't currently).

## What this decision constrains in current code

- `it_transport.c` is shaped around the UART HAL interface.
- The 2 KiB ring size is bounded by the RAM budget.
- USART2 RXNE/TC handling is HAL-driven.
- Domain ID, retry timing — all assume the transport is bidirectional (one socket).

A new transport (e.g., CAN) would require parallel callbacks and possibly a new session-management strategy.
