# OdyssiX STM32 Firmware

STM32F401RE firmware for the OdyssiX BFMC competition vehicle. Reads a BNO085 IMU at 100 Hz, publishes an IMU topic over micro-ROS, receives control commands from a Jetson host, and drives a steering servo and a Quicrun ESC via TIM2 PWM. Includes a 500 ms command-silence deadman that returns PWM to neutral.

## Hardware

| Item | Notes |
|---|---|
| STM32F401RE Nucleo (NUCLEO-F401RE) | Cortex-M4F at 84 MHz. 96 KiB RAM, 512 KiB FLASH. |
| BNO085 IMU breakout | I²C interface. ADR pin tied LOW selects I²C address `0x4A`. |
| Steering servo | Standard 50 Hz analogue servo, pulse range 1000–2000 µs. |
| Quicrun ESC | Bench-calibrated mapping (`docs/subsystems/control/README.md`). A different ESC requires recalibration. |
| 3S LiPo battery (or equivalent) | Powers ESC. Nucleo can be powered from USB during development. |
| Jetson host running ROS 2 Humble | Source side of the micro-ROS agent. Not part of this repository. |
| USB cable (Nucleo to Jetson) | The Nucleo's ST-LINK USB port exposes USART2 over USB-CDC. |

A 3D model, schematic, or PCB layout is not included in this repository.

## Wiring

See `hardware/system_connections.md` for the full pin map.

Summary:

| Pin | Purpose |
|---|---|
| PB8, PB9 | I²C1 SCL, SDA — to BNO085 |
| PB4 | EXTI input — BNO085 INTN |
| PA0 | TIM2_CH1 PWM — steering servo signal |
| PA1 | TIM2_CH2 PWM — ESC signal |
| PA2, PA3 | USART2 TX, RX — to Jetson via Nucleo USB-CDC bridge |
| PA5 | LED — 10 Hz heartbeat |

Grounds must be common across STM32, BNO085, servo, ESC, and any external power. The BNO085 INTN line is open-drain active-low; an external pull-up of 4.7–10 kΩ is recommended for cable runs longer than ~10 cm.

## Architecture

- Single FreeRTOS task (`microROSTask`) runs the application.
- 1 kHz cooperative main loop: executor spin → BNO085 read (when interrupt fires) → publish → deadman + PWM update.
- ISR layer is minimal: BNO085 INTN sets a flag, UART RX pushes one byte into a 2 KiB lock-free ring, TIM1 advances the HAL tick.
- Defense in depth for actuator safety: Cortex-M fault handlers reset to a known boot-time PWM-neutral state; the deadman returns PWM to 1500 µs on 500 ms of command silence.

The full architecture walk-through, including Mermaid diagrams, is in `docs/architecture/`.

## Build

Requires:

- `arm-none-eabi-gcc` 10.3 or later.
- GNU Make.
- The micro-ROS STM32CubeMX utilities (external dependency, see below).

```sh
make
```

Output goes to `build/`:

- `build/odyssix_stm32_core.elf`
- `build/odyssix_stm32_core.hex`
- `build/odyssix_stm32_core.bin`

### micro-ROS dependency

The repository expects the user to provide the micro-ROS STM32CubeMX utilities as an external dependency. The `Makefile` references:

- Headers from `micro_ros_stm32cubemx_utils/microros_static_library/libmicroros/microros_include`.
- The static library at `micro_ros_stm32cubemx_utils/microros_static_library/libmicroros/libmicroros.a`.

Clone the upstream into the repository root:

```sh
git clone https://github.com/micro-ROS/micro_ros_stm32cubemx_utils.git
```

`libmicroros.a` is not vendored. Build it from the upstream using the instructions in `micro_ros_stm32cubemx_utils/README.md` for the STM32F401RE target. The upstream's typical flow uses a Docker container that produces the static library and headers for the target's `-mcpu`, `-mfpu`, and `-mfloat-abi` flags. The Makefile prints those flags via `make print_cflags`.

## Flash

Via ST-LINK (built into the Nucleo):

```sh
st-flash write build/odyssix_stm32_core.bin 0x08000000
```

Or use STM32CubeProgrammer. Press the black NRST button on the Nucleo, or unplug and replug USB, to start the new firmware.

