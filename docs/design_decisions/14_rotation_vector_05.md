# Decision 14 — BNO085 Rotation Vector (report 0x05)

## What the source confirms

- `BNO085_EnableReport(&bno085, BNO085_REPORT_ROTATION_VECTOR, 10000)` at boot (`freertos.c:117`).
- 100 Hz interval (10000 µs).
- Auto-recovery re-enables the same report (`freertos.c:322`).
- Report ID constant: `0x05` (`bno085.h:21`).
- Parser path: `case BNO085_REPORT_ROTATION_VECTOR` (`bno085.c:205-232`).
- 14-byte payload: 4 quat components in Q14 + accuracy in Q12.
- Published yaw covariance: `0.01 rad²` (`freertos.c:238`) — 10× wider than roll/pitch.

## Why 0x05 specifically

A new engineer should understand: the BNO085 supports two rotation-vector report variants:

| Report | DoF | Magnetometer? | Yaw drift | Yaw absolute reference |
|---|---|---|---|---|
| Game Rotation Vector (0x08) | 6 | No (accel + gyro only) | Unbounded (gyro drift integrates) | None (relative to boot pose) |
| **Rotation Vector (0x05)** | **9** | **Yes** | Bounded by mag accuracy | Magnetic north |

The team chose 0x05 because:

- **Bounded yaw drift.** On a competition track that the vehicle drives for minutes, unbounded yaw drift would make the Jetson EKF's world-frame yaw meaningless.
- **Absolute heading reference.** The vehicle's world-frame orientation is anchored to magnetic north.
- **Per-frame accuracy estimate.** Available in the Q12 accuracy field — not currently used by downstream code but exists.

## The trade-off the choice accepts

The 9-DoF report is more EMI-vulnerable. BLDC motor magnetic noise corrupts the magnetometer contribution to the fused quaternion. The team mitigates:

1. **Wider yaw covariance** at publish (1e-2 rad² vs 1e-3 for roll/pitch — `freertos.c:236-238`).
2. **Quaternion sanity gate** `|q|² ∈ [0.9, 1.1]` drops wildly-corrupted samples (`bno085.c:223`).
3. **Jetson-side adaptive R** scales the EKF measurement covariance based on the published value.
4. **Drift estimator** on the Jetson back-calculates a yaw-bias correction from lane geometry — closing the loop.

So the firmware's role is "publish the corrupted-but-flagged quaternion with honest covariance"; the Jetson's role is "trust it appropriately."

## Why NOT Game Rotation Vector (0x08)

The parser supports 0x08 (`bno085.c:174-203`) but the team does NOT enable it. Reasons:

- Unbounded yaw drift would require periodic re-alignment from another reference (vision-based pose? GPS? — neither is present on the STM32 side).
- The team's Jetson autonomy expects an absolute yaw with bounded error — 0x05 provides that.

## Why NOT Linear Acceleration (0x04)

The parser supports 0x04 (treated identically to 0x01 in the case statement — `bno085.c:151-161`) but the team enables 0x01 instead. Reasons inferable from `06_calibration.md`:

- The Jetson EKF needs the **gravity vector** in the published accel reading — to estimate pitch/roll from gravity direction.
- 0x04 (Linear Acceleration) removes gravity → EKF loses pitch/roll observability.
- 0x01 (calibrated accel) keeps gravity → EKF can use it.

## What this report's accuracy field means

Per SH-2 spec, the 14-byte 0x05 report includes:
- bytes 0-3: SHTP-level header (sequence, status, delay)
- bytes 4-5: i (Q14)
- bytes 6-7: j (Q14)
- bytes 8-9: k (Q14)
- bytes 10-11: real (Q14)
- bytes 12-13: heading accuracy estimate (Q12 radians)

The team reads the accuracy into `dev->data.quat_accuracy` (`bno085.c:228`) but **no consumer references it**. It's wasted information.

A future improvement: scale the published yaw covariance per-sample by `quat_accuracy`. Currently the covariance is a constant 1e-2 rad².

## Drawbacks visible in current source

- **EMI-vulnerability** (mitigated, not eliminated).
- **Accuracy field unused** — wasted per-sample telemetry.
- **No fallback to 0x08.** If the magnetometer fails entirely (chip damage, hard EMI), the firmware would still emit 0x05 with potentially wrong yaw. No detection of "mag is broken."

## Alternatives the choice rules out

- **0x08 only.** Would lose absolute yaw reference; require external re-anchoring.
- **Dual-report (0x05 + 0x08).** Could compare for divergence detection. Both reports at 100 Hz = 2 SHTP cargo per packet (~24 bytes vs 14). Bus + parser overhead. Doable but not done.
- **Custom fusion on STM32 from accel + gyro + mag** — no longer using BNO085 fusion. Substantial complexity.

## What a future redesign might pick

- **Add accuracy-field propagation** into the published covariance.
- **Add 0x08 as a sanity check** — detect when 0x05 vs 0x08 diverge significantly (indicates magnetometer disturbance).
- **Health-monitor the magnetometer** via the BNO085's status byte (currently discarded by the parser).

## What this decision constrains in current code

- The yaw-covariance contract with the Jetson EKF (1e-2 rad²) is tuned for the EMI-exposed Rotation Vector.
- The drift_estimator on the Jetson is part of the closure for the EMI vulnerability.
- The mitigation is **architectural** — touches firmware, fusion, and planning. Switching to 0x08 would require coordinated changes across all three.
