/**
  ******************************************************************************
  * @file    dht22.c
  * @brief   DHT22 温湿度传感器驱动（PC15 单总线）
  * @note    GCC/CMake 版本在约4ms的响应与40位采样期间临时屏蔽中断，
  *          防止 SysTick/USART 中断破坏微秒级脉冲采样。1ms启动低电平期间
  *          保持中断开启，确保 HAL_Delay() 正常运行。
  ******************************************************************************
  */
#include "dht22.h"
#include "main.h"
#include "app_main.h"

static void DHT22_SetOutputMode(void);
static void DHT22_SetInputMode(void);
static uint8_t DHT22_StartAndWaitResponse(uint32_t *irq_state);
static uint8_t DHT22_ReadByte(uint8_t *value);
static void DHT22_EndCritical(uint32_t irq_state);

static void DHT22_SetOutputMode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &GPIO_InitStruct);
}

static void DHT22_SetInputMode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &GPIO_InitStruct);
}

void DHT22_Init(void)
{
    DHT22_SetOutputMode();
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
}

uint8_t DHT22_ReadData(float *temp, float *humi)
{
    uint8_t buffer[5] = {0};
    uint32_t irq_state = 0U;
    uint8_t result;

    if ((temp == NULL) || (humi == NULL))
    {
        return 5U;
    }

    result = DHT22_StartAndWaitResponse(&irq_state);
    if (result != 0U)
    {
        return result;
    }

    for (uint8_t i = 0U; i < 5U; i++)
    {
        if (DHT22_ReadByte(&buffer[i]) != 0U)
        {
            DHT22_EndCritical(irq_state);
            return 2U;
        }
    }

    DHT22_EndCritical(irq_state);

    if (((buffer[0] + buffer[1] + buffer[2] + buffer[3]) & 0xFFU) != buffer[4])
    {
        return 3U;
    }

    *humi = (float)(((uint16_t)buffer[0] << 8) | buffer[1]) / 10.0f;

    if ((buffer[2] & 0x80U) != 0U)
    {
        *temp = -(float)((((uint16_t)(buffer[2] & 0x7FU) << 8) | buffer[3])) / 10.0f;
    }
    else
    {
        *temp = (float)(((uint16_t)buffer[2] << 8) | buffer[3]) / 10.0f;
    }

    return 0U;
}

static uint8_t DHT22_StartAndWaitResponse(uint32_t *irq_state)
{
    uint32_t timeout;

    DHT22_SetOutputMode();
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);

    *irq_state = __get_PRIMASK();
    __disable_irq();

    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
    delay_us(30U);
    DHT22_SetInputMode();

    timeout = 120U;
    while (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_SET)
    {
        delay_us(1U);
        if (--timeout == 0U)
        {
            DHT22_EndCritical(*irq_state);
            return 1U;
        }
    }

    timeout = 120U;
    while (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_RESET)
    {
        delay_us(1U);
        if (--timeout == 0U)
        {
            DHT22_EndCritical(*irq_state);
            return 1U;
        }
    }

    return 0U;
}

static uint8_t DHT22_ReadByte(uint8_t *value)
{
    uint8_t byte = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint32_t timeout = 120U;

        byte <<= 1;

        while (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_SET)
        {
            delay_us(1U);
            if (--timeout == 0U)
            {
                return 1U;
            }
        }

        timeout = 100U;
        while (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_RESET)
        {
            delay_us(1U);
            if (--timeout == 0U)
            {
                return 1U;
            }
        }

        /* 40us 采样点远离 0-bit 的 26~28us 高电平边界。 */
        delay_us(40U);
        if (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_SET)
        {
            byte |= 0x01U;
        }
    }

    *value = byte;
    return 0U;
}

static void DHT22_EndCritical(uint32_t irq_state)
{
    DHT22_SetOutputMode();
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);

    if (irq_state == 0U)
    {
        __enable_irq();
    }
}
