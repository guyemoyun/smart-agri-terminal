#include "CO2.h"
#include <string.h>

#define CO2_RING_SIZE 64U
uint8_t co2_buffer[CO2_RX_BUF_SIZE];
volatile uint8_t co2_rx_len;
static volatile uint8_t ring[CO2_RING_SIZE], head, tail, overflow, uart_failed;
static CO2Parser_t parser;
static uint32_t last_restart;

void CO2_UART_Callback(uint16_t size)
{
    if (size > CO2_RX_BUF_SIZE) { overflow = 1U; size = CO2_RX_BUF_SIZE; }
    for (uint16_t i = 0U; i < size; ++i)
    {
        const uint8_t next = (uint8_t)((head + 1U) % CO2_RING_SIZE);
        if (next == tail) { overflow = 1U; break; }
        ring[head] = co2_buffer[i];
        head = next;
    }
    co2_rx_len = (uint8_t)size;
    /* 先复制到队列再复用接收缓冲，避免主循环解析期间被下一帧覆盖。 */
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_buffer, sizeof(co2_buffer)) != HAL_OK)
        uart_failed = 1U;
}

void CO2_UART_Receive_Start(void)
{
    head = tail = overflow = uart_failed = co2_rx_len = 0U;
    CO2Parser_Init(&parser);
    memset(co2_buffer, 0, sizeof(co2_buffer));
    last_restart = HAL_GetTick();
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_buffer, sizeof(co2_buffer)) != HAL_OK)
        uart_failed = 1U;
}

void CO2_UART_Error(void)
{
    uart_failed = 1U; /* ISR 中不阻塞、不打印，主循环负责恢复。 */
}

uint8_t CO2_TryRead(uint16_t *co2_value)
{
    uint8_t result = CO2_READ_NO_DATA;
    uint16_t latest = 0U;
    if (co2_value == 0) return CO2_READ_ARGUMENT;
    if (uart_failed != 0U)
    {
        if ((HAL_GetTick() - last_restart) >= 100U)
        {
            last_restart = HAL_GetTick();
            (void)HAL_UART_AbortReceive(&huart2);
            uart_failed = 0U;
            const uint32_t irq = __get_PRIMASK();
            __disable_irq();
            tail = head; overflow = 0U; co2_rx_len = 0U;
            __set_PRIMASK(irq);
            CO2Parser_Init(&parser);
            if (HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_buffer, sizeof(co2_buffer)) != HAL_OK)
                uart_failed = 1U;
        }
        return CO2_READ_UART_ERROR;
    }
    if (overflow != 0U)
    {
        const uint32_t irq = __get_PRIMASK();
        __disable_irq();
        tail = head; overflow = 0U; co2_rx_len = 0U;
        __set_PRIMASK(irq);
        CO2Parser_Init(&parser);
        return CO2_READ_OVERFLOW;
    }
    /* 每次至多消费一个队列容量，不因连续数据到达而霸占主循环。 */
    for (uint8_t count = 0U; count < CO2_RING_SIZE; ++count)
    {
        uint8_t byte;
        const uint32_t irq = __get_PRIMASK();
        __disable_irq();
        if (tail == head) { co2_rx_len = 0U; __set_PRIMASK(irq); break; }
        byte = ring[tail]; tail = (uint8_t)((tail + 1U) % CO2_RING_SIZE);
        __set_PRIMASK(irq);
        const uint8_t parsed = CO2Parser_Feed(&parser, byte, &latest);
        if (parsed == 1U) { *co2_value = latest; result = CO2_READ_OK; }
        else if (parsed == 2U && result != CO2_READ_OK) result = CO2_READ_BAD_FRAME;
    }
    return result;
}

uint8_t CO2_get_data(uint16_t *co2_value, uint32_t timeout)
{
    const uint32_t start = HAL_GetTick();
    uint8_t result;
    do
    {
        result = CO2_TryRead(co2_value);
        if (result != CO2_READ_NO_DATA) return result;
        if ((HAL_GetTick() - start) >= timeout) break;
        HAL_Delay(1U);
    } while (1);
    return CO2_READ_NO_DATA;
}
