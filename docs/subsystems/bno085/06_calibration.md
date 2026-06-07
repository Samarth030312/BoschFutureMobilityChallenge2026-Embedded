# BNO085 — 06: Calibration

## Why boot-time calibration

The vehicle chassis sits with ~1-2° static tilt that the accelerometer reports as a constant ~0.2 m/s² bias on X/Y. The gyro carries a few mdeg/s of zero-rate bias. Both would poison downstream integration (velocity estimate, yaw-rate-integrated heading) before vision could relocalize.

The team's solution: a **blocking 2-second boot routine** that averages accel + gyro samples and captures a baseline quaternion. Results stored once in `dev->calib`; applied as simple subtract + Hamilton product on every publish.

## The routine

`BNO085_PerformStartupCalibration` (`bno085.c:339-438`):

```
For 2 seconds (2000 ms):
  - Use race-free EXTI gate (flag OR pin LOW)
  - For each valid sample:
    - Accumulate accel X/Y (NOT Z — see below)
    - Accumulate gyro X/Y/Z
    - Latch the latest quaternion
    - sample_count++
  - osDelay(1)

After loop:
  IF sample_count == 0:
    Identity defaults (offsets 0, baseline = unit quat); return HAL_ERROR
  ELSE:
    offset_accel_X = sum_X / N (same for Y)
    offset_accel_Z = 0  (EXPLICIT — see below)
    offset_gyro_X/Y/Z = sum / N
    baseline_quat_inv = (-qi, -qj, -qk, qr)  (pre-conjugated)
    calibrated = true
    return HAL_OK
```

Expected sample count: ~200 (at 100 Hz × 2 s).

## Why Z-accel offset is forced to zero

The Z accelerometer at rest reads the gravity vector (~9.81 m/s²). The team **explicitly does not subtract it**:

```c
/* Z-axis offset EXPLICITLY zeroed. Subtracting the boot-time mean
 * would cancel gravity (~9.81 m/s²) before the frame reaches the
 * Jetson EKF, which requires the raw gravity reading to estimate
 * pitch/roll. The X/Y accel offsets still kill chassis-tilt bias
 * because gravity projects onto Z when the car sits level. */
dev->calib.offset_accel_z = 0.0f;
```
(`bno085.c:419-424`)

The Jetson EKF needs gravity present in Z to:
- Estimate pitch/roll from accel direction.
- Cross-check the IMU's onboard fusion under EMI.

If Z were subtracted, the EKF would treat the chassis as having zero gravity → unable to recover pitch/roll under failed magnetometer.

## Race-free sampling

The calibration loop uses the same flag-OR-pin-LOW pattern as the main runtime loop:

```c
if (bno085_int_flag ||
    HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET) {
    bno085_int_flag = false;
    if (BNO085_GetData(dev) == HAL_OK && dev->data.data_valid) {
        /* accumulate */
    }
}
osDelay(1);
elapsed_ms++;
```
(`bno085.c:356-396`)

Critical: the flag is cleared BEFORE the call to `BNO085_GetData`, and the flag is also pre-cleared before entering the loop:

```c
dev->data.data_valid = false;
bno085_int_flag = false;  // pre-clear EXTI latch
```
(`bno085.c:351-354`)

Without pre-clear, any stale edge from before calibration starts would count as a valid sample.

## Pre-conjugated baseline

The captured quaternion is the **inverse** (= conjugate for unit quat) of the resting quaternion:

```c
dev->calib.baseline_quat_inv_i    = -qi;
dev->calib.baseline_quat_inv_j    = -qj;
dev->calib.baseline_quat_inv_k    = -qk;
dev->calib.baseline_quat_inv_real =  qr;
```
(`bno085.c:431-434`)

Storing the inverse means the hot path skips a negate every publish. Single Hamilton product instead of conjugate-then-multiply.

See `05_quaternion_pipeline.md` for the hot-path application.

