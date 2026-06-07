# Decision 11 — Polled (blocking) HAL I²C, not DMA

## What the source confirms

- `HAL_I2C_Master_Receive(dev->hi2c, BNO085_I2C_ADDR, header, BNO085_SHTP_HEADER_SIZE, 100)` — polled HAL with 100 ms timeout (`bno085.c:14-15`).
- Same pattern for cargo read (`bno085.c:28-29`) and `HAL_I2C_Master_Transmit` for Set Feature (`bno085.c:51-52`).
- I²C1 NVIC IRQ enabled in MspInit but the team's source does NOT use interrupt-driven or DMA I²C — it uses the blocking `Master_Receive`/`Master_Transmit` calls.

## Why polled (not DMA)

A new engineer should understand: SHTP is a **two-stage protocol**. The first read is 4 bytes (the header); the firmware parses the length out of the header; the second read is `packet_len` bytes (the cargo). The length is variable.

DMA mode is most efficient for transactions of **known fixed length**. For SHTP's variable cargo, DMA would require:

- A first DMA transaction to read the header.
- An interrupt + ISR to compute the cargo length from the header.
- A second DMA transaction to read the cargo.
- Another interrupt + ISR to signal "cargo complete."

That's the same number of interrupts as just calling `HAL_I2C_Master_Receive` twice — but with more complexity (state machines, callbacks, error handling).

Polled HAL is simpler:
- `HAL_I2C_Master_Receive(header, 4, 100)` — blocks ~140 µs at 400 kHz for 4 bytes + address + START/STOP.
- Parse length.
- `HAL_I2C_Master_Receive(cargo, len, 100)` — blocks proportionally for the cargo.

Total: ~3-5 ms blocking per packet at 100 Hz.

## Why blocking is acceptable here

- The blocking happens **in the task context** (not an ISR). The task is the only user task; its block doesn't preempt anything.
- The 100 Hz BNO085 rate means a ~5 ms blocking call followed by ~5 ms of "task does other stuff" still hits the cadence.
- The HAL timeout (100 ms) bounds the worst case. A stuck bus is detected and reported via `HAL_ERROR`; the auto-recovery counter increments.

## The hidden cost: I²C stall blocks publish + actuation

The worst case for `BNO085_GetData` is two consecutive HAL timeouts = 200 ms. During those 200 ms:

- `rcl_publish` doesn't fire (the task is blocked in I²C).
- The deadman check doesn't run (same task).
- TIM2 keeps emitting its last CCR.

The 500 ms firmware deadman + Jetson-side supervisors absorb this.

This is the unavoidable cost of polled I²C on a single task. DMA wouldn't help: the alternative is "block in DMA wait" or "yield via osDelay and poll DMA completion" — neither buys real concurrency without a second task.

## Drawbacks visible in current source

- **200 ms worst-case stall.** Documented.
- **No SDA-stuck recovery.** If the BNO085 wedges holding SDA LOW, neither polled nor DMA I²C will recover. The 50-error auto-recovery (`freertos.c:315-326`) does NOT issue SDA clock pulses to free the bus.
- **No I²C error ISR.** I2C1_EV_IRQ and I2C1_ER_IRQ are not enabled (not visible in `i2c.c`). HAL polled mode doesn't need them.

## Alternatives the choice rules out

- **DMA I²C.** Could be done; doesn't help SHTP's two-stage structure.
- **Interrupt-driven I²C** (`HAL_I2C_Master_Receive_IT`). Non-blocking; requires the task to poll `HAL_I2C_GetState`. Saves CPU during the I²C transaction but doesn't save wall-clock time.
- **Software bit-banging I²C** on regular GPIO. Slower but interruptible. No clock-stretch support unless coded.

## What a future redesign might pick

- **Add SDA-stuck recovery** in `BNO085_Init`. Standard pattern: detect SDA stuck LOW, manually clock SCL 9 times to free the slave, then HAL_I2C_Init again.
- **Move I²C to a dedicated sensor task** (would also need to address the FPU context-save state).
- **Use DMA I²C** if a future re-design ever batches reads (e.g., reading 100 BNO085 packets at once instead of 1) — would amortise the overhead.

## What this decision constrains in current code

- The 100 ms timeout bounds the main-loop stall.
- The two-stage SHTP read pattern (`bno085.c:9-31`) depends on synchronous, sequential HAL calls.
- The auto-recovery threshold (50 errors) is calibrated against the polled cadence.
- No I²C ISR exists; the vector slot is dormant.
