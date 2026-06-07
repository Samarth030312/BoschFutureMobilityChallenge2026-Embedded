#include <uxr/client/transport.h>

#include <rmw_microxrcedds_c/config.h>

#include "main.h"
#include "cmsis_os.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef RMW_UXRCE_TRANSPORT_CUSTOM

/* ---- micro-ROS XRCE-DDS UART transport (hardened, IT-mode RX) ----
 *
 * Three structural fixes vs the original cubemx_transport:
 *   1. Blocking HAL_UART_Transmit  — kills the gState race that made
 *      the first XRCE ping silently return 0 bytes sent.
 *   2. HAL_UART_ErrorCallback      — clears ORE/NE/FE/PE and re-arms
 *      RX. Without this, the FIRST single-byte overrun at 460800
 *      permanently wedged the receiver.
 *   3. (tail+1) % N wrap with "drop on full" — XRCE message frames
 *      stay intact under back-pressure.
 */

/* 2 KB ring — absorbs ~40 ms of sustained 460800-baud traffic. RAM
 * budget is tight (97 % used) so larger isn't feasible right now. */
#define UART_IT_BUFFER_SIZE 2048u

static uint8_t           it_buffer[UART_IT_BUFFER_SIZE];
static uint8_t           it_data;
static volatile uint32_t it_head = 0u;   /* read index (transport_read) */
static volatile uint32_t it_tail = 0u;   /* write index (RX ISR)        */

bool cubemx_transport_open(struct uxrCustomTransport * transport)
{
    UART_HandleTypeDef * uart = (UART_HandleTypeDef *) transport->args;

    /* Reset ring buffer — a stale session must not poison the new one. */
    it_head = 0u;
    it_tail = 0u;

    /* Clear any latched error flags from a prior session. */
    __HAL_UART_CLEAR_OREFLAG(uart);
    __HAL_UART_CLEAR_NEFLAG(uart);
    __HAL_UART_CLEAR_FEFLAG(uart);
    __HAL_UART_CLEAR_PEFLAG(uart);

    /* Discard any stale byte in DR so the next byte clocks in clean. */
    volatile uint8_t dummy = (uint8_t)(uart->Instance->DR);
    (void)dummy;

    HAL_UART_Receive_IT(uart, &it_data, 1);
    return true;
}

bool cubemx_transport_close(struct uxrCustomTransport * transport)
{
    UART_HandleTypeDef * uart = (UART_HandleTypeDef *) transport->args;
    HAL_UART_Abort_IT(uart);
    return true;
}

/*
 * Blocking TX. ~80-byte XRCE message at 460800 baud = ~1.9 ms. Well
 * inside the urosTransport task's 100 ms ping budget and the 5 ms spin
 * loop delay. Eliminates the gState race entirely.
 */
size_t cubemx_transport_write(struct uxrCustomTransport * transport,
                              const uint8_t * buf,
                              size_t          len,
                              uint8_t       * err)
{
    (void)err;
    UART_HandleTypeDef * uart = (UART_HandleTypeDef *) transport->args;

    HAL_StatusTypeDef ret = HAL_UART_Transmit(uart, (uint8_t *)buf, len, 100u);
    return (ret == HAL_OK) ? len : 0u;
}

/*
 * Non-blocking ring-buffer drain. micro-ROS handles its own
 * poll-with-timeout above this layer.
 */
size_t cubemx_transport_read(struct uxrCustomTransport * transport,
                             uint8_t * buf,
                             size_t    len,
                             int       timeout,
                             uint8_t * err)
{
    (void)transport; (void)timeout; (void)err;

    size_t wrote = 0u;
    while (wrote < len) {
        uint32_t local_head = it_head;
        if (local_head == it_tail) {
            break;   /* empty */
        }
        buf[wrote++] = it_buffer[local_head];
        it_head      = (local_head + 1u) % UART_IT_BUFFER_SIZE;
    }
    return wrote;
}

/* RX-complete: push one byte into the ring with proper wrap.
 * If the ring is full, drop the new byte rather than overwrite unread
 * data — XRCE messages must stay intact. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef * huart)
{
    uint32_t next = (it_tail + 1u) % UART_IT_BUFFER_SIZE;
    if (next != it_head) {
        it_buffer[it_tail] = it_data;
        it_tail            = next;
    }
    HAL_UART_Receive_IT(huart, &it_data, 1);
}

/* Error path: ORE/NE/FE/PE. Without this handler the FIRST overrun at
 * 460800 baud permanently wedges the receiver — the agent then sees
 * the STM32 as forever silent. Clearing the flags and re-arming RX
 * makes the transport self-healing. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef * huart)
{
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);

    /* Drain the latched byte so the next interrupt is clean. */
    volatile uint8_t dummy = (uint8_t)(huart->Instance->DR);
    (void)dummy;

    /* Abort any in-flight IT operation so the re-arm starts fresh. */
    HAL_UART_AbortReceive_IT(huart);

    HAL_UART_Receive_IT(huart, &it_data, 1);
}

#endif /* RMW_UXRCE_TRANSPORT_CUSTOM */
