# BNO085 — 08: Error Handling

## The race-free EXTI gate (the most important pattern)

```c
if (bno085_int_flag ||
    HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET) {
    bno085_int_flag = false;   /* clear BEFORE the read */
    if (BNO085_GetData(&bno085) == HAL_OK && bno085.data.data_valid) {
        /* use data */
    }
}
```
(`freertos.c:308-309`)

### The race it solves

```
t=0:  Packet 1 ready  → INTN ↓ → EXTI fires → flag set
t=1:  Task reads PB4 LOW; clears flag; starts BNO085_GetData
t=2:  I²C transaction in progress (typically 5 ms)
t=3:  Packet 2 becomes ready  → INTN was already LOW → INTN stays LOW
      → falling-edge EXTI gets NO new edge → flag is NOT set
t=4:  Task finishes reading Packet 1; loop iteration done
t=5:  Next iteration: flag is clear → flag-only gate would skip
      BUT: PB4 is still LOW → pin-LOW fallback triggers read of Packet 2
```

Without the pin-LOW fallback, the parser would have missed Packet 2. The BNO085 would be 10 ms behind reality on average, accumulating to half a second over a minute.

### Why clear-before-read

If the flag is cleared AFTER the read, a re-assert during the read is missed by the flag. Clearing before means any re-assert produces a new falling edge that the ISR latches for the next loop tick.

```
t=0: flag = false
t=1: INTN ↓ → flag = true
t=2: detect flag true → clear flag → BNO085_GetData starts
t=3: during read, new packet ready, INTN ↓ → ISR sets flag = true
t=4: read finishes; loop iteration ends
t=5: next iteration: flag is true → read again ✓
```

## 50-error auto-recovery

```c
HAL_StatusTypeDef imu_status = BNO085_GetData(&bno085);
if (imu_status == HAL_OK) {
    imu_err_count = 0u;
} else if (++imu_err_count >= 50u) {
    imu_err_count = 0u;
    /* Re-init + re-enable; do NOT re-run calibration */
    BNO085_Init(&bno085, &hi2c1);
    BNO085_EnableReport(&bno085, BNO085_REPORT_ROTATION_VECTOR, 10000);
    BNO085_EnableReport(&bno085, BNO085_REPORT_ACCELEROMETER,   10000);
    BNO085_EnableReport(&bno085, BNO085_REPORT_GYROSCOPE,       10000);
    bno085_int_flag = false;
}
```
(`freertos.c:312-326`)

50 consecutive errors × ~10 ms (BNO085 packet interval) = ~500 ms of consecutive failure.

Why this matters: a brief I²C glitch (NACK, electrical noise) might cause 1-2 errors before recovering. Auto-recovery on every single failure would be noisy. 50 is short enough to recover quickly, long enough not to thrash.

## What auto-recovery does

| Action | Reason |
|---|---|
| `BNO085_Init` | Re-flushes startup packets; resets sequence counters; re-asserts the I²C connection |
| Re-enable 3 reports | The BNO085 may have lost its configuration; re-arm at 100 Hz |
| Clear `bno085_int_flag` | Any stale latched flag is invalidated |

## What auto-recovery does NOT do

- **Hardware reset.** The BNO085's nRESET pin is not driven by the STM32 (hardware-tied HIGH).
- **I²C bus unstick.** If SDA is held LOW by a wedged BNO085, no `BNO085_Init` will help. The bus needs nine clock pulses or a peripheral reset to free SDA. Currently not implemented.
- **Re-run calibration.** Deliberate (`06_calibration.md` §"Auto-recovery does NOT re-run calibration").

## EMI-induced corruption: quaternion sanity gate

```c
if (mag2 >= 0.9f && mag2 <= 1.1f) {
    /* accept */
}
```
(`bno085.c:192-201`)

Silently rejects EMI-corrupted quaternions. The last good value stays in `dev->data`.

This is the lightest error-handling tool in the firmware. No counter, no recovery — just a silent drop. Justification: under sustained EMI, MANY quaternions will be rejected; the BNO085 still emits the correct values between corruptions; the publish path keeps using the most recent valid one.

## Parser bounds checks

```c
if (report_len == 0) break;                       // unknown ID
if ((offset + report_len) > cargo_len) break;     // bounds check
```
(`bno085.c:138-145`)

Defensive against:
- Truncated packet (BNO085 returned partial cargo).
- Corrupted length field that snuck past the SHTP-layer checks.
- Unknown report ID (BNO085 firmware update emits a new report type).

In all cases, the walker aborts gracefully — any reports parsed so far still take effect via `any_parsed → data_valid = true`.

## HAL I²C 100 ms timeout

Every `HAL_I2C_Master_Receive` / `Transmit` has a 100 ms timeout (`bno085.c:14, 28, 51`).

A stuck bus blocks the task for 100 ms (header read) + 100 ms (cargo read) = up to 200 ms. The deadman timeout of 500 ms tolerates this.

## Diagnostic LED pattern (NOT IMPLEMENTED — recommended)

Currently PA5 is toggled at 10 Hz unconditionally (`freertos.c:406-409`). A future improvement: encode IMU health in the LED pattern:

| State | Pattern |
|---|---|
| Healthy 100 Hz IMU + agent connected | 10 Hz steady |
| Calibration in progress | 5 Hz |
| Calibration failed (sample_count = 0) | fast flicker (50 Hz) |
| Auto-recovery in progress | 2 Hz |
| Agent disconnected | OFF |

Easy to add. Not currently done.

## Failure-mode summary table

| Failure | First-line detection | Recovery | Time-to-recover |
|---|---|---|---|
| Single NACK | HAL returns error | err_count++ | next iteration |
| Brief I²C glitch (<50 errors) | err_count accumulates | next successful read resets it | <500 ms |
| Sustained I²C errors (≥50) | err_count threshold | re-init + re-enable | ~50 ms after the 500 ms threshold |
| EMI-corrupted quat | Sanity gate `|q|² ∉ [0.9, 1.1]` | last good quat retained | next valid packet |
| BNO085 hardware reset (rare) | First few reads will see SHTP startup | Auto-recovery treats as bus errors → re-init | <1 s |
| Bus stuck LOW (wedged) | Persistent timeouts | None — IWDG would help (currently disabled, the IWDG-disabled state) | infinite without IWDG |
| Stuck `bno085_int_flag` | Wouldn't happen — flag is bool, set/cleared deterministically | N/A | N/A |
| 50-error counter overflow | Hits 50, recovers, counter reset | N/A | N/A |
