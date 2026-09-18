#include "CO2.h"
#include <string.h>

static uint8_t co2_rx_buffer[CO2_RX_BUF_SIZE];
static uint8_t co2_frame_buffer[CO2_RX_BUF_SIZE];
volatile uint8_t co2_rx_len;
static volatile uint8_t co2_frame_ready;

void CO2_UART_Callback(uint16_t Size)
{
    if (Size == CO2_RX_BUF_SIZE)
    {
        memcpy(co2_frame_buffer, co2_rx_buffer, CO2_RX_BUF_SIZE);
        co2_frame_ready = 1U;
        co2_rx_len = (uint8_t)Size;
    }
    else
    {
        co2_rx_len = (uint8_t)Size;
    }

    (void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_rx_buffer, sizeof(co2_rx_buffer));
}

void CO2_UART_Receive_Start(void)
{
    memset(co2_rx_buffer, 0, sizeof(co2_rx_buffer));
    memset(co2_frame_buffer, 0, sizeof(co2_frame_buffer));
    co2_rx_len = 0U;
    co2_frame_ready = 0U;
    (void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_rx_buffer, sizeof(co2_rx_buffer));
}

uint8_t CO2_TakeFrame(uint8_t frame[CO2_RX_BUF_SIZE])
{
    if (!co2_frame_ready)
    {
        return 0U;
    }

    memcpy(frame, co2_frame_buffer, CO2_RX_BUF_SIZE);
    co2_frame_ready = 0U;
    return 1U;
}

uint8_t CO2_ParseFrame(const uint8_t frame[CO2_RX_BUF_SIZE], uint16_t *co2_value)
{
    uint8_t check_sum = 0U;

    if ((frame == NULL) || (co2_value == NULL))
    {
        return 4U;
    }

    if (frame[0] != 0x2CU)
    {
        return 2U;
    }

    for (uint8_t i = 0U; i < (CO2_RX_BUF_SIZE - 1U); i++)
    {
        check_sum = (uint8_t)(check_sum + frame[i]);
    }

    if (check_sum != frame[CO2_RX_BUF_SIZE - 1U])
    {
        return 3U;
    }

    *co2_value = (uint16_t)(((uint16_t)frame[1] << 8) | frame[2]);
    return 0U;
}

uint8_t CO2_get_data(uint16_t *co2_value, uint32_t timeout)
{
    uint32_t start_time = HAL_GetTick();
    uint8_t frame[CO2_RX_BUF_SIZE];

    while ((HAL_GetTick() - start_time) <= timeout)
    {
        if (CO2_TakeFrame(frame))
        {
            return CO2_ParseFrame(frame, co2_value);
        }
    }

    return 1U;
}
