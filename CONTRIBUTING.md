# Contributing

This document defines the expected process for contributions to this repository. It is intentionally short.

## Branching

- `main` is the integration branch and reflects what is deployed to the vehicle.
- Feature work happens on short-lived branches named `topic/<short-description>`.
- Bug-fix branches are named `fix/<short-description>`.
- Documentation-only branches are named `docs/<short-description>`.

## Commits

Each commit:

- Has a single subject line under 72 characters.
- Has a body explaining **why**, not **what** (the diff shows what).
- References related issues by ID where applicable.

Avoid commit messages like "final checks" that do not describe the change.

## Documentation requirements

Every source change that affects observable behaviour requires a documentation update in the same pull request. Specifically:

| If the change touches… | Update… |
|---|---|
| A constant in source | `docs/engineering_constants.md` |
| The ISR layer | `docs/architecture/interrupt_graph.md` |
| The boot path | `docs/architecture/boot_sequence.md` |
| Memory layout | `docs/architecture/memory_layout.md` and `memory_graph.md` |
| The BNO085 driver | The relevant file in `docs/subsystems/bno085/` |
| The control mapping | `docs/subsystems/control/README.md` |
| The transport | `docs/architecture/data_flow_graph.md` |
| Any architectural decision | A new file in `docs/design_decisions/` |

A pull request that changes behaviour without updating the relevant documentation should be rejected.

## Review expectations

At minimum one reviewer who is not the author. The reviewer is expected to:

- Read the diff against the cited file lines in the documentation.
- Confirm that any new constants are added to `docs/engineering_constants.md`.
- Confirm that any new ISR is added to `docs/architecture/interrupt_graph.md`.
- Confirm that the change does not violate properties listed in `SECURITY.md` under "Unsafe modification examples."
- Confirm that the change preserves the safety-floor behaviours: fault-handler reset, deadman, boot-time PWM neutral.

## Testing expectations

There are currently no automated tests in this repository.

For changes that affect observable behaviour, the contributor should bench-test the firmware on a stand with wheels off the ground and confirm:

- PA5 LED toggles at ~10 Hz within ~1 s of reset.
- TIM2 PWM outputs at PA0 and PA1 are at 1500 µs after boot and during the deadman state.
- `/microros/imu_raw` is visible on the Jetson at ~100 Hz once the micro-ROS agent is up.
- `/control/command` is acknowledged by observable PWM changes.

Document the test sequence in the pull request body.

## Requirements for firmware modifications

The following modifications require additional rigour:

| Modification class | Required actions |
|---|---|
| Changing TIM2 PWM mapping (steering or ESC) | Bench measurement of PWM at a scope; recalibration report committed to `hardware/` |
| Changing the deadman threshold | Demonstrate that the new threshold is consistent with the Jetson supervisor's stale-command window |
| Changing BNO085 report set | Update `docs/engineering_constants.md` and `docs/subsystems/bno085/04_report_configuration.md`; verify the Jetson EKF still consumes the published covariance values |
| Changing micro-ROS topic names | Coordinate with the Jetson side; update both repositories together |
| Changing FreeRTOS configuration | Verify no fields touched have downstream effects on the FPU context-save state |
| Enabling IWDG | Verify the boot calibration loop refreshes IWDG; verify `DBG_IWDG_STOP` so breakpoints don't fire IWDG |

## What contributions are explicitly out of scope

The following are not in scope for routine pull requests. They require explicit discussion before being attempted:

- Adding a second FreeRTOS task. Coupled with the FPU context-save state.
- Replacing the custom heap with a different allocator.
- Switching from micro-ROS to a different transport.
- Adding sensors not currently in the bill of materials.

If the team's needs require any of these, open an architectural discussion in advance.

## Style

- Source files in `Core/Src` and `Core/Inc` preserve the `USER CODE BEGIN/END` markers. CubeMX may regenerate code outside these markers. Any user code outside the markers will be silently overwritten by a CubeMX regeneration.
- Documentation conventions: file:line citations, plain English, no marketing language, no fabricated measurements.

## License

Contributions are accepted under the project's Apache License, Version 2.0. See `LICENSE`.
