# BNO085 — 05: Quaternion Pipeline

This document covers Q-point math, the Hamilton product, the sanity gate, the baseline conjugation, and the published-frame normalisation.

## From bytes to floats — Q-point conversion

The BNO085 reports quaternion components as signed 16-bit fixed-point integers in **Q14** format:

$$\text{float value} = \frac{\text{int16 value}}{2^{14}} = \frac{\text{int16}}{16384}$$

Range: ±32767/16384 ≈ ±1.999.

Implementation:

```c
static float BNO085_QToFloat(int16_t fixed, uint8_t q_point)
{
    return (float)fixed / (float)(1 << q_point);
}
```
(`bno085.c:59-62`)

The `q_point` parameter is a runtime variable; the compiler folds it for the specific call sites because each caller passes a constant. For Rotation Vector quaternion components: `q_point = 14`. For the heading accuracy: `q_point = 12`.

Single hardware FMUL on Cortex-M4F. ~1 cycle.

## Quaternion convention

The firmware uses the **scalar-last (i, j, k, real)** convention:

```c
typedef struct {
    ...
    float quat_i;       // x
    float quat_j;       // y
    float quat_k;       // z
    float quat_real;    // w (scalar)
    float quat_accuracy;
    ...
} BNO085_Data_t;
```
(`bno085.h:40-52`)

This matches ROS (`geometry_msgs::Quaternion` order: x, y, z, w). When the firmware publishes to ROS:

```c
imu_msg.orientation.x = (double)q_pub_i;
imu_msg.orientation.y = (double)q_pub_j;
imu_msg.orientation.z = (double)q_pub_k;
imu_msg.orientation.w = (double)q_pub_r;
```
(`freertos.c:385-388`)

Direct mapping.

## Hamilton product

For quaternion multiplication `r = a × b`:

$$
\begin{aligned}
r_i &= a_r b_i + a_i b_r + a_j b_k - a_k b_j \\
r_j &= a_r b_j - a_i b_k + a_j b_r + a_k b_i \\
r_k &= a_r b_k + a_i b_j - a_j b_i + a_k b_r \\
r_r &= a_r b_r - a_i b_i - a_j b_j - a_k b_k
\end{aligned}
$$

Implementation (inlined for the hot path):

```c
static inline void BNO085_QuatMultiply(float ai, float aj, float ak, float ar,
                                       float bi, float bj, float bk, float br,
                                       float *ri, float *rj, float *rk, float *rr)
{
    float ti = ar*bi + ai*br + aj*bk - ak*bj;
    float tj = ar*bj - ai*bk + aj*br + ak*bi;
    float tk = ar*bk + ai*bj - aj*bi + ak*br;
    float tr = ar*br - ai*bi - aj*bj - ak*bk;
    *ri = ti; *rj = tj; *rk = tk; *rr = tr;
}
```
(`bno085.h:114-123`)

**Why temporaries.** The inputs may alias the outputs (i.e., a caller can pass the same pointer as input and output). Computing all four results into local temps before storing prevents corrupting the inputs mid-calculation.

**Cost:** 16 multiplies + 12 add/subs = 28 FPU ops × 1 cycle = ~28 cycles at 84 MHz = **~333 ns per multiply**. Negligible.

## Quaternion sanity gate

The team rejects quaternions with `|q|² ∉ [0.9, 1.1]`:

```c
float mag2 = fi*fi + fj*fj + fk*fk + fr*fr;
if (mag2 >= 0.9f && mag2 <= 1.1f) {
    /* accept */
} /* else: silently discard, keep last good */
```
(`bno085.c:192-201` for GRV, `222-230` for Rotation Vector)

**Why this gate exists.** The BNO085's internal fusion produces unit quaternions (|q| = 1 within float precision). An |q|² far from 1.0 means I²C cargo corruption — most often from EMI from the BLDC motor. The team observed this in field testing.

**Why the threshold is asymmetric.** `[0.9, 1.1]` accepts `|q|² up to ±5 % off` — comfortably wider than float-precision drift but narrow enough to catch a flipped bit (which would push `|q|²` far outside this range).

**Effect of rejection.** The last good quaternion stays in `dev->data`. Subsequent reports in the same batch (accel, gyro) are still parsed normally — they're independent.

## Boot calibration's baseline quaternion

After 2 s of sampling, the team captures the **resting quaternion** as `(qi, qj, qk, qr)` and **pre-inverts** it:

```c
/* Unit quaternion inverse = conjugate. Storing the inverse (not the
 * baseline itself) means the hot path skips a negate every tick. */
dev->calib.baseline_quat_inv_i    = -qi;
dev->calib.baseline_quat_inv_j    = -qj;
dev->calib.baseline_quat_inv_k    = -qk;
dev->calib.baseline_quat_inv_real =  qr;
```
(`bno085.c:431-434`)

