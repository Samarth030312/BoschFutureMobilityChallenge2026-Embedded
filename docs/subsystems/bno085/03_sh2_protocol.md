# BNO085 — 03: SH-2 Protocol & Multi-Report Walker

## What SH-2 is

SH-2 is the higher-level protocol that lives **inside SHTP cargo on Channel 3** (Input Sensor Reports). Each SH-2 report is identified by a single-byte report ID, followed by a fixed number of payload bytes.

The team's parser walks the cargo, identifying each report by ID, looking up its length from a table, and dispatching to a per-report handler.

## Report IDs in use

| ID | Constant | Name | Length (bytes) | Fixed-point Q | Used |
|---|---|---|---|---|---|
| 0x01 | `BNO085_REPORT_ACCELEROMETER` | Accelerometer (calibrated, m/s²) | 10 | Q8 | YES |
| 0x02 | `BNO085_REPORT_GYROSCOPE` | Gyroscope (calibrated, rad/s) | 10 | Q9 | YES |
| 0x04 | `BNO085_REPORT_LINEAR_ACCELERATION` | Linear acceleration (gravity-removed) | 10 | Q8 | parser supports, not enabled |
| 0x05 | `BNO085_REPORT_ROTATION_VECTOR` | 9-DoF Rotation Vector (mag-fused, with accuracy) | 14 | Q14 (components) + Q12 (accuracy) | YES |
| 0x08 | `BNO085_REPORT_GAME_ROTATION_VECTOR` | 6-DoF Rotation Vector (no mag, no accuracy) | 12 | Q14 | parser supports, not enabled |
| 0xFA | `BNO085_SH2_TIMESTAMP_REBASE` | delta-of-delta timestamp marker | 5 | — | skipped |
| 0xFB | `BNO085_SH2_BASE_TIMESTAMP_REF` | base timestamp marker | 5 | — | skipped |

(Constants in `bno085.h:18-22, 67-68`.)

## Report length table

```c
static uint8_t BNO085_ReportLen(uint8_t report_id) {
    switch (report_id) {
        case BNO085_SH2_TIMESTAMP_REBASE:
        case BNO085_SH2_BASE_TIMESTAMP_REF:    return 5;
        case BNO085_REPORT_ACCELEROMETER:
        case BNO085_REPORT_GYROSCOPE:
        case BNO085_REPORT_LINEAR_ACCELERATION:return 10;
        case BNO085_REPORT_GAME_ROTATION_VECTOR:return 12;
        case BNO085_REPORT_ROTATION_VECTOR:    return 14;
        default:                               return 0;
    }
}
```
(`bno085.c:101-113`)

Return value 0 means "unknown" — the walker breaks out of the cargo-walk loop on encountering an unknown ID (cannot safely continue without knowing the offset to the next report).

## Multi-report walker

The historical bug the walker fixes (per the source comment, `bno085.c:73-98`):

> The OLD parser read `report_id = payload[5]` exactly once, which worked only when the BNO085 happened to emit a SINGLE rotation report per packet. As soon as Accel + Gyro + Quat are batched together (the common case), the OLD parser captured only the first non-timebase report (Accel) and silently dropped the others — causing severe Gyro/Quat frame loss.

The new walker:

```c
uint16_t cargo_len = packet_len - BNO085_SHTP_HEADER_SIZE;
bool any_parsed = false;
uint16_t offset = 0;

while (offset < cargo_len) {
    uint8_t report_id  = payload[offset];
    uint8_t report_len = BNO085_ReportLen(report_id);

    if (report_len == 0) break;                       // unknown ID
    if ((offset + report_len) > cargo_len) break;      // bounds check

    uint8_t *r = &payload[offset];   // pointer to current report

    switch (report_id) {
        case BNO085_REPORT_ACCELEROMETER:
        case BNO085_REPORT_LINEAR_ACCELERATION:
            /* parse 10-byte accel report */
            ...
            any_parsed = true;
            break;
        case BNO085_REPORT_GYROSCOPE:
            /* parse 10-byte gyro report */
            ...
            any_parsed = true;
            break;
        case BNO085_REPORT_GAME_ROTATION_VECTOR:
            /* parse 12-byte 6-DoF quat (no accuracy) */
            ...
            break;
        case BNO085_REPORT_ROTATION_VECTOR:
            /* parse 14-byte 9-DoF quat (with accuracy) */
            ...
            break;
        case BNO085_SH2_TIMESTAMP_REBASE:
        case BNO085_SH2_BASE_TIMESTAMP_REF:
            /* skip — markers only */
            break;
        default: break;
    }
    offset = (uint16_t)(offset + report_len);
}

if (any_parsed) dev->data.data_valid = true;
```
(`bno085.c:115-253`)

Key properties:

