#ifndef BNO085_H
#define BNO085_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* I2C address — ADR pin tied LOW selects 0x4A. Shifted for HAL 7-bit API. */
#define BNO085_I2C_ADDR        (0x4A << 1)

/* SHTP channel definitions */
#define BNO085_CHANNEL_COMMAND        0
#define BNO085_CHANNEL_EXE            1
#define BNO085_CHANNEL_CONTROL        2
#define BNO085_CHANNEL_INPUT_SENSOR   3

/* Sensor report IDs (SH-2 Reference Manual, Table 6-1) */
#define BNO085_REPORT_ACCELEROMETER         0x01
#define BNO085_REPORT_GYROSCOPE             0x02
#define BNO085_REPORT_LINEAR_ACCELERATION   0x04
#define BNO085_REPORT_ROTATION_VECTOR       0x05
#define BNO085_REPORT_GAME_ROTATION_VECTOR  0x08

/* Control report IDs */
#define BNO085_REPORT_SET_FEATURE_COMMAND   0xFD
#define BNO085_REPORT_PRODUCT_ID_REQ        0xF9
#define BNO085_REPORT_PRODUCT_ID_RESP       0xF8
#define BNO085_REPORT_COMMAND_REQUEST       0xF2

/* SH-2 command IDs (carried as byte[2] of a 0xF2 Command Request) */
#define BNO085_CMD_SAVE_DCD                 0x06
#define BNO085_CMD_ME_CALIBRATION           0x07
#define BNO085_CMD_DCD_PERIOD_SAVE          0x09
#define BNO085_CMD_CLEAR_DCD                0x0B

/* SHTP transport sizes */
#define BNO085_SHTP_HEADER_SIZE 4
#define BNO085_MAX_PACKET_SIZE  128

typedef struct {
    float quat_i;
    float quat_j;
    float quat_k;
    float quat_real;
    float quat_accuracy;
    float lin_accel_x;
    float lin_accel_y;
    float lin_accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    bool  data_valid;
} BNO085_Data_t;

/* Power-up zero reference computed once by BNO085_PerformStartupCalibration.
 * Applied on the hot path as a single subtract + Hamilton product per tick. */
typedef struct {
    float offset_accel_x;
    float offset_accel_y;
    float offset_accel_z;
    float offset_gyro_x;
    float offset_gyro_y;
    float offset_gyro_z;
    /* Inverse of the baseline quaternion (pre-computed as conjugate, since
     * baseline is unit-norm). Used as the LEFT operand in the runtime
     * Hamilton product so published_quat = q_baseline_inv * q_raw. */
    float baseline_quat_inv_i;
    float baseline_quat_inv_j;
    float baseline_quat_inv_k;
    float baseline_quat_inv_real;
    bool  calibrated;
} BNO085_Calibration_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t tx_buf[BNO085_MAX_PACKET_SIZE];
    uint8_t rx_buf[BNO085_MAX_PACKET_SIZE + BNO085_SHTP_HEADER_SIZE];
    uint8_t sequence_number[6];
    uint8_t cmd_sequence;
    BNO085_Data_t data;
    BNO085_Calibration_t calib;
    bool initialized;
} BNO085_Handle_t;

HAL_StatusTypeDef BNO085_Init(BNO085_Handle_t *dev, I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef BNO085_EnableReport(BNO085_Handle_t *dev,
                                      uint8_t report_id,
                                      uint32_t interval_us);
HAL_StatusTypeDef BNO085_GetData(BNO085_Handle_t *dev);

/* Blocking 2-second power-up routine. Accumulates accel + gyro samples
 * (mean → permanent bias offsets) and captures the final stable
 * quaternion as a baseline. Pre-inverts the baseline so the runtime
 * path only needs one Hamilton product. Must be called AFTER reports
 * have been enabled and the bus has produced at least one packet. */
HAL_StatusTypeDef BNO085_PerformStartupCalibration(BNO085_Handle_t *dev);

/* Block (yield-friendly) until BNO085 INTN drops or timeout expires. */
bool BNO085_WaitForInt(uint32_t timeout_ms);

/* Calibration / DCD control */
HAL_StatusTypeDef BNO085_ConfigCalibration(BNO085_Handle_t *dev,
                                           uint8_t accel_en,
                                           uint8_t gyro_en,
                                           uint8_t mag_en);
HAL_StatusTypeDef BNO085_SetDCDAutoSave(BNO085_Handle_t *dev,
                                        uint32_t period_us);
HAL_StatusTypeDef BNO085_SaveDCD(BNO085_Handle_t *dev);
HAL_StatusTypeDef BNO085_DisableDynamicCalibration(BNO085_Handle_t *dev);

/* Hamilton product r = a * b. Inputs may alias the outputs safely
 * (temporaries are taken before any store). static inline so the hot
 * publish path pays zero call overhead. */
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

/* BNO085 INTN (PB4) interrupt latch. Defined in stm32f4xx_it.c, set by
 * HAL_GPIO_EXTI_Callback on falling edge, cleared by the consumer
 * IMMEDIATELY BEFORE the I2C read so a re-assert during the read is
 * captured on the next loop tick (race-free path documented in
 * freertos.c and BNO085_PerformStartupCalibration). */
extern volatile bool bno085_int_flag;

#endif /* BNO085_H */
