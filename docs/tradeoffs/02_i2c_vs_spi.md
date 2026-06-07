# Tradeoff 02 — I²C vs SPI for BNO085

## Chosen path (current source)

**I²C1 at 400 kHz Fast-mode** with the BNO085 at address `0x4A << 1`. SHTP protocol over I²C.

Evidence: `bno085.h:9`, `i2c.c:41`, `bno085.c` (entire driver assumes I²C).

## Alternative considered (SPI)

The BNO085 supports SPI via its PS0/PS1 strap pins. With a hardware reconfiguration, the same chip could be addressed at up to ~10 MHz SPI.

## Side-by-side

| Property | I²C (chosen) | SPI |
|---|---|---|
| Speed | 400 kHz (Fast-mode max for BNO085) | Up to 10 MHz typical |
| Wires | 2 (SCL + SDA) + INTN + power | 4 (CLK + MOSI + MISO + CS) + INTN + power |
| Throughput per packet | ~5 ms for 30-byte SHTP | ~25 µs for same packet at 10 MHz |
| Bus contention | Multi-slave (could add more devices on same bus) | Per-device CS (more wires for multiple slaves) |
| Clock stretching | Yes (BNO085 may stretch during fusion) | Not applicable (master-driven clock) |
| Pull-up resistors | Required (internal in STM32 + optional external) | Not required (push-pull) |
| EMI vulnerability | Open-drain bus more EMI-tolerant | Differential pairs more EMI-tolerant; SPI single-ended worse than I²C in some regimes |
| Protocol structure | Address + R/W + ACK per byte | Raw clocked bytes |

## What was gained by choosing I²C

- **Two-wire simplicity.** Fewer cables to route.
- **Fewer STM32 pins consumed.** PB8/PB9 + PB4 INTN = 3 pins (vs 4 for SPI).
- **No CS line management.** Saves a GPIO and a code path.
- **Adequate throughput.** 5 ms per packet at 100 Hz uses 50 % of the bus; plenty of margin.

## What was sacrificed

- **20× lower bus speed than SPI.** Not a problem at 100 Hz IMU rate; would be at, say, 1 kHz.
- **Stuck-bus risk.** If the BNO085 wedges holding SDA LOW, the bus is unrecoverable without SCL clock-pulse recovery (not implemented).
- **Slower error recovery.** I²C NACK error detection is per-byte; SPI is per-transaction.
- **Open-drain at 400 kHz is on the edge of long-cable reliability.**

## When the team might switch

- **Higher publish rate** (e.g., 1 kHz IMU for high-speed dynamics). I²C at 400 kHz can't sustain it; SPI at 10 MHz easily can.
- **Longer cable runs.** SPI is more cable-tolerant in some configurations.
- **Stuck-bus hardening.** Switching to SPI eliminates the SDA-LOW wedge failure mode.

## Current cost-benefit reading

I²C is correctly chosen for this workload. The 100 Hz IMU at 400 kHz I²C has comfortable bus-time margin. The wire-count advantage matches a hobby-grade build.