- **Bounds-safe.** `(offset + report_len) > cargo_len` check before reading the report body. An adversarial packet cannot run the parser off the end.
- **Unknown-ID break.** Breaks out (rather than incrementing by 1 and re-trying). Avoids skip-and-pray on a partial corruption.
- **`data_valid = true` set ONCE per batched packet.** Not per-report. So the publish path reads accel + gyro + quat **together** — temporally consistent.

## Typical SH-2 cargo (at the configured rate)

With Rotation Vector + Accelerometer + Gyroscope all at 100 Hz interval, a typical Channel-3 packet contains:

```
[0xFB Base Timebase Ref     (5 B)]   ← offset 0
[0x01 Accelerometer report  (10 B)]
[0x02 Gyroscope Calibrated  (10 B)]
[0x05 Rotation Vector       (14 B)]
```

Total cargo: 39 B. Plus 4 B SHTP header: 43 B packet. Comfortably under 128 B buffer.

(The pre-2026 configuration used Game Rotation Vector 0x08 (12 B) instead of Rotation Vector 0x05 (14 B); cargo was 37 B. The team switched to 0x05 for absolute heading reference — see `freertos.c:108-117`.)

## Reading the accelerometer report (example walkthrough)

```c
case BNO085_REPORT_ACCELEROMETER:
case BNO085_REPORT_LINEAR_ACCELERATION: {
    int16_t ax = (int16_t)((uint16_t)r[5] << 8 | r[4]);
    int16_t ay = (int16_t)((uint16_t)r[7] << 8 | r[6]);
    int16_t az = (int16_t)((uint16_t)r[9] << 8 | r[8]);
    dev->data.lin_accel_x = BNO085_QToFloat(ax, 8);
    dev->data.lin_accel_y = BNO085_QToFloat(ay, 8);
    dev->data.lin_accel_z = BNO085_QToFloat(az, 8);
    any_parsed = true;
    break;
}
```
(`bno085.c:151-161`)

The report layout per SH-2 spec:
- Byte 0: report ID (0x01)
- Byte 1: sequence number
- Byte 2-3: status (lower 2 bits = accuracy level; upper 6 bits = delay)
- Byte 4-5: X (int16 LE, Q8 m/s²)
- Byte 6-7: Y (int16 LE, Q8)
- Byte 8-9: Z (int16 LE, Q8)

The team reads bytes 4-9 only — discards sequence + status (status accuracy is implicitly handled by the published covariance constants).

## Reading the Rotation Vector report

```c
case BNO085_REPORT_ROTATION_VECTOR: {
    int16_t qi = (int16_t)((uint16_t)r[5]  << 8 | r[4]);
    int16_t qj = (int16_t)((uint16_t)r[7]  << 8 | r[6]);
    int16_t qk = (int16_t)((uint16_t)r[9]  << 8 | r[8]);
    int16_t qr = (int16_t)((uint16_t)r[11] << 8 | r[10]);
    int16_t qa = (int16_t)((uint16_t)r[13] << 8 | r[12]);

    float fi = BNO085_QToFloat(qi, 14);
    float fj = BNO085_QToFloat(qj, 14);
    float fk = BNO085_QToFloat(qk, 14);
    float fr = BNO085_QToFloat(qr, 14);

    float mag2 = fi*fi + fj*fj + fk*fk + fr*fr;
    if (mag2 >= 0.9f && mag2 <= 1.1f) {
        dev->data.quat_i        = fi;
        dev->data.quat_j        = fj;
        dev->data.quat_k        = fk;
        dev->data.quat_real     = fr;
        dev->data.quat_accuracy = BNO085_QToFloat(qa, 12);
        any_parsed = true;
    }
    break;
}
```
(`bno085.c:205-232`)

Layout:
- Byte 0: report ID (0x05)
- Byte 1-3: sequence + status
- Byte 4-5: i (int16, Q14)
- Byte 6-7: j (Q14)
- Byte 8-9: k (Q14)
- Byte 10-11: real / w (Q14)
- Byte 12-13: accuracy (Q12, radians)

The team computes `|q|² = i² + j² + k² + w²` and rejects if outside `[0.9, 1.1]`. See `05_quaternion_pipeline.md` for the rationale.

## Endianness

SH-2 is **little-endian** within each multi-byte field:

```c
int16_t ax = (int16_t)((uint16_t)r[5] << 8 | r[4]);
```

LSB at lower offset. The `(uint16_t)` cast before the shift is critical — without it, signed-extension of `r[5]` to `int` would corrupt negative values.

## Timestamp markers

`0xFA` and `0xFB` are SH-2 timing-related markers. They have 5-byte payloads but no value the team needs (the firmware uses its own wall clock via `rmw_uros_epoch_nanos`). The walker just advances past them.

If a future maintainer wants to align IMU sample timestamps with the BNO085's internal clock, the 0xFB base timestamp ref + 0xFA delta-of-delta sequence would be the place to start.
