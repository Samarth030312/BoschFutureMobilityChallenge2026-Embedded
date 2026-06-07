# Decision 04 — micro-ROS

## What the source confirms

- micro-ROS static library linked: `-lmicroros` (`Makefile:129`), located in `micro_ros_stm32cubemx_utils/microros_static_library/libmicroros/`.
- micro-ROS API used: `rcl_init_options`, `rclc_support`, `rclc_node`, `rclc_publisher`, `rclc_subscription`, `rclc_executor`, `rmw_uros_*` (`freertos.c:139-205`).
- Publisher: `/microros/imu_raw` (`sensor_msgs::Imu`).
- Subscriber: `/control/command` (`bfmc_msgs::ControlCommand`).
- Custom transport bound: `rmw_uros_set_custom_transport(true, &huart2, ...)` (`freertos.c:130`).
- Custom allocator installed: `rcutils_set_default_allocator` (`freertos.c:145`).
- Custom clock source: `clock_gettime` implementation in `microros_time.c` against FreeRTOS tick.

## Why micro-ROS

A new engineer should understand: this firmware speaks ROS 2 topics. **micro-ROS is what makes that possible on a small MCU.**

- **Same API as full ROS 2** (rcl + rclc). The team's code reads like ROS 2 code; familiar to anyone with ROS 2 background.
- **Static memory model.** All allocations route through the custom allocator. No surprise mallocs deep in the library.
- **Agent-bridged.** Translates the firmware's XRCE-DDS to native DDS on the Jetson side, so the Jetson sees a "normal" ROS 2 publisher.
- **Topic-level integration with the autonomy stack.** Cleanest possible boundary between firmware (publishes IMU) and autonomy (subscribes IMU, publishes commands).

## What micro-ROS is, technically

`rcl` = ROS Client Library (same API as ROS 2 desktop).
`rclc` = a thin C wrapper providing simpler entity creation + executor patterns.
`rmw_microxrcedds_c` = RMW (ROS Middleware) implementation that speaks XRCE-DDS instead of full DDS.
`uxrCustomTransport` = pluggable transport (UART here; could be UDP, USB, etc.).

The team uses all four layers. They're stacked in this firmware as a static library — no source code customisation beyond the transport callbacks and the allocator adapters.

## What the firmware contributes (in-source code)

| File | Role |
|---|---|
| `Core/Src/it_transport.c` | Custom UART transport (open/close/read/write + RX/Error callbacks) |
| `Core/Src/microros_allocators.c` | Adapter from rcl_allocator_t signatures to the custom heap |
| `Core/Src/custom_memory_manager.c` | The heap_4 fork backing the allocator |
| `Core/Src/microros_time.c` | `clock_gettime` against FreeRTOS tick |
| `Core/Src/syscalls.c:192-203` | `usleep` against `osDelay` (used by micro-ROS internals) |
| `Core/Src/freertos.c:103-266` | Bring-up sequence + main publish/subscribe loop |

Everything else is the static library.

## Drawbacks visible in current source

- **Static library opacity.** Bugs inside `libmicroros.a` are invisible without rebuild from source. The build provenance of the .a file is not documented in this repo.
- **Domain ID hardcoded** (`freertos.c:173`: 42). Mismatch with Jetson `ROS_DOMAIN_ID` env is silent.
- **No version pin.** Which micro-ROS commit was used to build the .a? Unknown from the repo alone.
- **No diagnostic topic.** Only IMU pub + command sub. If the firmware wanted to report stack-margin, heap-margin, error counters — it would need new publishers.

## Alternatives the choice rules out

- **Raw XRCE-DDS without rcl/rclc** (UXR client only). Smaller footprint; more boilerplate. Loses the ROS-topic abstraction.
- **Custom binary protocol** (no DDS). Tiny footprint, full control. Loses interoperability with ROS 2 tools.
- **MQTT over UART** + bridge. Different ecosystem; works but loses tooling.
- **CANopen + ROS 2 bridge.** Different bus, different ecosystem.

For a team that already has a Jetson + ROS 2 autonomy stack, micro-ROS is the path of least friction.

## What a future redesign might pick

- **Stay on micro-ROS, add diagnostic publishers** for stack/heap/error telemetry.
- **Pin the static library version** in a `THIRD_PARTY.md`.
- **Move the domain ID to a build-time `-DROS_DOMAIN_ID=42`** flag so it's at least visible in the build command.
- **Migrate to micro-ROS Iron or newer** when the team's ROS 2 distro upgrades — verify XRCE protocol compatibility.

## What this decision constrains in current code

- The publish path (`freertos.c:341-403`) is shaped around `rcl_publish`.
- The custom transport callbacks have fixed signatures imposed by `uxrCustomTransport`.
- The allocator interface is `rcl_allocator_t`.
- The clock source has to provide `clock_gettime`.

Removing micro-ROS would touch all of these and require a replacement transport layer.
