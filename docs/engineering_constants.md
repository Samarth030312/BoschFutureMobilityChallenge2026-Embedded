# Engineering Constants Bible

**Generated:** 2026-06-08 | **Phase:** 7
**Discipline:** every constant has a file:line citation in the current source. No constant is left unexplained. Where the engineering justification is not explicit in the source (comments or surrounding code), the entry says so — "NOT PROVEN FROM CODE."

This document is the canonical reference for every numerical magic value in the firmware. If you change one, you change behavior — and you must update this document.

---

## TABLE OF CONTENTS

1. [Clock & PLL constants](#1-clock--pll-constants)
2. [FreeRTOS configuration](#2-freertos-configuration)
3. [Heap & memory constants](#3-heap--memory-constants)
4. [I²C & BNO085 constants](#4-i²c--bno085-constants)
5. [UART & micro-ROS constants](#5-uart--micro-ros-constants)
6. [TIM2 PWM constants](#6-tim2-pwm-constants)
7. [Steering & ESC mapping constants](#7-steering--esc-mapping-constants)
8. [Deadman & loop timing constants](#8-deadman--loop-timing-constants)
9. [Calibration constants](#9-calibration-constants)
10. [Quaternion processing constants](#10-quaternion-processing-constants)
11. [IMU covariance constants](#11-imu-covariance-constants)
12. [GPIO & EXTI constants](#12-gpio--exti-constants)
13. [Auto-recovery constants](#13-auto-recovery-constants)
14. [Watchdog (IWDG) constants — DISABLED](#14-watchdog-iwdg-constants--disabled)
15. [ROS constants](#15-ros-constants)
16. [LED / heartbeat constants](#16-led--heartbeat-constants)

---

## 1. Clock & PLL constants

### `RCC_OSCILLATORTYPE_HSI` — clock source

| | |
|---|---|
| Value | `RCC_OSCILLATORTYPE_HSI` (16 MHz internal RC) |
| File:line | `main.c:183` |
| Meaning | The system uses the STM32's internal RC oscillator as the PLL input. |
| Reason | Simpler bring-up; no external crystal required. Adequate for the team's needs. |
| Engineering justification | The Nucleo board has an HSE crystal available (via ST-LINK MCO) but the team chose HSI for simplicity. NOT PROVEN whether bench-tested for stability. |
| Consequences if changed | Switching to HSE (8 MHz crystal) requires `PLLM = 4` (not 8). Tighter timing accuracy (±50 ppm vs ±1 %) — better for UART at 460 800 baud. |
| Risk if drifts | UART BRR error at 460 800 baud; `HAL_UART_ErrorCallback` would self-heal most cases. the HSI clock source. |

### `PLLM = 8`, `PLLN = 84`, `PLLP = RCC_PLLP_DIV2`, `PLLQ = 4`

| | |
|---|---|
| Value | M=8, N=84, P=/2, Q=4 |
| File:line | `main.c:188-191` |
| Meaning | PLL configuration. `SystemClock = (HSI / PLLM) × PLLN / PLLP = (16/8) × 84 / 2 = 84 MHz`. Q is for USB OTG (unused). |
| Reason | Maximum SystemClock for STM32F401RE. |
| Engineering justification | F401RE max is 84 MHz at voltage scale 2 (`PWR_REGULATOR_VOLTAGE_SCALE2`). The choice maxes out the chip. |
| Consequences if changed | Lower clock → cleaner power (less EMI from MCU itself); slower BNO085 publish loop; same PWM/I²C timing (those have their own dividers). |

### `PWR_REGULATOR_VOLTAGE_SCALE2`

| | |
|---|---|
| Value | Voltage scale 2 (1.2 V core) |
| File:line | `main.c:178` |
| Meaning | Internal voltage regulator setting; gates the maximum allowed clock. |
| Reason | Required for 84 MHz on F401. Scale 1 would allow higher but F401 doesn't support that. |
| Consequences if changed | Scale 3 = lower power but max 64 MHz; would require re-tuning the PLL. |

### `FLASH_LATENCY_2`

| | |
|---|---|
| Value | 2 wait states |
| File:line | `main.c:206` |
| Meaning | FLASH read access wait cycles for instruction fetch at 84 MHz. |
| Reason | F4 datasheet: 84 MHz at 2.7-3.6V requires 2 wait states; at 2.4-2.7V requires 3. |
| Engineering justification | STM32F401RE on Nucleo runs at 3.3V → 2 wait states. |
| Consequences if changed | Wrong value → either crash on flash fetch (too few WS) or wasted CPU cycles (too many). |

### `RCC_HCLK_DIV2` (APB1 prescaler), `RCC_HCLK_DIV1` (APB2 prescaler)

| | |
|---|---|
| Value | APB1=/2 (42 MHz), APB2=/1 (84 MHz) |
| File:line | `main.c:203-204` |
| Meaning | APB1 max 42 MHz on F4 (limit per datasheet); APB2 can run at full 84 MHz. |
| Reason | F401RE peripheral clock domain limits. |
| Implication | **APB1 timer clock = 84 MHz** (timers get 2× APB1 clock when the prescaler != 1). This is what gives TIM2 its 1 µs counter resolution at prescaler 84. |

---

## 2. FreeRTOS configuration

### `configCPU_CLOCK_HZ` = `SystemCoreClock` (84 MHz)

| File:line | `FreeRTOSConfig.h:67` |
|---|---|
| Reason | Drives FreeRTOS tick computation (`SysTick_RELOAD = (SystemCoreClock / configTICK_RATE_HZ) - 1`). |

### `configTICK_RATE_HZ = 1000`

| File:line | `FreeRTOSConfig.h:68` |
|---|---|
| Meaning | 1 ms FreeRTOS tick. `osDelay(1)` = 1 ms. |
| Reason | Standard; balances responsiveness vs. tick overhead. |
| Consequences if changed | 100 Hz tick → coarser `osDelay`; 10 kHz tick → more overhead, finer delay. |

### `configMAX_PRIORITIES = 56`

| File:line | `FreeRTOSConfig.h:69` |
|---|---|
| Reason | CMSIS-RTOS V2 default. Plenty of room; team uses only 0, 2, 24. |

### `configMINIMAL_STACK_SIZE = 128` words

| File:line | `FreeRTOSConfig.h:70` |
|---|---|
| Meaning | Idle task stack = 512 bytes. |
| Reason | FreeRTOS port default for Cortex-M4. |
| Consequences if changed | Too small → idle task overflow → silent corruption (no stack-overflow hook). |

### `configTOTAL_HEAP_SIZE = 32000`

| File:line | `FreeRTOSConfig.h:71` |
|---|---|
| Meaning | FreeRTOS internal heap size (vendor heap_4 `ucHeap[]`). |
| Reason | Large enough for task stacks + TCBs + future expansion. |
| Critical | **This same constant is also used by `custom_memory_manager.c:30`** for the micro-ROS heap. **Two heaps × 32 KB = 64 KB out of 96 KB RAM.** the dual-heap sizing. |
| Consequences if changed | Reduce → frees RAM but tightens budget. Increase → less RAM for stacks. |

### `configMAX_TASK_NAME_LEN = 16`

| File:line | `FreeRTOSConfig.h:72` |
|---|---|
| Reason | Long enough for `"microROSTask"` (12 chars). |

### `configTIMER_TASK_PRIORITY = 2`, `configTIMER_QUEUE_LENGTH = 10`, `configTIMER_TASK_STACK_DEPTH = 256`

| File:line | `FreeRTOSConfig.h:92-94` |
|---|---|
| Meaning | Software-timer task at priority 2, queue 10, 1 KB stack. |
| Reason | Standard. The application does not use software timers, but the task exists. |
| Consequences if changed | Functionally none in this firmware. |

### `configKERNEL_INTERRUPT_PRIORITY = 15`, `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`

| File:line | `FreeRTOSConfig.h:136, 142` (shifted derivations at 146, 149) |
|---|---|
| Meaning | Kernel ISRs (SysTick, PendSV) at priority 15 (lowest). FreeRTOS-safe ISRs must be priority ≥ 5 (numerically). |
| Reason | Cortex-M port requirements. |
| Critical contract | Every peripheral ISR in this firmware is at priority 5 (see Section 12). They are EXACTLY at the floor — may call `*FromISR` APIs. |
| Consequences if changed | Lowering syscall priority below 5 → ISRs preempt kernel critical sections → undefined behavior. |

### `configENABLE_FPU = 0`

| File:line | `FreeRTOSConfig.h:59` |
|---|---|
| Meaning | FreeRTOS port does NOT save/restore FPU registers on task switch. |
| Reason | Single-task design; FPU context save is wasted cycles. |
| Critical risk | **the FPU context-save state.** If a second task uses the FPU, silent corruption across context switches. Single-task safe today. |

### `configCHECK_FOR_STACK_OVERFLOW` — NOT DEFINED (= 0)

| File | `FreeRTOSConfig.h` |
|---|---|
| Meaning | No stack overflow detection. Overflow = silent corruption. |
| Recommendation (NOT MADE) | Set to 2 + define `vApplicationStackOverflowHook` → reset. |

### `configUSE_MALLOC_FAILED_HOOK` — NOT DEFINED (= 0)

| File | `FreeRTOSConfig.h` |
|---|---|
| Meaning | No OOM detection. `pvPortMalloc` NULL-return is silent. |
| Recommendation (NOT MADE) | Set to 1 + define `vApplicationMallocFailedHook` → reset. |

### `configASSERT(x)` body

| | |
|---|---|
| Value | `if ((x) == 0) {taskDISABLE_INTERRUPTS(); for( ;; );}` |
| File:line | `FreeRTOSConfig.h:154` |
| Meaning | Failed assert → hang with interrupts disabled. |
| Critical risk | Same problem class as the Error_Handler hang behaviour `Error_Handler`: TIM2 keeps last PWM. |
| Recommendation (NOT MADE) | Replace with `NVIC_SystemReset()`. |

---

## 3. Heap & memory constants

### `_Min_Heap_Size = 0x200` (512 bytes)

| File:line | `STM32F401RETX_FLASH.ld:41` |
|---|---|
| Meaning | Reserve for the newlib `sbrk` heap (used by `malloc`/`printf` in libc). |
| Reason | Small — printf is never called in this firmware. |
| Consequences if changed | Lower → libc `malloc` fails sooner. Higher → wastes RAM. |

### `_Min_Stack_Size = 0x400` (1024 bytes)

| File:line | `STM32F401RETX_FLASH.ld:42` |
|---|---|
| Meaning | Reserve for the MSP (Main Stack Pointer) at top of RAM. Used for interrupt frames + early init. |
| Reason | 1 KB tolerates ~10-20 nested IRQ frames on Cortex-M4 (32 bytes each without FPU lazy stacking; 100+ bytes with). |
| Consequences if changed | Lower → MSP overflow on deep IRQ nesting (rare). Higher → wastes RAM (cuts into BSS/heap region). |

### `_estack = ORIGIN(RAM) + LENGTH(RAM)` = `0x20018000`

| File:line | `STM32F401RETX_FLASH.ld:39` |
|---|---|
| Meaning | Initial MSP value (top of RAM). Loaded from vector entry 0 at reset. |
| Reason | Cortex-M4 stacks grow downward; start at top to maximize usable region. |

### micro-ROS task stack = `3000 * 4 = 12000` bytes

| File:line | `freertos.c:48` (`.stack_size = 3000 * 4`) |
|---|---|
| Meaning | Stack for `microROSTask`. Allocated from FreeRTOS heap by `osThreadNew`. |
| Reason | rcl/rclc deserialisation frames are deep; 12 KB is conservative. |
| Verification needed | `uxTaskGetStackHighWaterMark(microROSTaskHandle)` would show actual peak. NOT PROVEN; no telemetry exists. |
| Consequences if changed | Lower → overflow risk (silent — see configCHECK_FOR_STACK_OVERFLOW = 0). Higher → eats FreeRTOS heap. |

---

## 4. I²C & BNO085 constants

### `BNO085_I2C_ADDR = (0x4A << 1) = 0x94`

| File:line | `bno085.h:9` |
|---|---|
| Meaning | 7-bit I²C address shifted to HAL convention. ADR pin tied LOW selects 0x4A. |
| Reason | BNO085 default. |
| Consequences if changed | Must match the physical ADR strap. |

### I²C1 `ClockSpeed = 400000` (400 kHz Fast-mode)

| File:line | `i2c.c:41` |
|---|---|
| Meaning | I²C bus clock. |
| Reason | BNO085 max supported. ~3 KB/s throughput required; 400 kHz gives ~50 KB/s ceiling. Comfortable. |
| Consequences if changed | 100 kHz Standard-mode → less margin; might still work. 1 MHz Fast-mode-plus → BNO085 doesn't support. |

### I²C1 `DutyCycle = I2C_DUTYCYCLE_2`

| File:line | `i2c.c:42` |
|---|---|
| Meaning | 16:9 high:low ratio (vs 2:1 for `_16_9`). |
| Reason | F4 datasheet recommends `_2` for 400 kHz at high APB1 clock. |

### I²C1 `NoStretchMode = DISABLE` (stretch ENABLED)

| File:line | `i2c.c:48` |
|---|---|
| Meaning | Slave (BNO085) is allowed to hold SCL low to extend transactions. |
| Reason | BNO085 may stretch during SHTP cargo prep. |

### HAL_I2C_Master_Receive timeout = 100 ms (used in BNO085 ReadPacket)

| File:line | `bno085.c:15, 29, 52` |
|---|---|
| Meaning | Maximum wait per I²C transaction. |
| Reason | Bounded blocking; 100 ms is much larger than a successful transaction (~5 ms at 400 kHz for 30 bytes). |
| Consequences if changed | Too short → spurious errors on slow BNO085 responses. Too long → wider main-loop stall (worst case = 200 ms for two-stage read). The deadman tolerates up to 500 ms. |

### `BNO085_SHTP_HEADER_SIZE = 4`

| File:line | `bno085.h:37` |
|---|---|
| Meaning | SHTP fixed header bytes. |
| Reason | Bosch SHTP spec. |

### `BNO085_MAX_PACKET_SIZE = 128`

| File:line | `bno085.h:38` |
|---|---|
| Meaning | rx/tx buffer cap. |
| Reason | Worst-case batched cargo at this config is ~43 bytes (5 timestamp + 10 accel + 10 gyro + 14 RotVec + 4 header). 128 has overwhelming headroom. |
| Consequences if changed | Lower → cargo truncation if firmware ever enables more reports. Higher → wastes RAM. |

### SHTP channel IDs: `BNO085_CHANNEL_COMMAND=0`, `EXE=1`, `CONTROL=2`, `INPUT_SENSOR=3`

| File:line | `bno085.h:12-15` |
|---|---|
| Meaning | Standard SHTP channel numbering. Channel 3 = sensor reports (the only one read continuously). |
| Reason | Bosch SHTP spec; not negotiable. |

### SH-2 report IDs (from BNO085 SH-2 Reference Manual Table 6-1)

| Constant | Value | File:line |
|---|---|---|
| `BNO085_REPORT_ACCELEROMETER` | 0x01 | `bno085.h:18` |
| `BNO085_REPORT_GYROSCOPE` | 0x02 | `bno085.h:19` |
| `BNO085_REPORT_LINEAR_ACCELERATION` | 0x04 | `bno085.h:20` |
| `BNO085_REPORT_ROTATION_VECTOR` | 0x05 | `bno085.h:21` |
| `BNO085_REPORT_GAME_ROTATION_VECTOR` | 0x08 | `bno085.h:22` |
| `BNO085_SH2_TIMESTAMP_REBASE` | 0xFA | `bno085.c:67` |
| `BNO085_SH2_BASE_TIMESTAMP_REF` | 0xFB | `bno085.c:68` |
| `BNO085_REPORT_SET_FEATURE_COMMAND` | 0xFD | `bno085.h:25` |

### SH-2 report lengths (bytes including ID)

| Report | Length | File:line |
|---|---|---|
| Timestamp rebase, Base timestamp | 5 | `bno085.c:104-105` |
| Accelerometer, Gyroscope, LinearAccel | 10 | `bno085.c:106-108` |
| Game Rotation Vector | 12 | `bno085.c:109` |
| Rotation Vector (with accuracy) | 14 | `bno085.c:110` |

### Report enable interval = 10000 µs (100 Hz)

| File:line | `freertos.c:117, 119, 121` |
|---|---|
| Meaning | Three reports enabled at 100 Hz each. BNO085 batches them into one SHTP packet per 10 ms. |
| Reason | Matches the Jetson EKF tick (100 Hz). Above this would oversample BNO085's internal fusion (~12.5 ms time constant). |
| Consequences if changed | Higher → BNO085 clips to its internal max rate. Lower → stale samples; Jetson EKF starves. |

### `osDelay(10)` between EnableReport calls

| File:line | `freertos.c:118, 120, 122` |
|---|---|
| Meaning | 10 ms wait between Set-Feature commands. |
| Reason | Gives BNO085 time to process each. |
| Consequences if changed | Lower → BNO085 may NACK subsequent commands. |

### BNO085 startup flush count = 5

| File:line | `bno085.c:278` (`for (int i = 0; i < 5; i++)`) |
|---|---|
| Meaning | Number of packets to read-and-discard during `BNO085_Init` to drain boot announcements (SHTP advertisement, reset announce). |
| Reason | Empirically sufficient; the BNO085 typically emits 3-5 packets at boot. |
| Consequences if changed | Lower → first real read may catch boot leftovers and parse as garbage. Higher → wastes ~50 ms of init time. |

### `osDelay(100)` initial / `osDelay(50)` on fail / `osDelay(10)` on success during BNO085 init flush

| File:line | `bno085.c:275, 280, 282` |
|---|---|
| Reason | Differentiated wait based on whether the BNO085 responded. 50 ms on fail gives more recovery time. |

---

## 5. UART & micro-ROS constants

### USART2 baud = 460800

| File:line | `usart.c:75` |
|---|---|
| Meaning | XRCE-DDS link to Jetson. |
| Reason | Standard high-throughput rate; F4 USART can generate cleanly from 84 MHz. |
| Throughput check | `/microros/imu_raw` at 100 Hz × ~280 B = ~28 KB/s = ~50 % link. |
| Consequences if changed | Lower (115200) → can't sustain 100 Hz IMU + control. Higher → BRR error gets worse with HSI tolerance. |

### USART2 word length 8, stop bits 1, parity none

| File:line | `usart.c:76-79` |
|---|---|
| Meaning | Standard "8N1". |
| Reason | Compatible with USB-CDC bridge. |

### USART2 oversampling = `UART_OVERSAMPLING_16`

| File:line | `usart.c:81` |
|---|---|
| Reason | Standard 16× oversampling for best RX margin at 460800. |

### USART1 baud = 9600 (DEAD)

| File:line | `usart.c:46` |
|---|---|
| Meaning | USART1 init for a peripheral that no longer exists in this firmware. |
| Reason | Likely leftover from a previously-present sensor (now removed). The init runs but no source code uses `huart1`. |
| the dead USART1 init | Dead code; recommend removal. |

### UART RX ring `UART_IT_BUFFER_SIZE = 2048`

| File:line | `it_transport.c:29` |
|---|---|
| Meaning | XRCE-DDS RX buffer. |
| Reason | Source comment says "absorbs ~40 ms of sustained 460800-baud traffic." (Math: 2048 B / 46080 B/s = 44 ms.) |
| Consequences if changed | Lower → drops under sustained bursts. Higher → wastes RAM (already 97% used). |

### UART TX blocking timeout = 100 ms

| File:line | `it_transport.c:78` (in `HAL_UART_Transmit`) |
|---|---|
| Meaning | Maximum wait for a TX burst to complete. |
| Reason | Typical XRCE message at 460800 baud takes ~2 ms; 100 ms is bounded. |
| Consequences if changed | Lower → spurious TX failures. Higher → wider main-loop stall on slow link. |

### micro-ROS ping timeout 100 ms, attempts 1

| File:line | `freertos.c:152` (`rmw_uros_ping_agent(100, 1)`) |
|---|---|
| Meaning | Per-attempt ping timeout, one attempt per loop. |
| Reason | Loop retries every 100 ms with `osDelay(100)` between attempts. Fast probe; firmware survives slow docker startup. |

### micro-ROS session sync initial = 1000 ms, periodic = 10 ms

| File:line | `freertos.c:156` (initial), `freertos.c:283` (periodic) |
|---|---|
| Meaning | Initial sync has 1000 ms slack; periodic re-sync (every ~1 s, see Section 8) uses 10 ms. |
| Reason | Initial sync waits for the agent to fully establish; periodic re-sync is quick. |

### Domain ID = 42

| File:line | `freertos.c:173` |
|---|---|
| Meaning | DDS domain ID; must match Jetson env. |
| Reason | Team choice. Hardcoded. |
| Risk | Mismatch with Jetson's `ROS_DOMAIN_ID` env → silent no-discovery. Recommendation: build flag. |

### Node name `"odyssix_imu_node"`

| File:line | `freertos.c:179` |
|---|---|
| Meaning | Visible via `ros2 node list`. |

### Topic names `"/microros/imu_raw"` (pub), `"/control/command"` (sub)

| File:line | `freertos.c:186, 197` |
|---|---|
| Meaning | The two ROS topics this firmware participates in. |

### micro-ROS executor handle count = 1

| File:line | `freertos.c:190` (`rclc_executor_init(..., 1, ...)`) |
|---|---|
| Meaning | One subscription handle. Sufficient because we have one subscriber. |

### `rcutils_set_default_allocator` failure → `osDelay(1000); NVIC_SystemReset()`

| File:line | `freertos.c:146` |
|---|---|
| Meaning | If the allocator install fails, log delay + reset. |
| Reason | Without our custom allocator, micro-ROS would consume the FreeRTOS internal heap. Better to reset and try again. |

---

## 6. TIM2 PWM constants

### TIM2 `Prescaler = 84 - 1`

| File:line | `tim.c:45` |
|---|---|
| Meaning | Counter clock = 84 MHz / (83 + 1) = **1 MHz** = 1 µs/tick. |
| Reason | Gives 1 µs resolution for PWM pulse-width — matches the hobby servo/ESC convention exactly. |
| Consequences if changed | Different resolution; would require re-tuning every CCR mapping. |

### TIM2 `Period = 20000 - 1`

| File:line | `tim.c:47` |
|---|---|
| Meaning | Period = 20000 µs = 20 ms → **50 Hz PWM**. |
| Reason | Standard 50 Hz analogue servo + ESC frequency. |
| Consequences if changed | Higher freq → faster control loops but most hobby ESCs reject. |

### TIM2 `CounterMode = TIM_COUNTERMODE_UP`

| File:line | `tim.c:46` |
|---|---|
| Reason | Standard. PWM1 mode counts up to CCR (high), then up to ARR (low). |

### TIM2 `AutoReloadPreload = DISABLE`

| File:line | `tim.c:49` |
|---|---|
| Meaning | ARR updates take effect immediately, not on next update event. |
| Reason | ARR is never changed at runtime; preload disabled is fine. |

### Initial CCR = 0

| File:line | `tim.c:70` (`sConfigOC.Pulse = 0`) |
|---|---|
| Meaning | Both channels start at pulse 0 µs = always-LOW (no pulse). |
| Reason | Until PWM is started (`HAL_TIM_PWM_Start` in `freertos.c:209-210`), the output is LOW. After start, CCR is immediately written to 1500 µs neutral. |

### TIM2 OC config: `OCMode = TIM_OCMODE_PWM1`, `OCPolarity = TIM_OCPOLARITY_HIGH`, `OCFastMode = TIM_OCFAST_DISABLE`

| File:line | `tim.c:69-72` |
|---|---|
| Meaning | Standard PWM mode 1 (high while counter < CCR), output high polarity, fast mode disabled. |
| Reason | Matches hobby servo expectation (high pulse for high command). |

---

## 7. Steering & ESC mapping constants

### Steering: `1500 - (angle_rad * 1000)` clamp [1000, 2000]

| File:line | `freertos.c:66-72` (`steer_angle_to_pwm`) |
|---|---|
| Meaning | Linear mapping: 0 rad = 1500 µs center; +1 rad = 500 µs (clamped to 1000); -1 rad = 2500 µs (clamped to 2000). |
| Reason | The team's vehicle's steering linkage produces ±~0.5 rad effective range; the conversion gives ±500 µs around neutral. |
| Consequences if changed | Different vehicle geometry → recalibrate. |

### ESC throttle: `1443 - (throttle * 443)` clamp ≥ 1000

| File:line | `freertos.c:421-425` |
|---|---|
| Meaning | Forward throttle command. throttle=0 maps to 1443 µs (start of forward range); throttle=1.0 maps to 1000 µs (full forward). |
| Reason | Bench-calibrated for the specific Quicrun ESC. The 1443 represents the upper edge of the deadband. |
| Consequences if changed | Different ESC → re-bench-test. |

### ESC brake: `1527 + (brake * 473)` clamp ≤ 2000

| File:line | `freertos.c:426-430` |
|---|---|
| Meaning | Brake/reverse command. brake=0 maps to 1527 µs (start of reverse); brake=1.0 maps to 2000 µs (full reverse / max brake). |
| Reason | Bench-calibrated. The 1527 represents the lower edge of the deadband. |
| Critical | Quicrun's brake=1.0 → full reverse. The Jetson safety supervisor caps brake at 0.5. |

### ESC neutral = `1500u`

| File:line | `freertos.c:430, 213, 214, 415-416` |
|---|---|
| Meaning | "No throttle, no brake" → 1500 µs (center of deadband). ESC interprets as motor disengaged. |

### Deadband halves: 1500 - 1443 = 57 µs forward; 1527 - 1500 = 27 µs reverse

| | |
|---|---|
| Meaning | The ESC's deadband range is asymmetric: ±57 µs forward, ±27 µs reverse. |
| Reason | Quicrun-specific characterisation. NOT PROVEN from datasheet — bench-test only. |

---

## 8. Deadman & loop timing constants

### Deadman timeout = `500000000LL` ns (500 ms)

| File:line | `freertos.c:414` |
|---|---|
| Meaning | If `last_cmd_time_ns` is older than 500 ms (or zero), force PWM neutral. |
| Reason | Larger than the Jetson safety supervisor's stale-command window (~250 ms) so the supervisor catches it first; STM32 is the last-resort backstop. |
| Consequences if changed | Lower → thrashing on transient lag. Higher → unsafe gap. |

### `osDelay(1)` main-loop tick = 1 ms

| File:line | `freertos.c:436` |
|---|---|
| Meaning | Main loop cadence. |
| Reason | Matches `configTICK_RATE_HZ = 1000`. Polls 10× per BNO085 packet interval. |
| Consequences if changed | `osDelay(0)` → busy-spin. `osDelay(10)` → 10× coarser polling. |

### Sync re-tick threshold = `1000` (~1 s)

| File:line | `freertos.c:281` (`if (++sync_tick >= 1000u)`) |
|---|---|
| Meaning | One loop iteration is at least one `osDelay(1)` tick (1 ms at `configTICK_RATE_HZ = 1000`), so 1000 iterations ≈ 1 s, slightly more on iterations that read and publish the IMU. Every ~1 s, re-sync XRCE-DDS clock with agent. |
| Reason | Drift-correction periodicity. NOT PROVEN FROM CODE why 1000 was chosen. At the ±1 % HSI tolerance quoted in Section 1, the local clock can drift by up to ~10 ms between re-syncs at this period. |

### LED toggle threshold = `50` ticks (10 Hz blink)

| File:line | `freertos.c:406` (`if (++led_tick >= 50u)`) |
|---|---|
| Meaning | Toggle PA5 every 50 ms → 100 ms period → 10 Hz. |
| Reason | Visible heartbeat. Easy to confirm by eye that firmware is alive. |
| Consequences if changed | Higher → harder to see. Lower → fast flicker. |

### Auto-recovery error threshold = `50` consecutive errors

| File:line | `freertos.c:315` (`if (++imu_err_count >= 50u)`) |
|---|---|
| Meaning | After 50 consecutive `BNO085_GetData` failures, re-init. |
| Reason | 50 × ~10 ms BNO085 period ≈ 500 ms of consecutive failure. Short enough to recover quickly, long enough not to thrash. |
| Consequences if changed | Lower → recovery on transient glitches. Higher → slower recovery. |

---

## 9. Calibration constants

### `CALIBRATION_MS = 2000u`

| File:line | `bno085.c:347` |
|---|---|
| Meaning | Boot calibration window in milliseconds. |
| Reason | 2 s × 100 Hz = ~200 samples → mean is ~14× more precise than a single sample. |
| Consequences if changed | Lower → noisier bias estimate. Higher → longer boot delay. |

### Z-accel offset = `0.0f` (explicitly zero)

| File:line | `bno085.c:424` |
|---|---|
| Meaning | Z-component of accel bias is NOT subtracted at publish. |
| Reason | Source comment: preserves the gravity vector (~9.81 m/s²) for the Jetson EKF's pitch/roll estimation. |
| Consequences if changed | Removing gravity → Jetson EKF cannot recover pitch/roll. |

### Identity-quaternion default `quat_real = 1.0f`

| File:line | `bno085.c:273`, `freertos.c:251` |
|---|---|
| Meaning | First publish before any BNO085 packet has a valid identity quaternion. |
| Reason | Prevents NaN downstream if EKF receives an all-zero quaternion. |

### Min sample count check `sample_count == 0u`

| File:line | `bno085.c:399` |
|---|---|
| Meaning | If zero samples received during 2 s, calibration failed. |
| Reason | Detects BNO085 silent. |
| Current behavior | Identity defaults applied; HAL_ERROR returned but DISCARDED by caller. **Silent failure path** — a loud signal is recommended. |

---

## 10. Quaternion processing constants

### Quaternion sanity gate `|q|² ∈ [0.9, 1.1]`

| File:line | `bno085.c:193, 223` |
|---|---|
| Meaning | Accept the quaternion if its magnitude squared is between 0.9 and 1.1 (i.e., \|q\| within ~5 % of unit). |
| Reason | A healthy unit quaternion stays within float ULP of 1.0. Out-of-band magnitude indicates I²C cargo corruption from EMI. |
| Consequences if changed | Narrower → reject legitimate float drift. Wider → admit corrupt data. |

### Unit-norm renorm threshold `q_mag > 0.000001f`

| File:line | `freertos.c:377` |
|---|---|
| Meaning | Don't divide by zero if both `q_pub` and `baseline_quat_inv` degenerate. |
| Reason | NaN guard. |
| Consequences if changed | Lower → division by epsilon → enormous quaternion. Higher → may skip valid normalisation. |

### Quaternion Q-points: i/j/k/real = Q14, accuracy = Q12

| File:line | `bno085.c:182-185, 217-220, 228` |
|---|---|
| Meaning | Fixed-point format from BNO085. Q14 = scale by 1/16384; Q12 = 1/4096. |
| Reason | BNO085 SH-2 spec. |

### Accel Q-point = Q8 (scale 1/256)

| File:line | `bno085.c:156-158` |
|---|---|
| Reason | SH-2 spec for accel report (range ±127 m/s² @ Q8). |

### Gyro Q-point = Q9 (scale 1/512)

| File:line | `bno085.c:167-169` |
|---|---|
| Reason | SH-2 spec for gyro report (range ±64 rad/s @ Q9). |

---

## 11. IMU covariance constants

### Orientation diagonal = `[0.001, 0.001, 0.01]` rad²

| File:line | `freertos.c:236-238` |
|---|---|
| Meaning | Roll = 1e-3, pitch = 1e-3, **yaw = 1e-2** (intentionally 10× wider). |
| Reason | Yaw is mag-fused; magnetometer drifts under BLDC EMI. Wider covariance tells the Jetson EKF to trust yaw less. |
| Source comment | `"yaw is bumped because GRV has no magnetometer reference and drifts..."` — note: the current report is Rotation Vector (0x05, mag-fused), not Game Rotation Vector (0x08). The comment is from a historical state but the value is correct for the current rationale (mag corruption under EMI). |

### Angular velocity diagonal = `2.19e-6` (rad/s)²

| File:line | `freertos.c:240-242` |
|---|---|
| Meaning | Gyro noise variance. |
| Reason | BNO085 datasheet noise spec. NOT PROVEN exact derivation (likely Allan-variance). |

### Linear acceleration diagonal = `0.004` (m/s²)²

| File:line | `freertos.c:244-246` |
|---|---|
| Meaning | Accel noise variance. |
| Reason | BNO085 datasheet noise spec. |

### `frame_id = "imu_link"`

| File:line | `freertos.c:220` |
|---|---|
| Meaning | TF frame name; matches the Jetson autonomy's static TF tree. |

---

## 12. GPIO & EXTI constants

### PA5 LED: mode `OUTPUT_PP`, pull `NOPULL`, speed `LOW`, initial RESET

| File:line | `gpio.c:47-54` |
|---|---|
| Meaning | Nucleo on-board user LED (green). Off at boot, toggled at 10 Hz. |

### PB4 INTN: mode `IT_FALLING`, pull `PULLUP`, speed `LOW`

| File:line | `gpio.c:64-68` |
|---|---|
| Meaning | BNO085 INTN input: falling-edge interrupt, internal pull-up holds HIGH at rest, low speed (signal is slow). |
| Reason | INTN is open-drain active-LOW; pull-up sources the HIGH; falling edge fires EXTI4. |

### EXTI4 priority = `5, 0`

| File:line | `gpio.c:73` |
|---|---|
| Meaning | Preemption priority 5, sub-priority 0. |
| Reason | Matches `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`. The ISR is allowed to call `*FromISR` FreeRTOS APIs. |

### USART2_IRQn, USART1_IRQn, DMA1_Stream5, DMA2_Stream2, DMA2_Stream7 all at priority `5, 0`

| File:line | `usart.c:154, 200`, `dma.c:48-55` |
|---|---|
| Meaning | All peripheral ISRs at the FreeRTOS syscall floor. |
| Reason | Same as EXTI4 — allows FreeRTOS API access if needed. |

### PendSV priority = `15, 0`

| File:line | `stm32f4xx_hal_msp.c:75` |
|---|---|
| Meaning | Lowest priority. |
| Reason | FreeRTOS port requirement. PendSV does the context switch; must run only when no other ISR is pending. |

---

## 13. Auto-recovery constants

(See Section 8 `freertos.c:315` for error threshold = 50.)

### Auto-recovery re-enables reports at 10000 µs

| File:line | `freertos.c:322-324` |
|---|---|
| Meaning | Same 100 Hz as initial enable. |
| Reason | Re-establishes the original report config. |
| Critical | Calibration is NOT re-run — preserves the boot-time offsets per source comment `freertos.c:317-320`. |

---

## 14. Watchdog (IWDG) constants — DISABLED

### IWDG prescaler `/32` (`IWDG_PR_DIV32 = 3`)

| File:line | `main.c:78` |
|---|---|
| Meaning | LSI (~32 kHz) / 32 = 1 kHz IWDG counter = 1 ms per count. |
| Reason | Convenient 1 ms resolution. |
| Status | **Code path is dead** — `IWDG_Init_Direct` is `__attribute__((unused))` and the call site at `main.c:135-140` is commented out. the IWDG-disabled state. |

### IWDG reload `2000` (`IWDG_RLR_2000MS`)

| File:line | `main.c:79` |
|---|---|
| Meaning | 2000 × 1 ms = 2 s timeout. |
| Reason | Long enough to absorb the BNO085 200 ms I²C stall + executor + publish; short enough to recover from genuine hangs within seconds. |
| Status | Dormant . |

### IWDG keys: `0x5555` (unlock), `0xAAAA` (reload), `0xCCCC` (start)

| File:line | `main.c:75-77` |
|---|---|
| Meaning | Standard STM32 IWDG key register values. |
| Reason | Datasheet-defined. |

---

## 15. ROS constants

(See Section 5 for domain ID, node name, topic names.)

### `bfmc_msgs/ControlCommand` fields: `steering_angle`, `throttle`, `brake`

| File:line | `freertos.c:447-449` (consumed) |
|---|---|
| Meaning | The three fields the firmware reads from `/control/command`. |

### `sensor_msgs/Imu` fields written

| | |
|---|---|
| Fields | `header.stamp`, `header.frame_id`, `orientation`, `orientation_covariance`, `angular_velocity`, `angular_velocity_covariance`, `linear_acceleration`, `linear_acceleration_covariance` |
| File:line | `freertos.c:342-398` |

---

## 16. LED / heartbeat constants

(See Section 8 for LED toggle threshold.)

### PA5 initial state = `GPIO_PIN_RESET` (off)

| File:line | `gpio.c:47` |
|---|---|
| Reason | Visually neutral at boot. First toggle ~50 ms after main loop entry. |

---

## Constants this document INTENTIONALLY does NOT cover

The following are vendor / framework constants whose values are not under team control:

- HAL state-machine internals (`HAL_LOCK`, `HAL_BUSY`, etc.).
- FreeRTOS portmacro values (`portMAX_DELAY`, etc.).
- CMSIS register address offsets.
- `Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_*.h` magic numbers.

These are documented by their respective vendors. Treat them as black-box constants.

---

## Verification: constants for a future change

If you need to change a constant, this is your checklist:

1. **Find the constant in this document.** If it's not here, add it.
2. **Read the "Consequences if changed" row** — understand the impact.
3. **Trace the file:line citation** — make sure the source matches what this document says.
4. **Plan how to verify the new value** — usually bench testing.
5. **Update this document** with the new value and reasoning.
6. **Commit both the source change AND this document together.**

Any commit that changes a constant without updating this document is a regression in institutional knowledge.

---

## Open issues (NOT PROVEN; future work)

- Verification of HSI drift across temperature — bench measurement.
- Verification of `microROSTask` stack high-water-mark — instrumented build.
- Verification of custom heap fragmentation under steady-state load — long-run test.
- Bench-test report for the Quicrun ESC's exact deadband endpoints — currently only encoded as 1443/1527 magic numbers without a documented test report.
