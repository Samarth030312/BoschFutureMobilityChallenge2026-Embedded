#include "bno085.h"
#include "cmsis_os.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* SHTP transport — polling, 2-stage read (golden baseline)           */
/* ------------------------------------------------------------------ */

static HAL_StatusTypeDef BNO085_ReadPacket(BNO085_Handle_t *dev)
{
    HAL_StatusTypeDef status;
    uint8_t header[BNO085_SHTP_HEADER_SIZE];

    status = HAL_I2C_Master_Receive(dev->hi2c, BNO085_I2C_ADDR,
                                    header, BNO085_SHTP_HEADER_SIZE, 100);
    if (status != HAL_OK)
        return status;

    uint16_t packet_len = (uint16_t)(header[0]) |
                          ((uint16_t)(header[1] & 0x7F) << 8);

    if (packet_len == 0 || packet_len == 0x7FFF)
        return HAL_ERROR;

    if (packet_len > sizeof(dev->rx_buf))
        packet_len = sizeof(dev->rx_buf);

    status = HAL_I2C_Master_Receive(dev->hi2c, BNO085_I2C_ADDR,
                                    dev->rx_buf, packet_len, 100);
    return status;
}

static HAL_StatusTypeDef BNO085_SendPacket(BNO085_Handle_t *dev,
                                           uint8_t channel,
                                           uint8_t *data,
                                           uint16_t data_len)
{
    uint16_t total_len = data_len + BNO085_SHTP_HEADER_SIZE;
    uint8_t  packet[BNO085_MAX_PACKET_SIZE];

    if (total_len > sizeof(packet))
        return HAL_ERROR;

    packet[0] = (uint8_t)(total_len & 0xFF);
    packet[1] = (uint8_t)((total_len >> 8) & 0x7F);
    packet[2] = channel;
    packet[3] = dev->sequence_number[channel]++;

    memcpy(&packet[BNO085_SHTP_HEADER_SIZE], data, data_len);

    return HAL_I2C_Master_Transmit(dev->hi2c, BNO085_I2C_ADDR,
                                   packet, total_len, 100);
}

/* ------------------------------------------------------------------ */
/* Q-point fixed-to-float                                              */
/* ------------------------------------------------------------------ */

static float BNO085_QToFloat(int16_t fixed, uint8_t q_point)
{
    return (float)fixed / (float)(1 << q_point);
}

/* ------------------------------------------------------------------ */
/* SHTP report ID constants — internal use (not all exposed in header) */
/* ------------------------------------------------------------------ */
#define BNO085_SH2_TIMESTAMP_REBASE     0xFAu  /* delta-of-delta marker */
#define BNO085_SH2_BASE_TIMESTAMP_REF   0xFBu  /* base timestamp marker */

/* ------------------------------------------------------------------ */
/* Batched SH-2 input report parser (Channel 3)                        */
/* ------------------------------------------------------------------ */
/*
 * SHTP Channel 3 payload structure when multiple reports are enabled
 * at the same rate (canonical BNO085 behavior at 100 Hz):
 *
 *   [SHTP HEADER (4 B)] ─── packet_len in header includes these 4 B
 *   [0xFB Base Timebase Ref     (5 B)]   <─ payload offset 0
 *   [0x01 Accelerometer report  (10 B)]
 *   [0x02 Gyroscope Calibrated  (10 B)]
 *   [0x08 Game Rotation Vector  (12 B)]
 *
 * Worst-case batched cargo at this config: 5 + 10 + 10 + 12 = 37 B.
 * The 128-byte rx_buf has overwhelming headroom.
 *
 * The OLD parser read `report_id = payload[5]` exactly once, which
 * worked only when the BNO085 happened to emit a SINGLE rotation
 * report per packet. As soon as Accel + Gyro + Quat are batched
 * together (the common case), the OLD parser captured only the first
 * non-timebase report (Accel) and silently dropped the others ─
 * causing severe Gyro/Quat frame loss.
 *
 * The NEW parser walks the cargo using a while-loop dispatch on the
 * per-report ID byte at each offset, dynamically advancing by each
 * report's fixed SH-2 length. It handles all four SH-2 reports we use
 * + both timebase markers. Unknown reports cause a safe abort so we
 * never run off the buffer.
 */

