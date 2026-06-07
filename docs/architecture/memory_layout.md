# Memory Layout (Phase 8 — Linker Script Forensics)

**Generated:** 2026-06-08 | **Source files:** `STM32F401RETX_FLASH.ld`, `STM32F401RETX_RAM.ld`

This document is the linker-script forensic view. For runtime memory usage, see `memory_graph.md`.

## Active linker script — `STM32F401RETX_FLASH.ld`

```
MEMORY {
  RAM   (xrw)  : ORIGIN = 0x20000000, LENGTH = 96K
  FLASH (rx)   : ORIGIN = 0x08000000, LENGTH = 512K
}
```

| Memory bank | Origin | Length | Permissions | Use |
|---|---|---|---|---|
| FLASH | 0x08000000 | 524 288 bytes | r-x (read + execute) | Program code, vector table, constants |
| RAM | 0x20000000 | 98 304 bytes | rwx (read + write + execute) | Stack, heap, statics |

## FLASH section layout (in load order)

| Section | Source / Content | Notes |
|---|---|---|
| `.isr_vector` | `g_pfnVectors` from `startup_stm32f401retx.s:126-230` | **First at 0x08000000**. Vector entry 0 = `_estack`; entry 1 = `Reset_Handler`. |
| `.text` | All code (`*(.text)`, `*(.text*)`) + `.glue_7`/`.glue_7t` (ARM↔Thumb thunks) + `.eh_frame` (debug) + `.init`/`.fini` (libc) | Followed by `_etext` symbol. |
| `.rodata` | Constants, string literals | Read-only. |
| `.ARM.extab` (READONLY) | ARM exception unwind tables | Empty for non-C++. |
| `.ARM` (READONLY) | `.ARM.exidx` (exception index) | Empty for non-C++. |
| `.preinit_array` (READONLY) | C runtime pre-init function pointers | Empty in this firmware. |
| `.init_array` (READONLY) | C++ static constructors | Empty (no C++). |
| `.fini_array` (READONLY) | C++ static destructors | Empty. |
| `.data` initial values | LOADADDR(.data), copied to RAM at boot | Followed by `_etext`-style virtual end. |

The `(READONLY)` qualifier on extab/ARM/array sections is GCC11+ only — the comment in the .ld file warns about backward compatibility.

## RAM section layout

| Section | Range | Use |
|---|---|---|
| `.data` | `_sdata` to `_edata` | Initialised globals; copied from FLASH at boot. |
| `.bss` | `_sbss` to `_ebss` (also `__bss_start__` / `__bss_end__`) | Zero-initialised globals + static arrays. |
| `._user_heap_stack` | `_end` to `_estack` | Reserves `_Min_Heap_Size + _Min_Stack_Size` for newlib heap + MSP stack. |

The `._user_heap_stack` block is a placeholder — it consumes RAM in the linker's accounting so the build fails if RAM is exhausted. At runtime, `_end` is the sbrk heap start (used by newlib `malloc`), and `_estack` is the MSP starting value (vector entry 0).

## Symbol map

| Symbol | Linker assignment | Runtime meaning |
|---|---|---|
| `_estack` | `ORIGIN(RAM) + LENGTH(RAM) = 0x20018000` | MSP starting value |
| `_Min_Heap_Size` | `0x200 = 512` | newlib heap reserve |
| `_Min_Stack_Size` | `0x400 = 1024` | MSP reserve |
| `_sidata` | `LOADADDR(.data)` (FLASH address) | source of .data init |
| `_sdata` | start of .data in RAM | destination of .data init |
| `_edata` | end of .data | one-past-end |
| `_sbss` / `__bss_start__` | start of .bss | BSS zero start |
| `_ebss` / `__bss_end__` | end of .bss | BSS zero end |
| `end` / `_end` | start of `._user_heap_stack` | newlib heap start |
| `_etext` | end of `.text` | (informational) |
| `__exidx_start`, `__exidx_end` | C++ unwind tables | empty here |
| `__preinit_array_start`, `__preinit_array_end` | preinit pointers | empty |
| `__init_array_start`, `__init_array_end` | C++ ctors | empty |
| `__fini_array_start`, `__fini_array_end` | C++ dtors | empty |

## Boot-time memory relocation

Per `startup_stm32f401retx.s:67-95`:

1. `Reset_Handler` loads `_estack` into SP.
2. Copies `.data` from `_sidata` (FLASH) to `_sdata..._edata` (RAM).
3. Zeroes `_sbss..._ebss` (.bss + COMMON).
4. Calls `SystemInit`, `__libc_init_array`, `main`.

After step 2, all `.data` is in RAM and read/write. After step 3, `.bss` is zeroed — including the 32 KB FreeRTOS heap and the 32 KB custom micro-ROS heap . This zeroing takes some boot time: 64 KB / 4-byte writes / 84 MHz ≈ ~190 µs.

