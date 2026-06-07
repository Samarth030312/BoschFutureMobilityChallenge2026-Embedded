# Decision 15 — IWDG is currently disabled 

## What the source confirms

`Core/Src/main.c`:

- IWDG infrastructure exists (lines 75-95):
  - Direct-register macros: `IWDG_KEY_UNLOCK = 0x5555`, `IWDG_KEY_RELOAD = 0xAAAA`, `IWDG_KEY_START = 0xCCCC`, `IWDG_PR_DIV32 = 3`, `IWDG_RLR_2000MS = 2000`.
  - `IWDG_Init_Direct()` is defined with `__attribute__((unused))`.
  - `IWDG_Refresh_Direct()` is exported (non-static) for use from `freertos.c`.
- The call to `IWDG_Init_Direct()` at boot is **commented out** (`main.c:135-140`).
- The accompanying comment:

  > "IWDG INTENTIONALLY DISABLED at boot. Re-arm by uncommenting the line below ONLY after on-bench verification of (a) PA5 2 Hz heartbeat and (b) XRCE-DDS session establishment."

## Why the team disabled it

The team's stated rationale (verbatim from `main.c:135-140`): IWDG should be enabled only after bench-verifying the heartbeat + XRCE-DDS session work. The implication: during early bring-up, IWDG could reset the firmware before the operator could observe whether the bring-up succeeded.

This is a developer-convenience choice. Disabled IWDG = no surprise resets while bench-testing.

## Why this is a real safety gap

The fault-handler reset (Decision 13) covers Cortex-M faults but **does not cover hangs without faults**:

- A `for(;;){}` in `Error_Handler` (the Error_Handler hang behaviour already documented) — silent hang.
- A `configASSERT` failure — silent hang.
- A FreeRTOS scheduler deadlock — silent hang (theoretical with one task).
- An I²C bus wedge plus auto-recovery loop that thrashes without succeeding — slow degradation.
- An infinite loop bug in any of the team's code — silent hang.

None of these trigger a Cortex-M fault. None reset the chip. IWDG would.

## What IWDG would do if enabled

The team's pre-baked configuration (`main.c:75-79`):
- LSI (~32 kHz nominal) ÷ 32 prescaler → 1 kHz counter → 1 ms/count.
- Reload = 2000 → 2 s timeout.

So: if `IWDG_Refresh_Direct()` isn't called for 2 s, the chip resets.

The reload timeout is well above the worst-case main-loop iteration (~220 ms — see `task_graph.md`). 2 s gives substantial margin.

## What IWDG is NOT

- IWDG cannot detect "the firmware is running but doing the wrong thing." It only detects "the firmware stopped refreshing the counter."
- IWDG cannot unstick a hardware peripheral. A wedged I²C bus that triggers reset will, post-reset, re-init the I²C peripheral — which clears the STM32 side but doesn't free a slave still holding SDA LOW.

## Drawbacks of staying disabled

- **the IWDG-disabled state.** The audit's highest-RPN STM32-firmware finding.
- **No automatic hang recovery.** The operator must manually press NRST.
- **The pre-baked code is misleading.** Future readers see `IWDG_Init_Direct`, the comment block, `IWDG_Refresh_Direct` — and assume IWDG is active. Only careful reading reveals it isn't.

## Drawbacks of enabling

- **False resets if any code path exceeds 2 s without refresh.** The `BNO085_PerformStartupCalibration` blocks for 2 s with `osDelay(1)` ticks — if no refresh path exists in calibration, IWDG would reset mid-calibration. Need to add `IWDG_Refresh_Direct()` inside the calibration loop too.
- **Hard to debug.** Setting a breakpoint pauses the CPU → IWDG fires → reset. Most chips have a debug-mode bit that freezes IWDG while halted (DBG_IWDG_STOP); the team's code doesn't enable it.

## Alternatives the choice rules out

- **Window watchdog (WWDG).** Stricter (must refresh within a window, not just before timeout). More complex; rarely used for this kind of catch-all.
- **External hardware watchdog** (an external monitor chip pulling NRST). More reliable than internal IWDG (an internal-IWDG bug still hangs). Out of scope at this design's complexity.
- **No watchdog at all.** Current state.

## What a future redesign might pick

- **Enable IWDG** after bench verification. Single line uncomment + add `IWDG_Refresh_Direct()` calls in:
  - The boot calibration loop.
  - The infinite loop body.
  - The agent-ping wait loop.
- **Enable DBG_IWDG_STOP** so breakpoints don't fire IWDG.
- **Add IWDG status to a diagnostic publish.**

## What this decision constrains in current code

The disabled IWDG is currently a **known gap with a documented workaround** (commented-out enable call). The team has explicitly chosen to ship without it; `SECURITY.md` lists it under "Known safety gaps".

When re-enabled, enabling IWDG would also need:

1. The call uncommented at `main.c:139`.
2. Refresh calls added to every loop that could exceed 2 s. The boot calibration is the main one.
3. Bench verification that the firmware survives 60 s without spurious reset.

Until then, the safety floor is the Cortex-M fault reset + the FreeRTOS deadman + the boot-time PWM neutral.
