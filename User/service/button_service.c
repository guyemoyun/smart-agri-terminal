#include "button_service.h"
#include "button_state.h"
#include "main.h"

static ButtonState_t keys[2];
static uint32_t last_sample;
static uint8_t events;
static volatile uint8_t irq_seen;

void Button_ServiceInit(uint32_t now)
{
    ButtonState_Init(&keys[0], now);
    ButtonState_Init(&keys[1], now);
    last_sample = now - 10U;
    events = 0U;
    irq_seen = 0U;
}

void Button_NotifyIRQ(uint16_t pin)
{
    if (pin == SW1_Pin || pin == SW2_Pin) irq_seen = 1U;
}

void Button_ServiceTask(uint32_t now)
{
    if ((now - last_sample) < 10U && irq_seen == 0U) return;
    irq_seen = 0U;
    last_sample = now;
    events |= ButtonState_Update(&keys[0],
        (uint8_t)(HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET), now);
    events |= (uint8_t)(ButtonState_Update(&keys[1],
        (uint8_t)(HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET), now) << 2U);
}

uint8_t Button_TakeEvents(void)
{
    const uint8_t result = events;
    events = 0U;
    return result;
}

uint8_t Button_IsHeld(void)
{
    return (uint8_t)(keys[0].raw || keys[1].raw ||
        HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET ||
        HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET);
}
