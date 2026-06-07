# Decision 10 — Single-task architecture

## What the source confirms

- Exactly one user task: `microROSTask` created in `freertos.c:45-50, 82-91`.
- Task body: `StartDefaultTask` (`freertos.c:98-438`).
- Task priority: `osPriorityNormal` (24 in the FreeRTOS V2 wrapper scale).
- Task stack: `3000 * 4 = 12000` bytes.
- No `osThreadNew` calls beyond this one.
- No queues, semaphores, mutexes between application code (the custom heap uses `vTaskSuspendAll`, not a mutex).

## What the single task does

The task body (`freertos.c:103-437`) sequentially:

1. Initialises BNO085.
2. Enables 3 SH-2 reports.
3. Sets up micro-ROS custom transport.
4. Installs the custom allocator.
5. Pings the XRCE-DDS agent until success.
6. Syncs the session clock.
7. Builds rcl entities (init_options, support, node, publisher, subscriber, executor).
8. Starts TIM2 PWM and forces CCR1=CCR2=1500 µs neutral.
9. Initialises the IMU message struct.
10. Runs the 2-second BNO085 calibration.
11. Enters the infinite loop:
    - `rclc_executor_spin_some(executor, 0)` — non-blocking executor poll.
    - Every 10 s, re-sync session.
    - INT-gate test (flag OR pin LOW) → BNO085_GetData.
    - 50-error auto-recovery.
    - `data_valid` → build IMU msg → publish.
    - 10 Hz LED toggle.
    - Deadman + TIM2 CCR write.
    - `osDelay(1)`.

## Why a single task

Reasons inferable from the structure (NOT documented in source comments):

- **One sensor.** BNO085 is the only thing being read. No second sensor needs concurrent polling.
- **One publisher + one subscriber.** No multi-channel pub/sub fanout.
- **Cooperative 1 kHz cadence.** `osDelay(1)` yields every millisecond — gives other (kernel) tasks a chance to run. The Idle task gets the bulk of CPU when the application is sleeping.
- **Simplicity.** No queues, no semaphores, no inter-task synchronization to debug. The serial logic of `freertos.c:271-437` is readable top-to-bottom.
- **Memory.** A second task at 12 KB stack would eat ~13 KB more of the FreeRTOS heap. Current budget is tight .

## The asymmetry: ISRs handle concurrency

The single-task model works because **concurrency is handled at the ISR layer**:

- `EXTI4_IRQHandler` sets `bno085_int_flag`.
- `HAL_UART_RxCpltCallback` pushes UART bytes into the SPSC ring.
- `HAL_UART_ErrorCallback` self-heals UART overruns.
- `TIM1_UP_TIM10_IRQHandler` advances the HAL tick.
- `HAL_GPIO_EXTI_Callback` services the BNO085 INTN.

The task polls these inputs synchronously, processes them, and yields. Classical event-driven loop with ISR producers + main-loop consumer.

## Drawbacks visible in current source

- **I²C stalls block everything.** A `BNO085_GetData` that hits the 100 ms HAL timeout × 2 (header + cargo) = 200 ms stalls publish + actuation update for that duration. The deadman tolerates up to 500 ms. Tight.
- **`the FPU context-save state` invisibly load-bearing.** `configENABLE_FPU = 0` is safe only because there's one task. A future second task using floats would silently corrupt FPU state.
- **No producer/consumer separation.** Can't pipeline (e.g., BNO085 read in one task, ROS publish in another with the message-being-built queued between).
- **No diagnostic task.** Telemetry of heap, stack, error counters would require a second task or interleaving into the main loop.

## What would change with a second task

Adding any second task (e.g., diagnostic publisher) requires:

1. Setting `configENABLE_FPU = 1`  — single line.
2. Allocating another stack from the FreeRTOS heap.
3. Deciding the synchronization model — queue between BNO085 read and publish, or mutex on the IMU data struct.
4. Re-evaluating the heap budget .
5. Verifying that the new task doesn't starve the main task (priority assignment).

None of these are individually hard. The architectural simplicity is the cost.

## Alternatives the choice rules out

- **Two tasks: sensor + ROS** — would decouple I²C stall from publish stall.
- **Three tasks: sensor + ROS + actuation** — even finer decoupling.
- **N tasks via CMSIS-RTOS message queues** — fan-out model.

## What a future redesign might pick

- **Add a low-priority diagnostic task** that periodically publishes heap/stack/error-counter telemetry. Wouldn't affect the hot path.
- **Add a high-priority actuation task** that holds PWM at neutral and only releases on the latest valid command. Would isolate actuation from sensor stalls.
- **Move to a bare-metal cooperative model** (no FreeRTOS) — would shave RAM but lose micro-ROS integration.

## What this decision constrains in current code

- The serial `freertos.c:271-437` infinite loop body assumes all I/O is non-blocking-or-bounded.
- `bno085.data` is read directly without synchronization — safe only because there's one task.
- `target_cmd` is written by the executor callback and read by the deadman check — same task context, no race.
- the FPU context-save state is invisible because there's no FPU context to switch.

The architecture is **load-bearing on the assumption of one task**. Adding a second task touches many invariants.
