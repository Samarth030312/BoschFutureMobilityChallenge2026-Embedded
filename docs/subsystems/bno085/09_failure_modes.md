# BNO085 — 09: Failure Modes

Per-failure-mode catalogue. Cross-referenced from `docs/failure_analysis/fmea.md` for the full FMEA table.

| ID | Failure | Cause | Detection | Recovery | Severity (S, 1-5) | Likelihood (L, 1-5) | RPN |
|---|---|---|---|---|---|---|---|
| BNO-1 | BNO085 silent at boot | Wiring, dead chip, ADR wrong | `BNO085_PerformStartupCalibration` returns HAL_ERROR | **Discarded by caller** — silent gap | 4 | 2 | 8 |
| BNO-2 | BNO085 silent at runtime | Cable disconnect, I²C wedge | 50-error auto-recovery | Re-init attempt; may not unstick a hardware wedge | 3 | 2 | 6 |
| BNO-3 | EMI-corrupted quaternion (BLDC) | Motor noise | Quat sanity gate `|q|² ∈ [0.9, 1.1]` | Drop sample, keep last good | 3 | 4 (observed) | 12 |
| BNO-4 | EMI-corrupted accel or gyro | Same | None — no sanity gate on accel/gyro | Corrupted sample propagates | 3 | 3 | 9 |
| BNO-5 | Back-to-back packet edge-miss | INTN stays LOW across two packets | Flag-OR-pin-LOW gate | Pin-LOW fallback triggers next iteration read | 3 | 5 (would happen frequently) | **mitigated by design** |
| BNO-6 | Unknown SH-2 report ID (firmware update) | New BNO085 firmware emits new report | `BNO085_ReportLen` returns 0 → walker breaks | Reports parsed so far accepted; rest dropped | 2 | 1 | 2 |
| BNO-7 | Truncated cargo (bus error mid-transfer) | I²C interruption | HAL second-phase read returns error | err_count++ | 2 | 1 | 2 |
| BNO-8 | Length field corrupt (0 or 0x7FFF) | Bus noise | First-phase check `(packet_len == 0 \|\| 0x7FFF)` | Return HAL_ERROR; err_count++ | 2 | 1 | 2 |
| BNO-9 | Length > 128 (out-of-range) | Bus corruption or BNO085 anomaly | Cap to `sizeof(rx_buf)`; parser bounds-checks per-report | Reads what fits; parser handles | 2 | 1 | 2 |
| BNO-10 | I²C bus wedged (SDA held LOW) | Slave dies mid-transaction | HAL timeout (100 ms × 2) | Auto-recovery re-init does NOT unstick; needs SDA clock pulses (not implemented) | 4 | 1 | 4 (mitigated only by IWDG, currently the IWDG-disabled state disabled) |
| BNO-11 | Calibration sample variance high (vehicle moved during 2 s) | Operator error | None — no variance check | Wrong baseline → published yaw is offset | 3 | 2 | 6 |
| BNO-12 | Calibration captures pre-rest sample | EXTI flag not pre-cleared | `bno085_int_flag = false` before loop | Mitigated by design | 2 | 1 | 2 |
| BNO-13 | Hamilton product produces non-unit quat | Float drift | Unit-norm renorm before publish (`freertos.c:373-383`) | sqrtf + reciprocal | 2 | 5 (always happens at ~1e-7 drift) | mitigated by design |
| BNO-14 | Hamilton produces (0,0,0,0) | Both inputs degenerate | `q_mag > 1e-6f` guard prevents division | (0,0,0,0) published; Jetson rejects via its own quat_norm_ok gate | 3 | 1 | 3 |
| BNO-15 | Stale flag from previous boot | Wouldn't happen — `bno085_int_flag` is BSS-zeroed on reset | N/A | N/A | 1 | 0 | 0 |
| BNO-16 | Auto-recovery loops indefinitely (BNO085 stays broken) | Hardware fault | err_count thrashes 0→50→re-init→0→50→... | No bound; loop continues | 3 | 1 | 3 |
| BNO-17 | INTN pull-up insufficient (cable too long) | External hardware | EXTI fires spuriously OR INTN line never rises after de-assert | Symptoms: occasional missed packets OR continuous reads | 3 | 2 | 6 |

## High-RPN items (≥10)

### BNO-3 — EMI quaternion corruption (RPN 12)

Field-observed during competition. Mitigation chain:
- Firmware: quat sanity gate drops corrupt samples.
- Firmware: yaw covariance widened to 1e-2 rad² (from 1e-3).
- Jetson: adaptive R scales R_eff with confidence; drift_estimator's lane-derived correction backs out the residual drift.

Combined, the chain handles it. Single-layer alone would not.

### Mitigated-by-design (no RPN needed)

- BNO-5: race-free gate eliminates the back-to-back miss.
- BNO-12: pre-clear before calibration eliminates stale-edge sampling.
- BNO-13: float-drift renorm eliminates downstream NaN.

## Items that should be raised on RPN

### BNO-1 — Silent calibration failure (RPN 8 → should be 12)

The caller (`freertos.c:266`) discards `HAL_ERROR` from `BNO085_PerformStartupCalibration`. Severity should be 5 not 4 (raw-bias data is unbounded; can drive the vehicle off-track in the wrong direction). Likelihood 2 → 3 if you consider startup transients with wonky cabling.

Recommendation: **fail loud.** See `06_calibration.md` recommendations.

### BNO-4 — EMI corruption of accel/gyro (RPN 9)

No sanity gate on accel or gyro. The quaternion sanity gate catches EMI in the quat field but accel/gyro can still be corrupt — they're emitted in the same packet that contains the corrupt quat.

Recommendation: gate accel by `|accel| < 30 m/s²` (anything beyond is non-physical for this vehicle) and gyro by `|gyro| < 35 rad/s` (~2000 °/s, well above any plausible vehicle rotation). Simple bounds checks. Currently NOT done.

### BNO-10 — Wedged I²C bus (RPN 4 nominal, but unrecoverable)

If the BNO085 dies mid-transaction holding SDA LOW, the firmware cannot recover without IWDG reset (currently the IWDG-disabled state disabled) or operator intervention.

Recommendation: implement SDA-pulse-recovery in `BNO085_Init` (clock SCL 9 times if SDA is LOW). Standard pattern; ~30 lines.

## Recovery time matrix

| Scenario | Detection time | Recovery time | Visible to operator? |
|---|---|---|---|
| Single NACK | next iteration | next iteration | No |
| Brief I²C glitch | ~50 ms (err_count accumulates) | next successful read | No |
| Sustained errors → auto-recovery | 500 ms | ~50 ms re-init | No (LED still toggles) |
| EMI quat corruption | per-sample | per-sample | No |
| Wedged bus | 500 ms (then auto-recovery thrashes) | Never (without IWDG) | LED still toggles (misleading) |
| Calibration fail | 2 s | None | No (LED still toggles) |

The LED's unconditional 10 Hz toggle is the most worrying property here: **it gives no signal that anything is wrong.** A future cycle should encode IMU health into the LED pattern.
