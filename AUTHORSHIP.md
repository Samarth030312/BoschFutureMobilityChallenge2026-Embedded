# Authorship and Code Ownership

Last updated: 2026-06-08.

This document separates first-party (OdyssiX team) code from third-party code in the repository.

## Layers

### First-party (OdyssiX team)

| Path | Description |
|---|---|
| `Core/Src/bno085.c`, `Core/Inc/bno085.h` | BNO085 SHTP driver, multi-report walker, calibration |
| `Core/Src/freertos.c` | `microROSTask` body — bring-up sequence, IMU publish, command callback, deadman, PWM drive |
| `Core/Src/it_transport.c` | XRCE-DDS UART transport (open/close/read/write + callbacks) |
| `Core/Src/microros_allocators.c` | Adapter from `rcl_allocator_t` to custom heap |
| `Core/Src/custom_memory_manager.c` | Derivative of FreeRTOS heap_4 (see "Third-party" — derivative work) |
| `Core/Src/microros_time.c` | `clock_gettime` over FreeRTOS tick |
| User-code blocks in `Core/Src/main.c`, `Core/Src/gpio.c`, `Core/Src/stm32f4xx_it.c` | IWDG infrastructure, fault-handler reset, EXTI4 callback |
| `Makefile` | Build configuration |
| `docs/`, `hardware/`, this file | Documentation |

### Third-party — vendor and middleware

| Path | Origin | License |
|---|---|---|
| `Drivers/CMSIS/**` | Arm CMSIS via STMicroelectronics package | Apache 2.0 (`Drivers/CMSIS/LICENSE.txt`) |
| `Drivers/STM32F4xx_HAL_Driver/**` | STMicroelectronics STM32Cube HAL F4 | BSD-3-Clause (`Drivers/STM32F4xx_HAL_Driver/LICENSE.txt`) |
| `Middlewares/Third_Party/FreeRTOS/**` | FreeRTOS Kernel (Amazon / Real Time Engineers) | MIT (`Middlewares/Third_Party/FreeRTOS/Source/LICENSE`) |
| `Core/Startup/startup_stm32f401retx.s` | STMicroelectronics-generated | See file header copyright notice |
| `Core/Src/system_stm32f4xx.c`, related vendor sources | STMicroelectronics CMSIS-device | See file header copyright notices |
| `micro_ros_stm32cubemx_utils/**` | micro-ROS upstream (external dependency; user-provisioned) | Apache 2.0 (upstream) |

### CubeMX-generated scaffolding

Files in `Core/Inc` and `Core/Src` that follow the CubeMX pattern (`MX_*_Init`, `HAL_*_MspInit`, etc.) contain both ST-generated code and OdyssiX-added code in `USER CODE BEGIN/END` blocks. Ownership is mixed inside these files. Each file carries the STMicroelectronics copyright header; OdyssiX-added content in the user blocks is OdyssiX-owned.

Affected files: `main.c`, `gpio.c`, `dma.c`, `i2c.c`, `tim.c`, `usart.c`, `stm32f4xx_it.c`, `stm32f4xx_hal_msp.c`, `stm32f4xx_hal_timebase_tim.c`, `sysmem.c`, `syscalls.c`, plus their `.h` counterparts.

## Derivative work

`Core/Src/custom_memory_manager.c` declares: "A custom implementation of pvPortMallocMicroROS() and vPortFreeMicroROS() with realloc and calloc features based on FreeRTOS heap4.c."

The file is a derivative of `Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c`. The FreeRTOS MIT license notice is carried at the top of the file in compliance with the upstream's redistribution terms.

## License inventory at repository root

| Item | Present |
|---|---|
| `LICENSE` (Apache 2.0) | Yes |
| `README.md` | Yes |
| `AUTHORSHIP.md` | Yes |

## What is not claimed here

- No relicensing of any upstream third-party content. Upstream licenses apply to upstream files unchanged.
- No attribution to any individual team member by name — that is for the team to add when ready.
