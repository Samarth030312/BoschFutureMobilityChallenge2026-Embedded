# Tradeoff 09 — IWDG enabled vs disabled

## Chosen path (current source)

**Disabled** . Infrastructure exists (`main.c:75-95`); call to `IWDG_Init_Direct()` is commented out (`main.c:135-140`).

See `design_decisions/15_iwdg_disabled.md` for the full rationale.

## Alternative considered (enabled)

Uncomment the `IWDG_Init_Direct()` call. IWDG starts on first boot. 2-second timeout. `IWDG_Refresh_Direct()` would need to be called from every loop that runs longer than ~1 s.

## Side-by-side

| Property | Disabled (chosen) | Enabled |
|---|---|---|
| Hang recovery | None (manual NRST required) | Automatic 2-second reset on any hang |
| Cortex-M fault recovery | Yes (`NVIC_SystemReset` in fault handlers) | Same plus IWDG |
| Bench debugging | No surprise resets | Breakpoint → IWDG fires → reset (unless DBG_IWDG_STOP) |
| Boot calibration safety | Calibration's 2 s loop runs fine | Calibration would need `IWDG_Refresh_Direct()` calls inside, otherwise IWDG resets mid-calibration |
| Agent ping wait safety | Long wait loop is fine | Same — but each ping iteration must refresh |
| Operational safety | Lower (hangs persist) | Higher (hangs auto-recover) |

## What the team gained by disabling

- **Debugger compatibility.** Breakpoints don't trigger spurious resets.
- **Simpler boot flow.** Calibration's 2 s loop doesn't need refresh injection.
- **No "watchdog tuning" headaches.** Don't have to think about it.

## What the team sacrificed

- **No protection against silent hangs.** The audit catalogs several hang paths (the Error_Handler hang behaviour, `configASSERT`, theoretical scheduler deadlocks). None are recovered.
- **Operational reliability gap.** A field-deployed competition vehicle that hangs is unrecoverable without operator intervention.

## When the team should switch

- **Before any public release** of the firmware. the IWDG-disabled state is the audit's highest-RPN STM32 finding.
- **Before any race-day deployment** where operator access is limited.
- **After bench verification** that the firmware survives a continuous bring-up cycle without spurious resets.

## What enabling requires

1. Uncomment `IWDG_Init_Direct();` at `main.c:139`.
2. Add `IWDG_Refresh_Direct();` to:
   - The boot calibration loop (`bno085.c:339-397` — the 2-second sample window).
   - The agent ping wait loop (`freertos.c:152-154`).
   - The infinite main loop body (`freertos.c:271-437`).
3. Enable `DBG_IWDG_STOP` (debug-mode IWDG freeze) — typically via writing to DBGMCU register at boot.
4. Bench-verify: 10 minutes of continuous operation without spurious reset.

## Current cost-benefit reading

The disabled state is a debugging-phase choice that **must not survive into deployment**. This is captured in `docs/design_decisions/15_iwdg_disabled.md` and listed under "Known safety gaps" in `SECURITY.md`. The enablement is mechanical when the team chooses to re-enable.