## Connect to Jetson

The Nucleo's USB connects to the Jetson host. On the Jetson, the link enumerates as `/dev/ttyACM*`.

The micro-ROS agent (not provided in this repository) must be started on the Jetson and given the device path:

```sh
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0 --baudrate 460800
```

ROS domain ID 42 must match between the firmware and the Jetson host. The firmware uses domain 42 (set in `Core/Src/freertos.c`). The Jetson's `ROS_DOMAIN_ID` environment variable must equal 42 on the same network namespace.

## Operation

After flashing and powering:

1. PA5 LED toggles at ~10 Hz within ~1 s of reset.
2. TIM2 PWM at PA0 and PA1 holds 1500 µs.
3. The firmware waits for the micro-ROS agent. PWM stays at neutral throughout.
4. Once the agent connects: session sync, entity creation, and a 2-second BNO085 calibration window. The vehicle must be still during calibration.
5. The main loop publishes `/microros/imu_raw` at ~100 Hz and subscribes to `/control/command`.

## Verification

On the Jetson:

```sh
# Topic discovery
ros2 topic list | grep microros

# Publish rate
ros2 topic hz /microros/imu_raw      # expect ~100 Hz

# Echo (verify non-zero quaternion)
ros2 topic echo /microros/imu_raw --once

# Drive a command
ros2 topic pub --once /control/command bfmc_msgs/ControlCommand \
  "{steering_angle: 0.0, throttle: 0.0, brake: 0.0}"
```

PA0 and PA1 PWM should remain at 1500 µs in response to the zero command. A scope at PA0 and PA1 should show 50 Hz pulses with width matching the commanded values.

## Known limitations

- IWDG is configured in source but not started at boot (`Core/Src/main.c`). Software hangs without a Cortex-M fault are not auto-recovered. Rationale and trade-off in `docs/design_decisions/15_iwdg_disabled.md`.
- `Error_Handler` hangs with interrupts disabled rather than resetting. Peripheral init failure leaves TIM2 at its last CCR value. Documented in `SECURITY.md`.
- USART1 init and USART2 RX DMA are present in the CubeMX-generated scaffolding without active consumers. They have no functional impact.
- The Quicrun ESC mapping translates `brake = 1.0` to a PWM value the ESC interprets as full reverse. The Jetson safety supervisor caps brake at 0.5; the firmware itself does not. Operational safety recommendations are in `SECURITY.md`.
- `configENABLE_FPU = 0` while the C compiler uses `-mfloat-abi=hard`. Safe under the single-task architecture; a future second FPU-using task would require setting `configENABLE_FPU = 1`. Note in `docs/design_decisions/10_single_task.md`.

## Documentation

For everything beyond this README, start at `PROJECT_KNOWLEDGE_INDEX.md`.

Suggested reading order for a first-time reader:

1. `docs/architecture/system_overview.md` — what the firmware is and what it does.
2. `docs/architecture/boot_sequence.md` — the boot path stage by stage.
3. `docs/subsystems/bno085/README.md` — IMU integration index.
4. `docs/subsystems/control/README.md` — TIM2 PWM, steering and ESC mapping.
5. `SECURITY.md` — safety properties and unsafe-modification examples.
6. `docs/design_decisions/` — rationale for architectural choices.
7. `docs/tradeoffs/` — comparative analyses of alternatives.

## Contributing

See `CONTRIBUTING.md` for the branching model, commit conventions, documentation requirements, and review expectations.

## License

Apache License, Version 2.0. See `LICENSE`.

Vendor code retains its original license terms:

- `Drivers/CMSIS/` — Apache 2.0 (see `Drivers/CMSIS/LICENSE.txt`).
- `Drivers/STM32F4xx_HAL_Driver/` — BSD-3-Clause (see `Drivers/STM32F4xx_HAL_Driver/LICENSE.txt`).
- `Middlewares/Third_Party/FreeRTOS/` — MIT (see `Middlewares/Third_Party/FreeRTOS/Source/LICENSE`).
- `micro_ros_stm32cubemx_utils/` (external, user-provisioned) — Apache 2.0 from the upstream.

See `AUTHORSHIP.md` for the first-party / third-party split.