## Failure case — sensor silent for 2 s

```c
if (sample_count == 0u) {
    /* Sensor silent for 2 s. Leave calibration as identity so the
     * publish path passes raw data through unchanged. */
    dev->calib.offset_accel_x = 0.0f;
    ...
    dev->calib.baseline_quat_inv_real = 1.0f;
    dev->calib.calibrated = false;
    return HAL_ERROR;
}
```
(`bno085.c:399-413`)

**Identity defaults applied; HAL_ERROR returned.** The caller (`freertos.c:266`) **discards the return value**. The system enters the main loop with bias-uncorrected published data. The `calibrated = false` flag is set but no consumer reads it.

This is a **real safety gap** (F-2 in the workspace audit, cross-referenced here as an embedded-side concern). Recommendation: light a fault LED on `HAL_ERROR` return + log loudly + consider refusing to enter the main loop.

## Auto-recovery does NOT re-run calibration

```c
} else if (++imu_err_count >= 50u) {
    imu_err_count = 0u;
    /* Auto-recovery: re-init and re-enable the same 9-DoF config.
     * Calibration is NOT re-run — the original power-up offsets stay
     * authoritative. Re-zeroing mid-run would let post-recovery
     * tilt/orientation leak into the published frame. */
    BNO085_Init(&bno085, &hi2c1);
    BNO085_EnableReport(&bno085, BNO085_REPORT_ROTATION_VECTOR, 10000);
    BNO085_EnableReport(&bno085, BNO085_REPORT_ACCELEROMETER,   10000);
    BNO085_EnableReport(&bno085, BNO085_REPORT_GYROSCOPE,       10000);
    bno085_int_flag = false;
}
```
(`freertos.c:315-326`)

This is correct: the boot-time offsets reflect the vehicle's actual rest orientation; re-zeroing during operation would treat whatever pose the vehicle is currently in as the new "rest" — corrupting the published yaw thereafter.

`BNO085_Init` is careful to NOT clear `dev->calib`:

```c
/* NOTE: dev->calib is intentionally NOT cleared here — the auto-recovery
 * path calls BNO085_Init() again mid-run and must keep the boot-time
 * offsets authoritative. */
```
(`bno085.c:267-272`)

## When calibration is invoked

In `StartDefaultTask`:

```
1. BNO085_Init                    (line 107)
2. Enable 3 reports               (lines 117-122)
3. ... bring up micro-ROS ...
4. TIM2 PWM Start + force neutral (lines 209-214)
5. Init IMU msg                   (lines 219-251)
6. BNO085_PerformStartupCalibration  (line 266)  ← BLOCKS for 2 s
7. Enter main loop                 (line 271)
```

Calibration runs **after** the micro-ROS session is up but **before** the main loop. Important: PWM is already at 1500 µs neutral (step 4) so the vehicle is physically still during calibration.

## Why 2 seconds

At 100 Hz, 2 s = 200 samples. Standard error of the mean for a Gaussian with σ=σ₀ is σ₀/√N. With 200 samples, the mean is ~14× more precise than a single sample. Plenty for boot-time bias estimation.

Longer would add boot delay without proportional benefit. Shorter would be too noisy.

## Recommendations (NOT MADE — review-only)

1. **Loud failure path on `HAL_ERROR`.** Light PA5 fast-flicker, log via micro-ROS diagnostic, or trap in `Error_Handler`-style hang. Currently silent.
2. **Save the resting quaternion magnitude as a diagnostic.** A `|q|²` close to 1.0 confirms a healthy quat; far from 1.0 hints at corruption during the calibration window.
3. **Reject calibration if sample variance is high.** If the vehicle was moved during the 2 s window, the bias estimate is wrong. Computing variance is ~free given the same accumulators.
4. **Persist calibration to STM32 flash** for warm restarts. Currently re-runs on every boot. Trade-off: flash wear, complexity.
