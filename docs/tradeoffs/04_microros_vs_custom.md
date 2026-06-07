# Tradeoff 04 — micro-ROS vs custom binary protocol

## Chosen path (current source)

**micro-ROS over XRCE-DDS** with a static library link (`-lmicroros`, `Makefile:129`).

Two ROS topics: `/microros/imu_raw` publish + `/control/command` subscribe.

## Alternative considered (custom binary protocol)

A bespoke STM32-defined binary frame (e.g., `[STX][cmd_byte][payload_len][payload bytes][CRC16]`) parsed by a custom Jetson-side driver — bypassing the entire DDS / micro-ROS stack.

## Side-by-side

| Property | micro-ROS (chosen) | Custom binary protocol |
|---|---|---|
| Code on STM32 | ~few hundred KB static lib + ~500 LOC team code | ~500-1000 LOC total |
| ROS 2 native integration | Yes (`ros2 topic echo /microros/imu_raw` works out of the box) | Requires a Jetson-side translator node |
| Topic discovery | Automatic via XRCE-DDS agent | Hardcoded |
| QoS | Inherits ROS QoS profiles (RELIABLE, BE, KeepLast, etc.) | Whatever the team builds |
| Wire format | CDR-encoded DDS messages + XRCE framing | Team-defined |
| Backwards compatibility | Tied to micro-ROS / XRCE-DDS protocol version | Tied to team's own protocol version |
| Memory cost | Custom heap fork to keep micro-ROS isolated (~32 KB) | Could be allocator-free |
| Build complexity | micro-ROS static library + custom_transport + custom_allocator | Just the team's parser/serializer |
| Debugging | Standard ROS 2 tools (`ros2 topic echo`, `rqt`) | Need custom tools |
| Time-sync | `rmw_uros_sync_session` handles clock alignment | Manual time stamping |
| Future-proofing | Any future ROS 2 consumer can subscribe | Requires translator updates |

## What was gained by choosing micro-ROS

- **Standard ROS 2 topic interface.** The Jetson autonomy uses `/microros/imu_raw` the same way it uses any other ROS 2 topic.
- **No translator node needed.** The micro-ROS agent translates XRCE-DDS to native DDS transparently.
- **Tooling.** `ros2 topic echo`, `rqt_plot`, `rosbag2` all work.
- **Time sync built-in.** `rmw_uros_sync_session` handles clock alignment with the Jetson.
- **Pre-built static library.** No need to implement DDS framing.

## What was sacrificed

- **Memory footprint.** The custom heap fork exists because of micro-ROS allocation needs. ~32 KB BSS reserved .
- **Static library opacity.** Bugs inside `libmicroros.a` are invisible without source.
- **Domain ID hardcoded.** Mismatch with Jetson env is silent.
- **Build complexity.** Need to build/maintain the static library (or pin a known-good version).

## When the team might switch

- **Critical RAM shortage.** If a future feature needs more RAM than the micro-ROS heap allows, a custom protocol could halve the RAM cost.
- **Real-time guarantees.** If publishing must be deterministic-microsecond, the XRCE framing + agent overhead is unbounded; custom is tighter.
- **No ROS 2 on the host side.** If the host is bare-metal Linux or non-ROS, custom is simpler.

## Current cost-benefit reading

micro-ROS is the right choice for a ROS 2 autonomy stack. The team's mental model is "ROS topics from the STM32" — that's exactly what micro-ROS provides without re-implementing it.
