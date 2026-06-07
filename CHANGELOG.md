# Changelog

## v1.0.0-bfmc-2026

Initial public release of the OdyssiX STM32 firmware as deployed on the BFMC competition vehicle.

The release contains:

- The full STM32F401RE firmware source.
- The build system (`Makefile`, linker scripts, STM32CubeMX project descriptor).
- Architecture, subsystem, design-decision, and trade-off documentation.
- A failure-modes-and-effects analysis.
- An engineering-constants reference cataloguing every load-bearing magic number in source with file/line citations.
- A wiring guide under `hardware/`.

The repository reflects the firmware that ran on the vehicle. Known limitations are listed in the README and `SECURITY.md`.

Format: Keep a Changelog (loose). Versions follow `vMAJOR.MINOR.PATCH-bfmc-YEAR`.
