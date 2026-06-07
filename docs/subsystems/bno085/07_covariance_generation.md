# BNO085 — 07: Covariance Generation

The firmware publishes diagonal covariances on `sensor_msgs::Imu`. These are **constants** baked at boot, not runtime-derived from sensor accuracy fields.

## What gets set

```c
/* freertos.c:226-246 */
memset(imu_msg.orientation_covariance,         0, sizeof(imu_msg.orientation_covariance));
memset(imu_msg.angular_velocity_covariance,    0, sizeof(imu_msg.angular_velocity_covariance));
memset(imu_msg.linear_acceleration_covariance, 0, sizeof(imu_msg.linear_acceleration_covariance));

imu_msg.orientation_covariance[0]         = 0.001;    /* roll  (rad²) */
imu_msg.orientation_covariance[4]         = 0.001;    /* pitch (rad²) */
imu_msg.orientation_covariance[8]         = 0.01;     /* yaw   (rad², GRV drift) */

imu_msg.angular_velocity_covariance[0]    = 2.19e-6;  /* (rad/s)² */
imu_msg.angular_velocity_covariance[4]    = 2.19e-6;
imu_msg.angular_velocity_covariance[8]    = 2.19e-6;

imu_msg.linear_acceleration_covariance[0] = 0.004;    /* (m/s²)² */
imu_msg.linear_acceleration_covariance[4] = 0.004;
imu_msg.linear_acceleration_covariance[8] = 0.004;
```

Off-diagonal entries are zero. Only diagonal contribute.

## Why these specific values

| Channel | Diagonal | Justification (source comment) |
|---|---|---|
| Roll, pitch | 1e-3 rad² | "Roll/pitch stay tight (accel-gravity reference is unaffected by GRV)." Source comment was written before the team switched from GRV to RotVec, but the value is consistent — accel-gravity is unaffected by magnetometer health. |
| **Yaw** | **1e-2 rad²** | **Intentionally loose** — "yaw is bumped because GRV has no magnetometer reference and drifts ~0.5-2 deg/min under quiet conditions, faster under BLDC vibration." Comment still references GRV; after the switch to RotVec (mag-fused), this is a deliberate margin for mag corruption under EMI. |
| Gyro | 2.19e-6 (rad/s)² | BNO085 datasheet noise spec. NOT PROVEN exact derivation — likely Allan-variance-derived. |
| Accel | 4e-3 (m/s²)² | BNO085 datasheet noise spec. |

## The Jetson adaptive R mechanism

The Jetson EKF (`ekf_localization_node.cpp` in the workspace audit) consumes these covariances and scales the measurement R matrix per-tick:

$$R_{\mathrm{eff}} = \frac{R_{\mathrm{base}}}{\max(\mathrm{conf}_{\mathrm{IMU}}, \mathrm{MIN})}$$

where

$$\mathrm{conf}_{\mathrm{IMU}} = \exp\big(-(0.6\, \sigma_{\mathrm{orient}} + 0.3\, \sigma_{\mathrm{gyro}} + 0.1\, \sigma_{\mathrm{accel}})\big)$$

and each $\sigma_X = \sqrt{\mathrm{trace}(\Sigma_X)}$.

For the team's healthy IMU:
- $\sigma_{\mathrm{orient}} = \sqrt{0.001+0.001+0.01} = \sqrt{0.012} \approx 0.110$
- $\sigma_{\mathrm{gyro}}   = \sqrt{3 \times 2.19\text{e-}6} \approx 0.00256$
- $\sigma_{\mathrm{accel}}  = \sqrt{3 \times 0.004} \approx 0.110$

Weighted sigma ≈ 0.078. Confidence ≈ exp(-0.078) ≈ 0.925. The Jetson trusts the IMU at ~92.5 % at steady state.

If the firmware bumped any covariance, the confidence drops, and the EKF automatically trusts the IMU less. This is the central reason the covariance values matter as a contract with the Jetson side.

## Why constants, not runtime

The BNO085 emits an accuracy field per report (Q12 radians for the Rotation Vector). The team **doesn't propagate this into the published covariance** — they use baked constants.

**Trade-off:**
- ✅ Constants are deterministic and simple. The Jetson's adaptive R can rely on them being stable.
- ❌ A genuinely-degraded IMU sample would publish the same covariance as a healthy one. The Jetson can't tell.

A future improvement: scale the orientation covariance by `quat_accuracy / 0.1` (or similar) so a sample with high reported accuracy widens the published covariance proportionally. Currently NOT done.

## "Frame ID" choice

```c
rosidl_runtime_c__String__assign(&imu_msg.header.frame_id, "imu_link");
```
(`freertos.c:220`)

`"imu_link"` matches the static TF published by the autonomy bringup:

```python
tf_static_imu = Node(
    package='tf2_ros', executable='static_transform_publisher',
    arguments=['0', '0', '0.05', '0', '0', '0', 'base_link', 'imu_link'])
```
(workspace `race_day.launch.py:235-238`)

So the IMU's frame is 5 cm above `base_link`, no rotation. This is the physical mounting offset.

## Failure interactions

| Scenario | Firmware covariance | Jetson response |
|---|---|---|
| EMI corruption silently kept by quat sanity gate | Unchanged (constants) | Jetson trusts IMU at normal level despite a stale quat being held; mitigated by Jetson's hard gates (quat_norm_ok etc.) |
| Calibration silent failure (sample_count = 0) | Unchanged | Jetson trusts raw-bias data; bias accumulates uncorrected; drift estimator's lane correction is the only safety net |
| Auto-recovery in progress | Unchanged (firmware doesn't republish covariance) | Jetson sees normal cov; the stale-frame gate on the Jetson catches lack of fresh frames |

## Recommendations (NOT MADE)

1. **Add a per-frame "covariance multiplier"** derived from `quat_accuracy`. Minimal code change; gives the Jetson EKF a runtime signal.
2. **Add the BNO085's onboard `status` byte to a custom diagnostic topic.** The status byte includes the per-sample accuracy level (0-3). NOT PROVEN: whether the SH-2 report bytes 2-3 are currently being read; from the code, they appear to be discarded.
3. **Document the covariance contract with the Jetson** alongside the Jetson-side topic consumer so a future maintainer cannot accidentally change a value without coordinating.