/* SH-2 fixed report lengths (Reference Manual §6). 0 = unknown. */
static uint8_t BNO085_ReportLen(uint8_t report_id)
{
    switch (report_id) {
        case BNO085_SH2_TIMESTAMP_REBASE:
        case BNO085_SH2_BASE_TIMESTAMP_REF:    return  5;
        case BNO085_REPORT_ACCELEROMETER:
        case BNO085_REPORT_GYROSCOPE:
        case BNO085_REPORT_LINEAR_ACCELERATION:return 10;
        case BNO085_REPORT_GAME_ROTATION_VECTOR:return 12;
        case BNO085_REPORT_ROTATION_VECTOR:    return 14;
        default:                               return  0;
    }
}

static void BNO085_ParseInputReport(BNO085_Handle_t *dev)
{
    uint8_t *payload = &dev->rx_buf[BNO085_SHTP_HEADER_SIZE];

    /* Recompute cargo length from the SHTP header. packet_len is the
     * TOTAL packet size including the 4-byte header itself. */
    uint16_t packet_len = (uint16_t)dev->rx_buf[0] |
                          ((uint16_t)(dev->rx_buf[1] & 0x7F) << 8);
    if (packet_len < BNO085_SHTP_HEADER_SIZE) return;

    uint16_t cargo_len = packet_len - BNO085_SHTP_HEADER_SIZE;
    if (cargo_len > BNO085_MAX_PACKET_SIZE) return;   /* defensive */

    bool any_parsed = false;
    uint16_t offset = 0;

    while (offset < cargo_len)
    {
        uint8_t report_id  = payload[offset];
        uint8_t report_len = BNO085_ReportLen(report_id);

        /* Unknown report ID — cannot determine length, must abort to
         * stay in-bounds. Any reports parsed so far still take effect. */
        if (report_len == 0) {
            break;
        }

        /* Bounds check before reading the report body. */
        if ((uint16_t)(offset + report_len) > cargo_len) {
            break;
        }

        uint8_t *r = &payload[offset];   /* pointer to current report */

        switch (report_id)
        {
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

            case BNO085_REPORT_GYROSCOPE: {
                int16_t gx = (int16_t)((uint16_t)r[5] << 8 | r[4]);
                int16_t gy = (int16_t)((uint16_t)r[7] << 8 | r[6]);
                int16_t gz = (int16_t)((uint16_t)r[9] << 8 | r[8]);
                dev->data.gyro_x = BNO085_QToFloat(gx, 9);
                dev->data.gyro_y = BNO085_QToFloat(gy, 9);
                dev->data.gyro_z = BNO085_QToFloat(gz, 9);
                any_parsed = true;
                break;
            }

            case BNO085_REPORT_GAME_ROTATION_VECTOR: {
                /* GRV layout: header(4) + i,j,k,real (4 × int16) = 12 B.
                 * No magnetometer-dependent accuracy field. */
                int16_t qi = (int16_t)((uint16_t)r[5]  << 8 | r[4]);
                int16_t qj = (int16_t)((uint16_t)r[7]  << 8 | r[6]);
                int16_t qk = (int16_t)((uint16_t)r[9]  << 8 | r[8]);
                int16_t qr = (int16_t)((uint16_t)r[11] << 8 | r[10]);

                float fi = BNO085_QToFloat(qi, 14);
                float fj = BNO085_QToFloat(qj, 14);
                float fk = BNO085_QToFloat(qk, 14);
                float fr = BNO085_QToFloat(qr, 14);

                /* Retained quaternion sanity gate: BNO085 GRV is
                 * unit-magnitude by hardware. |q|^2 wildly off-unit
                 * means I2C cargo corruption (EMI under BLDC). Reject
                 * silently — last good quaternion stays in dev->data,
                 * other reports in this same batch are still accepted. */
                float mag2 = fi*fi + fj*fj + fk*fk + fr*fr;
                if (mag2 >= 0.9f && mag2 <= 1.1f) {
                    dev->data.quat_i        = fi;
                    dev->data.quat_j        = fj;
                    dev->data.quat_k        = fk;
                    dev->data.quat_real     = fr;
                    /* GRV has no built-in accuracy estimate — clamp to 0. */
                    dev->data.quat_accuracy = 0.0f;
                    any_parsed = true;
                }
                break;
            }

            case BNO085_REPORT_ROTATION_VECTOR: {
                /* 9-DoF Rotation Vector (0x05): SH-2 layout is
                 * header(4) + i,j,k,real (4 × int16 @ Q14) + accuracy
                 * (int16 @ Q12, radians) = 14 B. Magnetometer-fused, so
                 * yaw has an absolute reference and we get a per-frame
                 * heading-accuracy estimate the EKF can consume. */
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

            case BNO085_SH2_TIMESTAMP_REBASE:
            case BNO085_SH2_BASE_TIMESTAMP_REF:
                /* Timebase markers: advance only, no fields to extract. */
                break;

            default:
                /* Unreachable — ReportLen() returned 0 above. */
                break;
        }

        offset = (uint16_t)(offset + report_len);
    }

    /* data_valid is the publish trigger consumed by the freertos.c gate.
     * Setting it once per BATCHED packet (rather than per report) means
     * each ROS publish carries the freshest accel + gyro + quat together. */
    if (any_parsed) {
        dev->data.data_valid = true;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

HAL_StatusTypeDef BNO085_Init(BNO085_Handle_t *dev, I2C_HandleTypeDef *hi2c)
{
    dev->hi2c        = hi2c;
    dev->initialized = false;

    memset(dev->sequence_number, 0, sizeof(dev->sequence_number));
    memset(&dev->data, 0, sizeof(dev->data));

    /* Identity-quaternion default for live readings. NOTE: dev->calib
     * is intentionally NOT cleared here — the auto-recovery path calls
     * BNO085_Init() again mid-run and must keep the boot-time offsets
     * authoritative. calib is owned exclusively by
     * BNO085_PerformStartupCalibration(); first-boot identity defaults
     * come from C zero-init of the static handle. */
    dev->data.quat_real = 1.0f;

    osDelay(100);

    /* Flush startup packets (SHTP Advertisement, Reset Announce, etc.) */
    for (int i = 0; i < 5; i++) {
        if (BNO085_ReadPacket(dev) != HAL_OK)
            osDelay(50);
        else
            osDelay(10);
    }

    dev->initialized = true;
    return HAL_OK;
}

HAL_StatusTypeDef BNO085_EnableReport(BNO085_Handle_t *dev,
                                      uint8_t report_id,
                                      uint32_t interval_us)
{
    uint8_t cmd[17];
    memset(cmd, 0, sizeof(cmd));

    cmd[0] = BNO085_REPORT_SET_FEATURE_COMMAND;
    cmd[1] = report_id;

    cmd[5] = (uint8_t)(interval_us & 0xFF);
    cmd[6] = (uint8_t)((interval_us >> 8) & 0xFF);
    cmd[7] = (uint8_t)((interval_us >> 16) & 0xFF);
    cmd[8] = (uint8_t)((interval_us >> 24) & 0xFF);

    return BNO085_SendPacket(dev, BNO085_CHANNEL_CONTROL, cmd, sizeof(cmd));
}

HAL_StatusTypeDef BNO085_GetData(BNO085_Handle_t *dev)
{
    HAL_StatusTypeDef status = BNO085_ReadPacket(dev);
    if (status != HAL_OK)
        return status;

    if (dev->rx_buf[2] == BNO085_CHANNEL_INPUT_SENSOR)
        BNO085_ParseInputReport(dev);

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Power-up zeroing                                                    */
/* ------------------------------------------------------------------ */
/*
 * The chassis sits with a 1–2 degree static tilt that the accelerometer
 * picks up as a constant ~0.20 m/s^2 bias, and the gyro carries a few
 * mdeg/s of bias-on-startup. Both poison any downstream integration
 * (velocity estimate, yaw-rate-integrated heading) before vision can
 * relocalize. We pin them down once at boot with a 2-second mean and
 * keep the math out of the hot path.
 *
 * The quaternion baseline serves the same role for orientation: we
 * capture the resting quaternion, pre-invert it (unit-norm → conjugate),
 * and at runtime apply q_baseline_inv * q_raw so the car always boots
 * up facing [0, 0, 0, 1] relative to wherever it was physically placed.
 *
 * Loop cadence matches the running main loop (osDelay(1) ≈ 1 kHz poll
 * vs. ~100 Hz sensor output), so we expect ~200 valid samples in the
 * 2-second window — plenty to average out per-sample noise.
 */
HAL_StatusTypeDef BNO085_PerformStartupCalibration(BNO085_Handle_t *dev)
{
    float    sum_ax = 0.0f, sum_ay = 0.0f;
    float    sum_gx = 0.0f, sum_gy = 0.0f, sum_gz = 0.0f;
    uint32_t sample_count = 0u;

    float qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;

    const uint32_t CALIBRATION_MS = 2000u;
    uint32_t elapsed_ms = 0u;

    /* Pre-clear so we never bank a stale frame from before this call. */
    dev->data.data_valid = false;
    /* Pre-clear the EXTI latch too — any pre-call edge must NOT count
     * against the calibration window's race-free gate. */
    bno085_int_flag = false;

    while (elapsed_ms < CALIBRATION_MS) {
        /* Race-condition-free SHTP read gate.
         *
         * The BNO085 INTN pin is open-drain active-LOW. It de-asserts
         * (HIGH) only after the host has drained the pending SHTP
         * payload. If a second packet becomes ready while the I2C bus
         * is in the middle of reading the first, INTN stays LOW
         * continuously — and the falling-edge EXTI captures NO new
         * edge for the second packet. The flag-only gate would then
         * stall here for the full 2 s window.
         *
         * Combining the flag with a direct read of PB4 ensures we
         * still trigger an SHTP read whenever the line is physically
         * LOW (a packet IS pending), regardless of whether the EXTI
         * already fired for it. The flag is cleared BEFORE the read
         * so a mid-read re-assert produces a fresh edge that the
         * ISR latches for the next iteration. */
        if (bno085_int_flag ||
            HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET) {
            bno085_int_flag = false;
            if (BNO085_GetData(dev) == HAL_OK && dev->data.data_valid) {
                /* Accumulate XY only — Z is EXPLICITLY excluded from
                 * averaging so the Jetson EKF receives the raw 9.81 m/s²
                 * gravity vector through Z, un-subtracted. */
                sum_ax += dev->data.lin_accel_x;
                sum_ay += dev->data.lin_accel_y;
                sum_gx += dev->data.gyro_x;
                sum_gy += dev->data.gyro_y;
                sum_gz += dev->data.gyro_z;
                /* Latch the most recent quaternion — it becomes the
                 * baseline at loop exit. */
                qi = dev->data.quat_i;
                qj = dev->data.quat_j;
                qk = dev->data.quat_k;
                qr = dev->data.quat_real;
                sample_count++;
                dev->data.data_valid = false;
            }
        }
        osDelay(1);
        elapsed_ms++;
    }

    if (sample_count == 0u) {
        /* Sensor silent for 2 s. Leave calibration as identity so the
         * publish path passes raw data through unchanged. */
        dev->calib.offset_accel_x = 0.0f;
        dev->calib.offset_accel_y = 0.0f;
        dev->calib.offset_accel_z = 0.0f;
        dev->calib.offset_gyro_x  = 0.0f;
        dev->calib.offset_gyro_y  = 0.0f;
        dev->calib.offset_gyro_z  = 0.0f;
        dev->calib.baseline_quat_inv_i    = 0.0f;
        dev->calib.baseline_quat_inv_j    = 0.0f;
        dev->calib.baseline_quat_inv_k    = 0.0f;
        dev->calib.baseline_quat_inv_real = 1.0f;
        dev->calib.calibrated = false;
        return HAL_ERROR;
    }

    float inv_n = 1.0f / (float)sample_count;
    dev->calib.offset_accel_x = sum_ax * inv_n;
    dev->calib.offset_accel_y = sum_ay * inv_n;
    /* Z-axis offset EXPLICITLY zeroed. Subtracting the boot-time mean
     * would cancel gravity (~9.81 m/s²) before the frame reaches the
     * Jetson EKF, which requires the raw gravity reading to estimate
     * pitch/roll. The X/Y accel offsets still kill chassis-tilt bias
     * because gravity projects onto Z when the car sits level. */
    dev->calib.offset_accel_z = 0.0f;
    dev->calib.offset_gyro_x  = sum_gx * inv_n;
    dev->calib.offset_gyro_y  = sum_gy * inv_n;
    dev->calib.offset_gyro_z  = sum_gz * inv_n;

    /* Unit quaternion inverse = conjugate. Storing the inverse (not the
     * baseline itself) means the hot path skips a negate every tick. */
    dev->calib.baseline_quat_inv_i    = -qi;
    dev->calib.baseline_quat_inv_j    = -qj;
    dev->calib.baseline_quat_inv_k    = -qk;
    dev->calib.baseline_quat_inv_real =  qr;

    dev->calib.calibrated = true;
    return HAL_OK;
}
