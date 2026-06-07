# micro-ROS Subsystem

**Generated:** 2026-06-08 | **Phase:** 5

Most of the micro-ROS analysis lives in two places:

- `docs/architecture/data_flow_graph.md` — runtime packet flow.
- `docs/subsystems/memory/` — custom allocator.
- `docs/architecture/interrupt_graph.md` — UART RX/Error ISR.

This document is the **embedded-side framing** of the micro-ROS bring-up + the transport layer details.

## Stack diagram

```
ROS 2 application (Jetson)
  ↓ rcl/rclc API
rmw_cyclonedds_cpp (Jetson RMW)
  ↓ DDS protocol
CycloneDDS
  ↓
micro-ROS agent (docker)
  ↓ XRCE-DDS protocol over UART
USART2 (460 800 baud)
  ↓ USB-CDC bridge (Nucleo ST-LINK chip)
USART2 on STM32 (PA2 TX / PA3 RX)
  ↓ it_transport.c
rmw_microxrcedds_c
  ↓
rclc / rcl (embedded)
  ↓
microROSTask application code
```

## Bring-up sequence (firmware side)

1. **Custom transport bind** (`freertos.c:130-134`):

```c
rmw_uros_set_custom_transport(true, (void *) &huart2,
                              cubemx_transport_open,
                              cubemx_transport_close,
                              cubemx_transport_write,
                              cubemx_transport_read);
```

The `true` first arg means "use framing" (XRCE-DDS adds STX/checksums on top of the raw transport). The four callbacks live in `it_transport.c`.

2. **Custom allocator install** (`freertos.c:139-147`):

```c
rcl_allocator_t freeRTOS_allocator = rcutils_get_zero_initialized_allocator();
freeRTOS_allocator.allocate      = microros_allocate;
freeRTOS_allocator.deallocate    = microros_deallocate;
freeRTOS_allocator.reallocate    = microros_reallocate;
freeRTOS_allocator.zero_allocate = microros_zero_allocate;

if (!rcutils_set_default_allocator(&freeRTOS_allocator)) {
  osDelay(1000); NVIC_SystemReset();
}
```

From this moment, every rcl/rclc/rmw allocation goes through `custom_memory_manager.c`. Reset on failure.

3. **Agent ping loop** (`freertos.c:152-154`):

```c
while (rmw_uros_ping_agent(100, 1) != RMW_RET_OK) {
    osDelay(100);
}
```

100 ms timeout per ping, 1 attempt. Loop until success. Firmware will park here until the docker container brings up the agent.

4. **Session sync** (`freertos.c:156`):

```c
rmw_uros_sync_session(1000);
```

Aligns the firmware's notion of epoch nanos with the agent's. 1000 ms timeout.

5. **Init options + domain ID** (`freertos.c:171-173`):

```c
rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
rcl_init_options_init(&init_options, allocator);
rcl_init_options_set_domain_id(&init_options, 42);
```

**Domain 42 hardcoded.** Must match Jetson launch's `ROS_DOMAIN_ID=42`.

6. **rclc_support + node** (`freertos.c:175-181`):

```c
rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator);
rclc_node_init_default(&node, "odyssix_imu_node", "", &support);
```

Node name `odyssix_imu_node` visible via `ros2 node list` on the Jetson.

7. **Publisher** (`freertos.c:183-188`):

```c
rclc_publisher_init_default(
    &imu_publisher, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
    "/microros/imu_raw");
```

Topic `/microros/imu_raw`. Default QoS = RELIABLE KeepLast(1) on the micro-ROS side.

8. **Executor + subscription** (`freertos.c:190-205`):

```c
rclc_executor_init(&executor, &support.context, 1, &allocator);
rclc_subscription_init_default(
    &cmd_subscriber, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(bfmc_msgs, msg, ControlCommand),
    "/control/command");
rclc_executor_add_subscription(
    &executor, &cmd_subscriber, &target_cmd,
    &control_cmd_callback, ON_NEW_DATA);
```

Executor with 1 handle. Subscription delivers into pre-allocated `target_cmd` struct. Callback fires only on new data.

## Continuous-loop tasks

In the infinite loop (`freertos.c:271-437`):

