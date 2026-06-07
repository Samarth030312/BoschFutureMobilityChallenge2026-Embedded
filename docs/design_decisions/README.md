# Engineering Decisions Book

**Generated:** 2026-06-08 | **Phase:** 5

This book documents major architectural decisions visible in the current firmware. Each decision is described as it stands in the current source — what was chosen, what the source implies the team considered, the benefits, the drawbacks, and what a future redesign might address.

**Discipline:** every claim is anchored to current source. We do not editorialize about prior states the source no longer reflects.

| # | File | Decision |
|---|---|---|
| 01 | `01_stm32f401re.md` | Why STM32F401RE |
| 02 | `02_bno085.md` | Why BNO085 IMU |
| 03 | `03_freertos.md` | Why FreeRTOS |
| 04 | `04_microros.md` | Why micro-ROS |
| 05 | `05_xrce_dds_uart.md` | Why XRCE-DDS over UART |
| 06 | `06_baud_460800.md` | Why 460 800 baud |
| 07 | `07_quicrun_esc.md` | Why the Quicrun ESC + PWM mapping |
| 08 | `08_custom_allocator.md` | Why the custom micro-ROS allocator |
| 09 | `09_hsi_clock.md` | Why HSI internal clock |
| 10 | `10_single_task.md` | Why single-task architecture |
| 11 | `11_polled_i2c.md` | Why polled HAL I²C (not DMA) |
| 12 | `12_blocking_uart_tx.md` | Why blocking UART TX |
| 13 | `13_fault_reset.md` | Why fault handlers call NVIC_SystemReset |
| 14 | `14_rotation_vector_05.md` | Why Rotation Vector (0x05) report |
| 15 | `15_iwdg_disabled.md` | Why IWDG is currently disabled |
