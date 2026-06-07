# Control System (Servo + ESC)

**Generated:** 2026-06-08 | **Phase:** 7

## TIM2 PWM configuration

| Setting | Value | Source |
|---|---|---|
| Timer | TIM2 (32-bit timer on APB1) | `tim.c:44` |
| Clock source | Internal (APB1 timer clock = 84 MHz) | `tim.c:54` |
| Prescaler | 84 − 1 = 83 | `tim.c:45` |
| Counter clock | 84 MHz / (83 + 1) = 1 MHz → 1 µs/count | derived |
| Period | 20000 − 1 = 19999 | `tim.c:47` |
| PWM period | 20000 × 1 µs = 20 ms | derived |
| **Frequency** | **50 Hz** | derived |
| Counter mode | Up | `tim.c:46` |
| AutoReloadPreload | Disable | `tim.c:49` |
| OC mode | PWM1 (CCR controls high time) | `tim.c:69` |
| OC polarity | High | `tim.c:71` |
| OC fast mode | Disable | `tim.c:72` |
| Initial pulse | 0 | `tim.c:70` |

## Channel mapping

| Channel | GPIO | Function | Driven |
|---|---|---|---|
| TIM2_CH1 | PA0 (alt-fn 1) | Steering servo PWM | `freertos.c:209, 213, 418` |
| TIM2_CH2 | PA1 (alt-fn 1) | ESC throttle PWM | `freertos.c:210, 214, 432` |

GPIO config: alt-fn 1, push-pull, no pull, low speed (`tim.c:113-122`).

## PWM range

- **1 000 µs** = full minimum (one extreme)
- **1 500 µs** = neutral / centre
- **2 000 µs** = full maximum (other extreme)

CCR values are direct microsecond counts (because counter clock = 1 MHz).

## Steering control

```c
static uint32_t steer_angle_to_pwm(float angle_rad)
{
  int32_t pwm = 1500 - (int32_t)(angle_rad * 1000.0f);
  if (pwm < 1000) pwm = 1000;
  if (pwm > 2000) pwm = 2000;
  return (uint32_t)pwm;
}
```
(`freertos.c:66-72`)

| Input | PWM | Direction |
|---|---|---|
| `angle_rad = -1.0` (left) | 2500 → clamped to 2000 µs | full left |
| `angle_rad = -0.5` | 2000 µs | left |
| `angle_rad = 0.0` (center) | 1500 µs | center |
| `angle_rad = +0.5` | 1000 µs | right |
| `angle_rad = +1.0` (right) | 500 → clamped to 1000 µs | full right |

Linear conversion: 1 rad = 1000 µs deviation from 1500. Net: −500 µs to +500 µs, clamped.

**Note:** the convention "positive = right" matches typical vehicle frame (positive yaw = clockwise looking down = right turn).

## ESC control (throttle + brake)

```c
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
```
(`freertos.c:421-431`)

| Input | PWM | Action |
|---|---|---|
| `throttle = 1.0` | 1443 − 443 = 1000 µs | full forward (clamped) |
| `throttle = 0.5` | 1443 − 221 = 1222 µs | partial forward |
| `throttle = 0.05` | 1443 − 22 = 1421 µs | slight forward |
| `throttle = 0`, `brake = 0` | 1500 µs | neutral |
| `brake = 0.05` | 1527 + 24 = 1551 µs | slight brake |
| `brake = 0.5` | 1527 + 237 = 1764 µs | hard brake |
| `brake = 1.0` | 1527 + 473 = 2000 µs | max brake (= max reverse per ESC characteristic) |

**The 1443 / 1527 asymmetry is the ESC's deadband.** The Quicrun ESC has a ~30 µs dead zone around 1500 µs where the motor doesn't engage. Forward command starts at 1443; reverse/brake at 1527. Anything between (1444-1526) is effectively neutral.

These constants are bench-calibrated for the specific Quicrun ESC. A different ESC would need new constants.

## Deadman

```c
int64_t age_ns = time_ns - last_cmd_time_ns;
if (last_cmd_time_ns == 0 || age_ns > 500000000LL) {
    TIM2->CCR1 = 1500u;
    TIM2->CCR2 = 1500u;
} else {
    TIM2->CCR1 = steer_angle_to_pwm(target_cmd.steering_angle);
    /* ... esc_pwm computation ... */
    TIM2->CCR2 = esc_pwm;
}
```
(`freertos.c:411-433`)

Two conditions force neutral:
- `last_cmd_time_ns == 0` — no command received since boot.
- `age_ns > 500 000 000` (500 ms) — last command is stale.

500 ms is **larger than** the Jetson safety_supervisor's `cmd_stale_s` (default ~0.25 s). The supervisor zeros first (sending `(0, 0, 0)` commands), then the STM32 deadman holds as a last resort if the supervisor itself stops publishing.

## Boot-time PWM neutral

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
TIM2->CCR1 = 1500u;
TIM2->CCR2 = 1500u;
```
(`freertos.c:209-214`)

PWM is started **before** any command can arrive (`last_cmd_time_ns == 0`). The deadman engages on the very first loop iteration. Vehicle parked until commanded.

## Timing diagram

```
Time → 0     20    40    60    80   100 ms

CH1:   ▔▔__▔▔▔____▔▔__▔▔▔____▔▔__   PWM ~1.5 ms high every 20 ms (50 Hz)
                                        (high time = steering command, 1000-2000 µs)

CH2:   ▔▔__▔▔▔____▔▔▔____▔▔▔____    PWM ~1.5 ms high every 20 ms (50 Hz)
                                        (high time = ESC command, 1000-2000 µs)
```

Both channels share the same TIM2 counter, so they're synchronised in phase. The high-time of each pulse encodes the command.

## Why 50 Hz

Standard hobby servo + ESC convention. They expect:
- Pulse width 1.0-2.0 ms.
- Pulse interval 10-20 ms (50-100 Hz).
- 50 Hz is the canonical analogue-servo standard.

Higher frequency (e.g., 200 Hz) would enable faster control loops but most low-cost hobby ESCs don't support it.

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| ESC brake = 1.0 produces full reverse (Quicrun bug) | High at competition | Jetson supervisor caps `max_brake = 0.5` (`race_day.launch.py:201`); firmware ESC mapping at brake=1.0 still produces 2000 µs (full extreme), which is "max reverse" on Quicrun |
| TIM2 not started → no PWM signal | High | Boot init starts; failure = `Error_Handler`  |
| PWM stuck at non-neutral after fault | High | Cortex-M fault handlers `NVIC_SystemReset` → reboot → boot-time 1500 µs |
| Servo/ESC misconnected | Medium | Wiring guide (see `hardware/`) |
| TIM2 overflow during long uptime | Negligible | Period is 20 000; ARR resets every period |
| PWM at 50 Hz while ESC expects different | Low | All known Quicrun models support 50 Hz |

## Recommendations (NOT MADE)

1. **Document the bench-test constants** (1443, 1527, 443, 473) with the ESC model and bench-test date next to them.
2. **Define named constants** (`#define ESC_THROTTLE_START 1443`, `#define ESC_DEADBAND_HIGH 1527`, etc.) for clarity.
3. **Add a brake-clamp at firmware level** as defense-in-depth (e.g., never produce CCR2 > 1800 µs even if the Jetson supervisor's clamp is broken).
4. **Bench-test calibration once per cycle** and check in a `hardware/esc_calibration.md` document.

## Reference

- Quicrun ESC manuals (varies by model).
- Hobby servo PWM standards (50 Hz, 1-2 ms pulse).
- The bench-test numerical constants are in `freertos.c:421-431`.