- `rclc_executor_spin_some(&executor, 0)` — non-blocking spin every iteration. Returns immediately if no new subscribed messages. When a `/control/command` arrives, dispatches `control_cmd_callback`.
- `rmw_uros_sync_session(10)` — every ~10 s, re-syncs the clock. Quick (10 ms timeout).
- `rcl_publish(&imu_publisher, &imu_msg, NULL)` — every time `data_valid` is set.

## Why these specific knobs

| Choice | Rationale |
|---|---|
| `rmw_uros_ping_agent(100, 1)` retry forever | Operator-friendly: firmware survives docker startup delays. |
| `rmw_uros_sync_session(1000)` once + (10) periodically | Initial sync gets a long timeout; periodic re-sync is quick. |
| Domain 42 | BFMC team-specific; must match launch. |
| Executor handle count = 1 | Only one subscription (`/control/command`). |
| `ON_NEW_DATA` trigger | Callback fires only when a new message arrives (not on every spin). |

## ON_NEW_DATA semantics

The executor stores the latest unread `/control/command` message. On `spin_some`, if the message is new (sequence number differs from last delivered), the callback fires synchronously. The callback returns; spin returns.

This means: a fast-rate `/control/command` could deliver every cycle. A slow-rate one wouldn't fire the callback until the next message arrives.

## Custom transport details

See `docs/architecture/data_flow_graph.md` for the data-flow view and `it_transport.c` source for the implementation. Highlights:

- IT-mode RX (single byte at a time) into a 2 KiB lock-free SPSC ring.
- Blocking TX via `HAL_UART_Transmit` (kills the gState race).
- `HAL_UART_ErrorCallback` clears ORE/NE/FE/PE and re-arms RX (overrun self-heal).
- Drop-on-full preserves XRCE frame integrity.

## Reset-on-failure pattern

Several stages reset the firmware on bring-up failure:

```c
if (rclc_support_init_with_options(...) != RCL_RET_OK) {
    osDelay(1000); NVIC_SystemReset();
}
```
(`freertos.c:175-177`, similarly at lines 179-181, 183-188, 190-192, 194-199, 203-207)

**Why reset rather than retry:**
- A failed entity init usually indicates an allocator OOM, a duplicate-name conflict, or an agent compatibility issue.
- Retries in-loop would hang the firmware indefinitely.
- Reset is the simplest recovery — boots back to fresh state.

The 1-second delay before reset gives the agent time to log the error.

## Reset gaps

This reset-on-failure pattern is **not applied to**:

- The `rmw_uros_ping_agent` loop (deliberate: agent may not be up yet).
- The `rmw_uros_sync_session` call (returns void; can't check).
- `rcl_publish` failures in the main loop (deliberate: occasional failures are normal; we don't reset on every dropped publish).

## micro-ROS static library

The Makefile links against `-lmicroros` from `micro_ros_stm32cubemx_utils/microros_static_library/libmicroros/`. This is a **pre-built** static library — the team doesn't build the micro-ROS source from scratch.

Building this library requires:
- `micro_ros_setup` package.
- `colcon build --packages-select micro_ros_stm32cubemx_utils`.
- Matching the `print_cflags` output (`Makefile:166-168`) to the build's expected CFLAGS.

The library is committed to the repo for build reproducibility. NOT PROVEN exact version; the team may want to capture the micro-ROS commit hash in a README.

## Risks

1. **Domain ID mismatch** — if firmware (`freertos.c:173`) and Jetson launch disagree, silent no-discovery. Build-time flag would close this.
2. **micro-ROS version drift** — the static library is committed but its source provenance isn't documented. If the team needs to rebuild, they'd have to recover the exact source state.
3. **OOM during publisher init** — protected by reset; but a failed publish at runtime is silent.
4. **Session loss mid-operation** — XRCE buffers undelivered messages; eventually OOMs the custom heap; no recovery without operator restart.

## Recommendations (NOT MADE — review-only)

1. Add a `-DROS_DOMAIN_ID=42` build flag in `Makefile` and use it in `freertos.c:173`.
2. Document the exact `micro_ros_stm32cubemx_utils` commit/tag used to build the library.
3. Add a periodic ping-agent check in the main loop; reset firmware if agent stays unreachable for >30 s.
4. Add a low-watermark check on the custom heap; reset if free heap drops below a threshold.
