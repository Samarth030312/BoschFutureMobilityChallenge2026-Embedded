# BNO085 Subsystem — Documentation Index

**Generated:** 2026-06-08 | **Phase:** 3 (BNO085 Reverse Engineering)

This subsystem documentation is split across 10 files per the spec. To preserve cohesion for a reviewer, the technical material is dense in each file but cross-references are explicit.

| # | File | Content summary |
|---|---|---|
| 01 | `01_hardware_connection.md` | Pin mapping, electrical interface, I²C address, INTN line, power |
| 02 | `02_shtp_protocol.md` | SHTP framing, header format, channels, two-stage read |
| 03 | `03_sh2_protocol.md` | SH-2 report IDs, timebase markers, multi-report walker |
| 04 | `04_report_configuration.md` | Set-feature command, enabled reports, intervals |
| 05 | `05_quaternion_pipeline.md` | Q-point math, Hamilton product, sanity gate, baseline conjugation |
| 06 | `06_calibration.md` | 2-second boot calibration, Z-accel-preserved, sample count |
| 07 | `07_covariance_generation.md` | Published covariances + Jetson EKF interaction |
| 08 | `08_error_handling.md` | EXTI race-free gate, 50-error auto-recovery |
| 09 | `09_failure_modes.md` | Per-failure-mode catalog with detection + recovery |
| 10 | `10_interview_questions.md` | High-signal interview Q&A for this subsystem |

## Quick reference

| Fact | Value | Source |
|---|---|---|
| I²C bus | I2C1 | `bno085.h:9`, `i2c.c` |
| I²C address | `0x4A << 1 = 0x94` | `bno085.h:9` |
| I²C speed | 400 kHz Fast-mode | `i2c.c:41-44` |
| SDA/SCL pins | PB9/PB8 | `i2c.c` HAL_I2C_MspInit |
| INTN pin | PB4 | `gpio.c:64-68` |
| INTN trigger | falling edge, internal pull-up | `gpio.c:64-66` |
| EXTI4 NVIC priority | 5, 0 | `gpio.c:73` |
| Reports enabled | Rotation Vector (0x05), Accelerometer (0x01), Gyroscope (0x02) | `freertos.c:117-121` |
| Report interval | 10 000 µs (100 Hz) | same |
| Calibration window | 2 000 ms | `bno085.c:347` |
| Quaternion sanity gate | \|q\|² ∈ [0.9, 1.1] | `bno085.c:192-201, 222-230` |
| Error threshold for auto-recovery | 50 consecutive | `freertos.c:315` |
