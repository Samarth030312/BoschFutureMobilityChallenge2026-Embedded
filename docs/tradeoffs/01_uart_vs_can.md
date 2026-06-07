# Tradeoff 01 — UART vs CAN for STM32↔Jetson link

## Chosen path (current source)

**UART over USB-CDC bridge** at 460 800 baud, micro-ROS XRCE-DDS protocol.

Evidence: `usart.c:75`, `it_transport.c`, `Makefile:129` (`-lmicroros`).

## Alternative considered (CAN bus)

The STM32F401 family supports bxCAN (basic extended CAN). The Nucleo-F401RE exposes CAN1 pins. With an external transceiver (e.g., SN65HVD230), the STM32 could speak CAN to a Jetson-side CAN interface.

The team did not take this path.

## Side-by-side

| Property | UART (chosen) | CAN |
|---|---|---|
| Throughput | 460 kbps usable | 1 Mbps typical, 5 Mbps with CAN FD (F401 doesn't support FD) |
| Protocol overhead | XRCE-DDS framing | Plain CAN frame OR CANopen (more complex) |
| External hardware | None (Nucleo USB-CDC is built-in) | CAN transceiver required (~$2 chip + level-shifting) |
| Cable | USB cable | Two-wire twisted pair |
| Multi-drop | No (point-to-point) | Yes (multiple nodes on one bus) |
| Hot-plug recovery | OK at the host side (CDC re-enumerates) | More complex (bus-off recovery) |
| micro-ROS support | Built-in (XRCE-DDS UART transport) | Possible (XRCE-DDS over CAN via custom transport) but less mature |
| ROS 2 native integration | Via micro-ROS agent | Via socketcan + canopen + ros2_socketcan |
| Bus contention | None (point-to-point) | Arbitration handled at hardware level |
| Throughput utilization (current load) | ~50% | <10% |

## What was gained by choosing UART

- **Built-in physical layer.** USB cable in, `/dev/ttyACM0` out. No hardware to add.
- **Simplest transport callbacks.** The team's `it_transport.c` is 140 LOC.
- **No external transceiver cost or PCB rework.**
- **Easy on-bench debugging.** Plug Nucleo into laptop, see USART2 traffic via `minicom`.

## What was sacrificed

- **Single-link redundancy.** One USB cable; cable disconnect = vehicle parks via deadman.
- **No multi-drop.** Cannot add a second sensor MCU on the same bus.
- **Lower theoretical throughput.** CAN FD could carry richer telemetry; UART at 460800 hits the ceiling around 280 kbps usable.
- **No bus diagnostics.** CAN exposes error counters; UART overrun is only detectable via HAL error callbacks.

## When the team might switch

- **Adding a second STM32 / encoder MCU** — CAN allows them to share one Jetson interface.
- **Higher-volume telemetry** (e.g., publishing per-frame BNO085 status bytes, heap watermarks) at a rate UART can't sustain.
- **Industrial environment** where CAN's differential signalling is more EMI-resistant.

## Current cost-benefit reading

For a single-MCU competition vehicle with one publisher + one subscriber, UART is correctly the simpler choice. The drawbacks listed are not currently load-bearing.
