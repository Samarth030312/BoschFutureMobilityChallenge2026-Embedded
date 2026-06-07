# Project Knowledge Index

Central navigation for this repository.

## Source files

| Path | Contents |
|---|---|
| `Core/Inc/` | First-party headers and CubeMX peripheral headers |
| `Core/Src/` | Application source, custom heap, transport, CubeMX peripheral scaffolding |
| `Core/Startup/startup_stm32f401retx.s` | Vector table and `Reset_Handler` |
| `Drivers/` | STMicroelectronics CMSIS and HAL F4 (vendor) |
| `Middlewares/Third_Party/FreeRTOS/` | FreeRTOS 10.3.1 kernel (vendor) |
| `Makefile` | Build configuration |
| `STM32F401RETX_FLASH.ld` | Active linker script |
| `odyssix_stm32_core.ioc` | STM32CubeMX project descriptor |

## Root documents

| File | Purpose |
|---|---|
| `README.md` | Project overview, build, flash, operation, verification |
| `PROJECT_KNOWLEDGE_INDEX.md` | This file |
| `AUTHORSHIP.md` | First-party vs third-party code ownership |
| `CONTRIBUTING.md` | Branching, commits, review, documentation requirements |
| `SECURITY.md` | Safety properties and unsafe-modification examples |
| `CHANGELOG.md` | Release history |
| `LICENSE` | Apache 2.0 |

## Documentation

### Architecture (`docs/architecture/`)

- `system_overview.md` — what the firmware is and how it is organised
- `boot_sequence.md` — reset to first IMU publish, stage by stage
- `interrupt_graph.md` — ISRs, priorities, shared variables, race analysis
- `task_graph.md` — FreeRTOS tasks, timing, stack and CPU estimates
- `memory_graph.md` — RAM layout, three-heap model
- `memory_layout.md` — linker-script view of FLASH and RAM
- `data_flow_graph.md` — end-to-end data flow
- `deployment_architecture.md` — how the firmware fits into the vehicle

### Subsystems (`docs/subsystems/`)

- `bno085/` — IMU integration, SHTP, SH-2, calibration, quaternion pipeline, covariance, error handling, failure modes
- `freertos/README.md` — FreeRTOS configuration
- `micro_ros/README.md` — micro-ROS bring-up and transport
- `memory/README.md` — three-heap model
- `control/README.md` — TIM2 PWM, steering and ESC mapping

### Engineering decisions and trade-offs

- `docs/design_decisions/` — 15 numbered architectural decisions
- `docs/tradeoffs/` — 10 comparative trade-off analyses

### Reference

- `docs/engineering_constants.md` — every magic number in source with file/line citation
- `docs/failure_analysis/fmea.md` — FMEA

### Hardware

- `hardware/system_connections.md` — pin map, wiring, power tree

## Reading paths

| Audience | Suggested path |
|---|---|
| New contributor | `README.md` → `docs/architecture/system_overview.md` → `docs/architecture/boot_sequence.md` → `SECURITY.md` |
| Hardware integrator | `README.md` → `hardware/system_connections.md` → `docs/subsystems/control/README.md` |
| Safety reviewer | `SECURITY.md` → `docs/failure_analysis/fmea.md` |
| Reader studying the architecture | `docs/architecture/system_overview.md` → `docs/design_decisions/` → `docs/tradeoffs/` |
