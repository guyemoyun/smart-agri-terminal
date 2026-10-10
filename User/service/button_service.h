#ifndef BUTTON_SERVICE_H
#define BUTTON_SERVICE_H
#include <stdint.h>
#define KEY_SW1_SHORT 0x01U
#define KEY_SW1_LONG  0x02U
#define KEY_SW2_SHORT 0x04U
#define KEY_SW2_LONG  0x08U
void Button_ServiceInit(uint32_t now);
void Button_ServiceTask(uint32_t now);
void Button_NotifyIRQ(uint16_t pin);
uint8_t Button_TakeEvents(void);
uint8_t Button_IsHeld(void);
#endif
