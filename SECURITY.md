# Security and Safety

This document covers vulnerability reporting and hardware safety. It is not a guarantee of fitness for any purpose.

## Supported versions

The firmware does not carry a version string at this time. The supported version is the current `HEAD` of the repository.

After the team adopts version tagging, this section should be updated to list supported tags.

## Reporting a vulnerability

Reports must come from someone authorised to act on behalf of the OdyssiX team (this file does not commit to a public reporting address).

Until a public contact is published, reports should be filed as private issues with the maintainer of the repository.

## Trust boundary

The firmware operates inside a single vehicle. It receives commands from a Jetson over a USB-CDC bridged UART (USART2 at 460 800 baud). It exposes:

- One outbound topic: `/microros/imu_raw`.
- One inbound topic: `/control/command`.

The firmware does not implement authentication on the command channel. Any host that can connect to the same UART, speak the configured XRCE-DDS session at domain ID 42, and produce `bfmc_msgs::ControlCommand` messages can drive the actuators.

This is acceptable for a closed competition venue. It is not acceptable for any deployment where the host link could be reached by an untrusted party.

## Hardware safety

### ESC

The ESC mapping in `Core/Src/freertos.c:421-431` translates `target_cmd.brake = 1.0` to `PWM = 2000 µs`. For the Quicrun ESC the firmware was bench-calibrated against, this PWM value corresponds to full reverse throttle, not "maximum braking force."

The Jetson safety supervisor caps brake at 0.5 (see workspace audit). The firmware does **not** cap brake at the source. If the Jetson cap is removed, full reverse is possible.

Operational safety recommendations:

- Keep the Jetson `max_brake` cap below 0.6.
- Keep wheels off the ground during bring-up.
- Verify ESC arms with neutral PWM before any throttle is commanded.

### Steering servo

PWM range is clamped to [1000, 2000] µs in `steer_angle_to_pwm` (`Core/Src/freertos.c:66-72`). The mechanical steering linkage must be verified to absorb both extremes without binding.

### Power

The Nucleo can be powered from USB (5 V from the host) or VIN. The ESC powers the steering servo via its BEC output (typically 5 V). All grounds must be common. Mixing power sources without a common ground is a known cause of glitchy PWM.

### Boot ordering

The firmware writes TIM2 CCR1 and CCR2 to 1500 µs immediately after `HAL_TIM_PWM_Start` (`Core/Src/freertos.c:213-214`). Power the STM32 first, then power the ESC. If the ESC sees PWM other than ~1500 µs at its power-on, arming may fail.

## PWM safety properties

- TIM2 is configured for 50 Hz output (`Core/Src/tim.c:45-47`).
- CCR values map directly to microseconds because the counter clock is 1 MHz.
- During a fault, Cortex-M fault handlers in `Core/Src/stm32f4xx_it.c` call `NVIC_SystemReset()`. After reset, the boot path re-applies the 1500 µs neutral latch.
- `Error_Handler` in `Core/Src/main.c` does **not** reset. It hangs with interrupts disabled. TIM2 continues to output the last CCR value. This is a known limitation; see the "Known safety gaps" section below.
- IWDG is present in source but disabled at boot (`Core/Src/main.c`). This is a known deployed behaviour; rationale in `docs/design_decisions/15_iwdg_disabled.md`.

## Unsafe modification examples

The following modifications would compromise safety:

- Removing the `last_cmd_time_ns == 0` check in the deadman (`Core/Src/freertos.c:413`). On cold boot before the first `/control/command`, PWM would not be forced neutral; the last CCR value at boot would persist until the first command arrived.
- Removing the boot-time CCR latch (`Core/Src/freertos.c:213-214`). PWM would not be brought to 1500 µs until the deadman ran its first iteration after calibration. During the 2-second calibration window, the PWM state would not be guaranteed.
- Raising the deadman threshold above ~1 s without verifying the Jetson autonomy stack's stale-command behaviour. The current 500 ms deadman is chosen larger than the Jetson supervisor's stale window but smaller than any reasonable race-condition window.
- Changing the ESC brake mapping ceiling above 2000 µs.
- Changing the steering PWM clamp range outside [1000, 2000] µs.
- Disabling the Cortex-M fault `NVIC_SystemReset()` calls in `Core/Src/stm32f4xx_it.c`.
- Disabling the quaternion sanity gate (`Core/Src/bno085.c:193, 223`).

## Deployment cautions

- Do not flash modified firmware to a vehicle without bench testing on a stand.
- Do not deploy a firmware build to a competition without verifying PA5 LED at ~10 Hz, `/microros/imu_raw` rate at ~100 Hz, and `/control/command` round-trip behaviour.
- Do not power the Jetson before the STM32 is at PWM neutral.
- Do not skip the BNO085 calibration window. The 2-second window assumes the vehicle is stationary.

## Known safety gaps

The following deployed behaviours are documented here so contributors do not introduce regressions while attempting changes.

- IWDG is configured in source but not started at boot. Software hangs without a Cortex-M fault are not auto-recovered.
- `Error_Handler` hangs with interrupts disabled. Peripheral init failures freeze TIM2 at the last CCR value.
- `configENABLE_FPU = 0` while the C compiler uses `-mfloat-abi=hard`. Safe under the single-task architecture; a future second task using floats would require setting `configENABLE_FPU = 1` to avoid corrupting FPU state across context switches.

Rationale and trade-offs for each are in `docs/design_decisions/` and `docs/tradeoffs/`.
