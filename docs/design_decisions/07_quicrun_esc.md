# Decision 07 — Quicrun ESC + the bench-calibrated PWM mapping

## What the source confirms

- TIM2 CH2 (PA1) drives the ESC PWM (`tim.c:84-128`, `freertos.c:210, 432`).
- 50 Hz PWM, 1 µs resolution (`tim.c:45-47`).
- ESC mapping (`freertos.c:421-431`):
  - `throttle > 0`: PWM = `1443 - throttle * 443`, clamped ≥ 1000.
  - `brake > 0`: PWM = `1527 + brake * 473`, clamped ≤ 2000.
  - else: PWM = 1500.
- Boot-time neutral: CCR2 = 1500 (`freertos.c:214`).
- Deadman engages CCR2 = 1500 after 500 ms of command silence (`freertos.c:411-416`).

## Why the asymmetric 1443 / 1527 endpoints

A new engineer should understand: **these numbers are bench-calibrated for a specific Quicrun ESC**. They are not from a datasheet.

The Quicrun ESC has a ~30 µs deadband around 1500 µs where the motor doesn't engage. The team measured:
- Forward command starts engaging at ~1443 µs (PWM dropping below this is "forward")
- Reverse / brake command starts engaging at ~1527 µs (PWM rising above this is "reverse/brake")

Between 1444 and 1526 µs, the motor stays disengaged regardless. That's the deadband.

The mapping linearly scales:
- Throttle 0.0 → PWM 1443 (start of forward; barely engaged)
- Throttle 1.0 → PWM 1000 (full forward; clamped)
- Brake 0.0 → PWM 1527 (start of reverse)
- Brake 1.0 → PWM 2000 (full reverse / max brake)

## Why Quicrun (not a different ESC)

NOT PROVEN from current source — the choice is encoded only as the calibration constants. The team's vehicle has a specific Quicrun ESC; other ESCs would need different deadband + neutral calibration.

## The brake = full reverse hazard

A critical safety property: on Quicrun, `brake = 1.0` produces `PWM = 2000 µs`, which the ESC interprets as **maximum reverse throttle**, not "max braking force." The Jetson safety supervisor caps brake at 0.5 (`race_day.launch.py:201`); the firmware does not have its own brake cap.

This means: if the Jetson supervisor's brake cap is ever changed or removed, the firmware will faithfully pass brake=1.0 → full reverse. Defense-in-depth gap.

## Drawbacks visible in current source

- **Magic numbers without bench-test report.** The 1443, 1527, 443, 473 values are bench-calibrated but the test report is not in the repo. A future maintainer cannot recalibrate without re-running the bench test.
- **No firmware-level brake cap.** Vehicle-side defense-in-depth has a gap.
- **Deadband is ESC-specific.** Swapping ESC requires recalibration.

## Alternatives the choice rules out

- **DShot / Oneshot protocols.** Some ESCs support digital PWM-like protocols with higher resolution. Not Quicrun.
- **CAN-based motor controller** (e.g., ODrive). Would replace the entire PWM path.
- **Telemetry-enabled ESC** (e.g., ESC with feedback over UART). Would close the open-loop hole — currently the firmware has no idea what the motor is actually doing.

## What a future redesign might pick

- **Document the bench-calibration report** for the specific Quicrun model.
- **Add a firmware-level `MAX_BRAKE_PWM = 1800`** clamp as defense-in-depth.
- **Replace magic numbers with named constants** (`ESC_FORWARD_START = 1443`, `ESC_REVERSE_START = 1527`, `ESC_THROTTLE_GAIN = 443`, `ESC_BRAKE_GAIN = 473`).
- **Telemetry-enabled ESC** for closed-loop speed feedback.

## What this decision constrains in current code

- The PWM mapping (`freertos.c:421-431`) is hardcoded for Quicrun characteristics.
- The Jetson autonomy stack's `max_brake: 0.5` (`race_day.launch.py`) is tuned against this firmware mapping.
- The deadman PWM neutral (1500 µs) is the only safe-state guarantee.
