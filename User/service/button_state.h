#ifndef BUTTON_STATE_H
#define BUTTON_STATE_H
#include <stdint.h>
#define BUTTON_EVENT_SHORT 0x01U
#define BUTTON_EVENT_LONG  0x02U
#define BUTTON_DEBOUNCE_MS 20U
#define BUTTON_LONG_MS    1500U

typedef struct
{
    uint8_t raw, stable, long_sent;
    uint32_t raw_tick, press_tick;
} ButtonState_t;
void ButtonState_Init(ButtonState_t *state, uint32_t now);
uint8_t ButtonState_Update(ButtonState_t *state, uint8_t pressed, uint32_t now);
#endif