## Alternative linker script — `STM32F401RETX_RAM.ld`

This script is for **RAM-only** builds (debug from RAM via stlink). Not referenced by the Makefile:

```makefile
LDSCRIPT = STM32F401RETX_FLASH.ld
```
(`Makefile:127`)

The RAM script is therefore **inactive code**. The team retains it presumably for ad-hoc debug builds; for an open-source release it should either be (a) deleted, or (b) moved to a `tools/` directory with a brief explanation.

## Build flags affecting layout

From `Makefile`:

| Flag | Effect |
|---|---|
| `-mcpu=cortex-m4` | Target architecture |
| `-mthumb` | Thumb-2 ISA |
| `-mfpu=fpv4-sp-d16` | Hardware FPU (single-precision, 16 D-registers) |
| `-mfloat-abi=hard` | Pass float args in FPU registers (the FPU context-save state consequence) |
| `-fdata-sections -ffunction-sections` | Per-symbol sections; allows `--gc-sections` to drop unused |
| `-Wl,--gc-sections` (LDFLAGS) | Drop unreferenced sections at link time |
| `-specs=nano.specs` | Use newlib-nano (smaller libc) |
| `-specs=nosys.specs` | No-OS stubs |
| `-Og` | Debug-optimised |
| `-g -gdwarf-2` (when `DEBUG=1`) | DWARF debug info |
| `-T STM32F401RETX_FLASH.ld` | Link with this script |

## Generated artefacts

| Artefact | Build target | Notes |
|---|---|---|
| `build/odyssix_stm32_core.elf` | Linked ELF | `arm-none-eabi-size` reports text/data/bss |
| `build/odyssix_stm32_core.hex` | Intel HEX | For SWD/JTAG flashing |
| `build/odyssix_stm32_core.bin` | Raw binary | For ST-LINK / DFU |
| `build/odyssix_stm32_core.map` | Linker map (via `-Wl,-Map=...,--cref`) | Cross-reference; **the source of truth for `.bss`/.data` sizing** |

The map file is not committed to the repo. Running `make` regenerates it. A useful release-prep step: commit a sample map output to demonstrate the size budget.

## Vector table

The vector table is the first section in FLASH:

```
0x08000000: _estack             (initial MSP)
0x08000004: Reset_Handler
0x08000008: NMI_Handler
0x0800000C: HardFault_Handler
0x08000010: MemManage_Handler
0x08000014: BusFault_Handler
0x08000018: UsageFault_Handler
0x0800001C-0x08000028: reserved (4 words)
0x0800002C: SVC_Handler
0x08000030: DebugMon_Handler
0x08000034: reserved
0x08000038: PendSV_Handler
0x0800003C: SysTick_Handler
0x08000040-...: External interrupts (EXTI4 at offset 0x68 = vector 26 = position 10 + 16 = "EXTI4_IRQHandler")
```

(See `startup_stm32f401retx.s:126-230` for the full table.)

Vector remap via `SCB->VTOR` happens in `SystemInit` (CMSIS). Since the firmware boots from FLASH (no bootloader), `VTOR = FLASH_BASE | 0x0` is the default.

## What this layout does NOT support

- No CCM Code-Coupled-Memory (F401 doesn't have it).
- No external memory (no FMC/FSMC peripheral on F401).
- No DTCM/ITCM (Cortex-M7 feature, F401 is M4).
- No backup RAM (BKPSRAM not available on F401).

## Recommendations (NOT MADE — review-only)

1. **Delete `STM32F401RETX_RAM.ld`** or move to `tools/debug/` with a README. Currently confusing.
2. **Commit a sample `*.map` output** to demonstrate the size budget at a known build.
3. **Tighten `_Min_Stack_Size`** if needed after measurement. 1 KB MSP is plenty for typical use but could be reduced if RAM gets tight.
4. **Add `__bss_start` symbol** in a portable name for any future memory-introspection telemetry.

## Why this matters for interview defense

A reviewer might ask:

- **"Why 32 KB heap?"** — Because `configTOTAL_HEAP_SIZE` was set to that and used by *both* heaps. Acknowledge the dual-heap sizing.
- **"Why is the MSP only 1 KB?"** — Cortex-M4 interrupt stack frames are 32 bytes (no FPU saved) or 100 bytes (with FPU lazy stacking). 1 KB tolerates ~10-30 stack frames of nesting. Plenty for this app.
- **"What happens if BSS gets corrupted?"** — `Reset_Handler` zeroes BSS on every boot. So a corrupted BSS at runtime stays corrupted until reset.
- **"Where does the task stack come from?"** — From the FreeRTOS heap (`pvPortMalloc`), not from `._user_heap_stack` (that's newlib's sbrk).