**Math.** The inverse of a unit quaternion equals its conjugate: `q⁻¹ = (-q_i, -q_j, -q_k, q_real)`.

Pre-inverting at calibration time means the hot path needs only **one Hamilton product** per publish — not a conjugate-then-multiply.

## Hot-path application

Every publish:

```c
BNO085_QuatMultiply(
    bno085.calib.baseline_quat_inv_i,
    bno085.calib.baseline_quat_inv_j,
    bno085.calib.baseline_quat_inv_k,
    bno085.calib.baseline_quat_inv_real,
    bno085.data.quat_i,
    bno085.data.quat_j,
    bno085.data.quat_k,
    bno085.data.quat_real,
    &q_pub_i, &q_pub_j, &q_pub_k, &q_pub_r);
```
(`freertos.c:351-360`)

This computes `q_pub = q_baseline_inv ⊗ q_raw`. Effect: when the vehicle is at the calibration rest pose, `q_raw == q_baseline`, so `q_pub == q_baseline_inv × q_baseline = identity = (0, 0, 0, 1)`.

In other words: the vehicle's published yaw is **zero at the boot rest position**, increasing as the vehicle yaws away from rest.

This is the team's coordinate-frame convention. The Jetson EKF treats yaw as world-frame, with zero at boot pose. As long as the boot pose is consistent (vehicle pointed forward on the track), this aligns with the BFMC mission's coordinate system.

## Unit-norm enforcement before publish

After the Hamilton product, the result may be slightly off-unit due to floating drift in the product:

```c
float q_mag = sqrtf(q_pub_i * q_pub_i +
                    q_pub_j * q_pub_j +
                    q_pub_k * q_pub_k +
                    q_pub_r * q_pub_r);
if (q_mag > 0.000001f) {
    float inv_mag = 1.0f / q_mag;
    q_pub_i *= inv_mag;
    q_pub_j *= inv_mag;
    q_pub_k *= inv_mag;
    q_pub_r *= inv_mag;
}
```
(`freertos.c:373-383`)

**Why this is necessary** (per the source comment, `freertos.c:362-372`):

> 32-bit float Hamilton products accumulate ~1e-7 per multiply, so the output magnitude drifts a few ULPs off 1.0. Even w = 1.0 + 2e-7 makes the Jetson EKF's acos(w) return NaN, which cascades through the whole filter. Normalize defensively. Single sqrtf + four multiplies; the FPU eats this in ~12 cycles.

**The 1e-6 floor on `q_mag`** is a NaN guard against the (theoretical) case of both inputs being zero — which can't happen in practice but defends against degenerate calibration.

Cost: 1 sqrtf + 4 multiplies + 1 reciprocal = ~10-15 cycles. ~150 ns.

## Complete quaternion flow

```mermaid
flowchart LR
    BNOQ[BNO085 RotVec report] --> Q14[Q14 int16<br/>4 components + Q12 accuracy]
    Q14 --> Q2F[BNO085_QToFloat × 4]
    Q2F --> MAG2["|q|² = i² + j² + k² + w²"]
    MAG2 --> GATE{0.9 ≤ |q|² ≤ 1.1?}
    GATE -- "No" --> KEEP[Keep previous valid q]
    GATE -- "Yes" --> STORE[Store as data.quat_*]
    STORE --> HAM[Hamilton:<br/>q_pub = baseline_inv ⊗ q_raw]
    HAM --> NORM["q_pub /= |q_pub|"]
    NORM --> PUB["Publish in imu_msg.orientation<br/>(x,y,z,w)"]
```

## Failure modes (quaternion-specific)

| Failure | Detection | Recovery |
|---|---|---|
| Quaternion `|q|²` out of range | sanity gate | last good quat retained |
| Both `q_pub` and `q_mag` are zero | `if (q_mag > 1e-6f)` guards division | published quat = (0,0,0,0) — invalid, but the EKF will reject via its own quat_norm_ok gate (`ekf_localization_node.cpp:250-251`) |
| Calibration's baseline_quat_inv = identity (sample_count == 0) | `bno085.c:408-411` | hot path equivalent to passthrough — no rotation applied |

## Why the FPU matters

Each publish does:
- 5 × Q-point conversions (~5 cycles)
- 1 × sanity gate (~10 cycles)
- 28 × Hamilton ops (~28 cycles)
- 1 × sqrt + reciprocal + 4 mul (~15 cycles)

Total: ~60 cycles of FPU work per publish × 100 Hz = **6000 cycles/s ≈ 0.007 %** of CPU. With software floats, this would balloon to ~10×-50× — still small in absolute terms but visible.

The FPU's main benefit isn't speed (the math is small), it's **correctness**: software floats follow IEEE 754 but differ in NaN/Inf propagation edge cases between toolchains.
