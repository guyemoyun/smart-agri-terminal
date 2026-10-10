#include "button_state.h"
#include <string.h>

void ButtonState_Init(ButtonState_t *state, uint32_t now)
{
    memset(state, 0, sizeof(*state));
    state->raw_tick = now;
}

uint8_t ButtonState_Update(ButtonState_t *state, uint8_t pressed, uint32_t now)
{
    uint8_t event = 0U;
    pressed = (uint8_t)(pressed != 0U);
    if (pressed != state->raw)
    {
        state->raw = pressed;
        state->raw_tick = now;
    }
    if (state->stable != state->raw && (now - state->raw_tick) >= BUTTON_DEBOUNCE_MS)
    {
        state->stable = state->raw;
        if (state->stable != 0U)
        {
            state->press_tick = now;
            state->long_sent = 0U;
        }
        else if (state->long_sent == 0U)
        {
            /* 主循环偶尔延迟到松手后，仍依据持续时间区分长短按。 */
            event = (now - state->press_tick) >= BUTTON_LONG_MS ? BUTTON_EVENT_LONG : BUTTON_EVENT_SHORT;
        }
    }
    if (state->stable != 0U && state->raw != 0U && state->long_sent == 0U &&
        (now - state->press_tick) >= BUTTON_LONG_MS)
    {
        state->long_sent = 1U;
        event = BUTTON_EVENT_LONG;
    }
    return event;
}
