/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <uxr/client/transport.h>
#include <rmw_microxrcedds_c/config.h>
#include <rmw_microros/rmw_microros.h>
#include <sensor_msgs/msg/imu.h>
#include <bfmc_msgs/msg/control_command.h>
#include <rosidl_runtime_c/string_functions.h>

#include <string.h>
#include <math.h>

#include "usart.h"
#include "i2c.h"
#include "tim.h"
#include "bno085.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static BNO085_Handle_t bno085;
static bfmc_msgs__msg__ControlCommand target_cmd;
static volatile int64_t last_cmd_time_ns = 0;
/* USER CODE END Variables */
/* Definitions for microROSTask */
osThreadId_t microROSTaskHandle;
const osThreadAttr_t microROSTask_attributes = {
  .name = "microROSTask",
  .stack_size = 3000 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
bool   cubemx_transport_open(struct uxrCustomTransport *transport);
bool   cubemx_transport_close(struct uxrCustomTransport *transport);
size_t cubemx_transport_write(struct uxrCustomTransport *transport, const uint8_t *buf, size_t len, uint8_t *err);
size_t cubemx_transport_read(struct uxrCustomTransport *transport, uint8_t *buf, size_t len, int timeout, uint8_t *err);

void *microros_allocate(size_t size, void *state);
void  microros_deallocate(void *pointer, void *state);
void *microros_reallocate(void *pointer, size_t size, void *state);
void *microros_zero_allocate(size_t number_of_elements, size_t size_of_element, void *state);

static void control_cmd_callback(const void *msgin);

static uint32_t steer_angle_to_pwm(float angle_rad)
{
  int32_t pwm = 1500 - (int32_t)(angle_rad * 1000.0f);
  if (pwm < 1000) pwm = 1000;
  if (pwm > 2000) pwm = 2000;
  return (uint32_t)pwm;
}
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Create the thread(s) */
  microROSTaskHandle = osThreadNew(StartDefaultTask, NULL, &microROSTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* USER CODE END RTOS_THREADS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the microROSTask thread.
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;

  /* -------------------------------------------------------------- */
  /* 1. Initialize sensors BEFORE micro-ROS to prevent session
   *    timeout while the BNO085 settles. Golden baseline order.
   * -------------------------------------------------------------- */
  BNO085_Init(&bno085, &hi2c1);
  /* ROTATION_VECTOR (0x05): 9-DoF fusion (accel + gyro + magnetometer).
   * Provides an absolute heading reference and per-frame accuracy
   * estimate (Q12 radians). The Jetson autonomy stack expects this on
   * /microros/imu_raw; do NOT swap back to GRV without coordinating
   * with the EKF/vision yaw weights on the Jetson side.
   *
   * 14-B payload per report — still fits inside the 128-B SHTP cargo
   * with accel + gyro batched. The 0x05 branch in BNO085_ParseInputReport
   * extracts the accuracy field. */
  BNO085_EnableReport(&bno085, BNO085_REPORT_ROTATION_VECTOR, 10000);
  osDelay(10);
  BNO085_EnableReport(&bno085, BNO085_REPORT_ACCELEROMETER,   10000);
  osDelay(10);
  BNO085_EnableReport(&bno085, BNO085_REPORT_GYROSCOPE,       10000);
  osDelay(10);

  /* URM37 purge (controlled): driver source absent, hardware DMA
   * remains initialized in usart.c but never started. No runtime cost. */

  /* -------------------------------------------------------------- */
  /* 2. Setup UART transport (it_transport.c).                      */
  /* -------------------------------------------------------------- */
  rmw_uros_set_custom_transport(true, (void *) &huart2,
                                cubemx_transport_open,
                                cubemx_transport_close,
                                cubemx_transport_write,
                                cubemx_transport_read);

  /* -------------------------------------------------------------- */
  /* 3. FreeRTOS-backed micro-ROS allocator.                        */
  /* -------------------------------------------------------------- */
  rcl_allocator_t freeRTOS_allocator = rcutils_get_zero_initialized_allocator();
  freeRTOS_allocator.allocate      = microros_allocate;
  freeRTOS_allocator.deallocate    = microros_deallocate;
  freeRTOS_allocator.reallocate    = microros_reallocate;
  freeRTOS_allocator.zero_allocate = microros_zero_allocate;

  if (!rcutils_set_default_allocator(&freeRTOS_allocator)) {
    osDelay(1000); NVIC_SystemReset();
  }

  /* -------------------------------------------------------------- */
  /* 4. Patient handshake.                                           */
  /* -------------------------------------------------------------- */
  while (rmw_uros_ping_agent(100, 1) != RMW_RET_OK) {
    osDelay(100);
  }

  rmw_uros_sync_session(1000);

  /* -------------------------------------------------------------- */
  /* 5. micro-ROS entity construction.                              */
  /* -------------------------------------------------------------- */
  rcl_publisher_t       imu_publisher;
  rcl_subscription_t    cmd_subscriber;
  sensor_msgs__msg__Imu imu_msg;
  rclc_support_t        support;
  rcl_allocator_t       allocator;
  rcl_node_t            node;
  rclc_executor_t       executor;

  allocator = rcl_get_default_allocator();

  rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
  rcl_init_options_init(&init_options, allocator);
  rcl_init_options_set_domain_id(&init_options, 42);

  if (rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator) != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
  }

  if (rclc_node_init_default(&node, "odyssix_imu_node", "", &support) != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
  }

  if (rclc_publisher_init_default(
        &imu_publisher, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
        "/microros/imu_raw") != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
  }

  if (rclc_executor_init(&executor, &support.context, 1, &allocator) != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
  }

  if (rclc_subscription_init_default(
        &cmd_subscriber, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(bfmc_msgs, msg, ControlCommand),
        "/control/command") != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
  }

  bfmc_msgs__msg__ControlCommand__init(&target_cmd);

  if (rclc_executor_add_subscription(
        &executor, &cmd_subscriber, &target_cmd,
        &control_cmd_callback, ON_NEW_DATA) != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
  }

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

  /* Park ESC and steering at neutral until first command arrives. */
  TIM2->CCR1 = 1500u;
  TIM2->CCR2 = 1500u;

  /* -------------------------------------------------------------- */
  /* 6. ROS 2 message structures.                                   */
  /* -------------------------------------------------------------- */
  sensor_msgs__msg__Imu__init(&imu_msg);
  rosidl_runtime_c__String__assign(&imu_msg.header.frame_id, "imu_link");

  /* Diagonal covariance — tuned for BNO085 fused outputs against a
   * robot_localization EKF. Indices 0/4/8 are the (i,i) diagonal of
   * the row-major 3x3 flattened to 9 elements. Off-diagonals stay
   * zero (no cross-axis correlation assumption). */
  memset(imu_msg.orientation_covariance,         0, sizeof(imu_msg.orientation_covariance));
  memset(imu_msg.angular_velocity_covariance,    0, sizeof(imu_msg.angular_velocity_covariance));
  memset(imu_msg.linear_acceleration_covariance, 0, sizeof(imu_msg.linear_acceleration_covariance));

  /* Roll/pitch stay tight (accel-gravity reference anchors them on the
   * Rotation Vector). Yaw is bumped to reflect the magnetometer-aided
   * Rotation Vector's residual drift under indoor magnetic disturbance
   * and BLDC vibration. The Jetson-side vision pipeline corrects yaw
   * via lane/sign/intersection relocalization; the looser covariance
   * lets robot_localization weight visual yaw appropriately. */
  imu_msg.orientation_covariance[0]         = 0.001;     /* roll  (rad^2) */
  imu_msg.orientation_covariance[4]         = 0.001;     /* pitch (rad^2) */
  imu_msg.orientation_covariance[8]         = 0.01;      /* yaw   (rad^2, RV residual drift) */

  imu_msg.angular_velocity_covariance[0]    = 2.19e-6;   /* (rad/s)^2 */
  imu_msg.angular_velocity_covariance[4]    = 2.19e-6;
  imu_msg.angular_velocity_covariance[8]    = 2.19e-6;

  imu_msg.linear_acceleration_covariance[0] = 0.004;     /* (m/s^2)^2 */
  imu_msg.linear_acceleration_covariance[4] = 0.004;
  imu_msg.linear_acceleration_covariance[8] = 0.004;

  /* Identity-quaternion default so the FIRST publish carries a sane
   * orientation even before the BNO085 sends its first packet. */
  imu_msg.orientation.x = 0.0; imu_msg.orientation.y = 0.0;
  imu_msg.orientation.z = 0.0; imu_msg.orientation.w = 1.0;

  /* -------------------------------------------------------------- */
  /* 6b. Power-up zeroing — 2 s blocking.                            */
  /*                                                                  */
  /* Runs AFTER micro-ROS sync (so the XRCE session is alive) and    */
  /* BEFORE the while(1) (so the first publish already carries the   */
  /* zeroed frame). Vehicle is parked at PWM neutral throughout, and */
  /* the deadman is still latched at last_cmd_time_ns == 0.          */
  /*                                                                  */
  /* The mean accel offset includes gravity, by design: we want the  */
  /* publish frame to read [0,0,0] at rest. The Jetson EKF tuning    */
  /* assumes this — do NOT switch the accel report to LINEAR_ACCEL   */
  /* without also retuning the offset semantics on the Jetson side.  */
  /* -------------------------------------------------------------- */
  BNO085_PerformStartupCalibration(&bno085);

  /* -------------------------------------------------------------- */
  /* 7. Main loop — golden topology.                                 */
  /* -------------------------------------------------------------- */
  for (;;)
  {
    static uint32_t sync_tick     = 0u;
    static uint32_t imu_err_count = 0u;
    static uint32_t led_tick      = 0u;

    /* Executor stays at fast cadence (~1 kHz) — IMU publish does NOT. */
    rclc_executor_spin_some(&executor, 0);

    /* Continuous clock re-sync every ~1 s (1000 loop iterations). */
    if (++sync_tick >= 1000u) {
      sync_tick = 0u;
      rmw_uros_sync_session(10);
    }

    int64_t time_ns = rmw_uros_epoch_nanos();

    /* ---------------------------------------------------------------- *
     * Race-condition-free SHTP read gate.                              *
     * ---------------------------------------------------------------- *
     * The BNO085 INTN pin is open-drain active-LOW. If a new packet    *
     * becomes ready while I2C is in the middle of reading the previous *
     * one, INTN remains LOW continuously — and the falling-edge EXTI   *
     * generates NO additional edge for the second packet. A flag-only  *
     * gate would then stall this loop until something else trips the   *
     * line, halting IMU output entirely.                               *
     *                                                                  *
     * The fix is a two-condition trigger:                              *
     *   - flag set  → an edge fired since last service                 *
     *   - pin LOW   → a packet is physically pending right now,        *
     *                 even if no fresh edge fired                      *
     *                                                                  *
     * The flag is cleared IMMEDIATELY BEFORE BNO085_GetData so any     *
     * mid-read re-assert produces a brand-new falling edge that the    *
     * ISR latches for the very next loop tick. This guarantees         *
     * continuous 100 Hz acquisition.                                   *
     * ---------------------------------------------------------------- */
    if (bno085_int_flag ||
        HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET) {
      bno085_int_flag = false;   /* clear BEFORE the read */

      HAL_StatusTypeDef imu_status = BNO085_GetData(&bno085);
      if (imu_status == HAL_OK) {
        imu_err_count = 0u;
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
    }

    /* ---------------------------------------------------------------- *
     * Hardware-readiness publish gate — software rate limiter removed. *
     * ---------------------------------------------------------------- *
     * data_valid is the single source of truth: set TRUE by the SH-2   *
     * parser when a fresh batch is decoded, cleared FALSE here after   *
     * publishing. The BNO085 itself paces the loop at its configured   *
     * 100 Hz report rate; layering a software 10 ms gate on top caused *
     * packet loss whenever sensor jitter pushed two reports into one   *
     * tick window and forced one to be dropped. The unified gate also  *
     * removes the negative-epoch rollback branch — there is no longer  *
     * any time arithmetic that could turn negative.                    *
     * ---------------------------------------------------------------- */
    if (bno085.data.data_valid) {
      imu_msg.header.stamp.sec     = (int32_t)(time_ns / 1000000000LL);
      imu_msg.header.stamp.nanosec = (uint32_t)(time_ns % 1000000000LL);

      /* --- Hot-path zeroing: all heavy work (mean, conjugate) was paid
       * for once in BNO085_PerformStartupCalibration. If calibration was
       * skipped or failed, calib.offsets are zero and the baseline
       * quaternion inverse is identity, so this block reduces to a
       * pass-through. */
      float q_pub_i, q_pub_j, q_pub_k, q_pub_r;
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

      /* ---------------------------------------------------------------- *
       * Enforce unit-norm before publish.                                *
       * ---------------------------------------------------------------- *
       * 32-bit float Hamilton products accumulate ~1e-7 per multiply, so *
       * the output magnitude drifts a few ULPs off 1.0. Even w = 1.0 +   *
       * 2e-7 makes the Jetson EKF's acos(w) return NaN, which cascades   *
       * through the whole filter. Normalize defensively. Single sqrtf +  *
       * four multiplies; the FPU eats this in ~12 cycles. The 1e-6 floor *
       * is a NaN guard in case both inputs degenerate to zero (e.g. an   *
       * un-calibrated frame against an identity baseline_inv at boot).   *
       * ---------------------------------------------------------------- */
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

      imu_msg.orientation.x = (double)q_pub_i;
      imu_msg.orientation.y = (double)q_pub_j;
      imu_msg.orientation.z = (double)q_pub_k;
      imu_msg.orientation.w = (double)q_pub_r;

      imu_msg.linear_acceleration.x = (double)(bno085.data.lin_accel_x - bno085.calib.offset_accel_x);
      imu_msg.linear_acceleration.y = (double)(bno085.data.lin_accel_y - bno085.calib.offset_accel_y);
      /* offset_accel_z is zeroed in BNO085_PerformStartupCalibration, so
       * this subtract leaves gravity (~9.81 m/s²) intact for the EKF. */
      imu_msg.linear_acceleration.z = (double)(bno085.data.lin_accel_z - bno085.calib.offset_accel_z);

      imu_msg.angular_velocity.x = (double)(bno085.data.gyro_x - bno085.calib.offset_gyro_x);
      imu_msg.angular_velocity.y = (double)(bno085.data.gyro_y - bno085.calib.offset_gyro_y);
      imu_msg.angular_velocity.z = (double)(bno085.data.gyro_z - bno085.calib.offset_gyro_z);

      (void)rcl_publish(&imu_publisher, &imu_msg, NULL);

      bno085.data.data_valid = false;   /* consume the frame */
    }

    /* PA5 heartbeat — golden cadence: every 50 ticks @ osDelay(1) = 10 Hz LED. */
    if (++led_tick >= 50u) {
      led_tick = 0u;
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    }

    /* Actuation: deadman + PWM drive. 500 ms timeout to neutrals. */
    {
      int64_t age_ns = time_ns - last_cmd_time_ns;
      if (last_cmd_time_ns == 0 || age_ns > 500000000LL) {
        TIM2->CCR1 = 1500u;
        TIM2->CCR2 = 1500u;
      } else {
        TIM2->CCR1 = steer_angle_to_pwm(target_cmd.steering_angle);

        uint32_t esc_pwm;
        if (target_cmd.throttle > 0.0f) {
          int32_t v = 1443 - (int32_t)(target_cmd.throttle * 443.0f);
          if (v < 1000) v = 1000;
          esc_pwm = (uint32_t)v;
        } else if (target_cmd.brake > 0.0f) {
          int32_t v = 1527 + (int32_t)(target_cmd.brake * 473.0f);
          if (v > 2000) v = 2000;
          esc_pwm = (uint32_t)v;
        } else {
          esc_pwm = 1500u;
        }
        TIM2->CCR2 = esc_pwm;
      }
    }

    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Application */
static void control_cmd_callback(const void *msgin)
{
  const bfmc_msgs__msg__ControlCommand *msg =
      (const bfmc_msgs__msg__ControlCommand *)msgin;

  target_cmd.steering_angle = msg->steering_angle;
  target_cmd.throttle       = msg->throttle;
  target_cmd.brake          = msg->brake;
  last_cmd_time_ns          = rmw_uros_epoch_nanos();
}
/* USER CODE END Application */
