#ifndef ACTUATOR_SERVICE_H
#define ACTUATOR_SERVICE_H
#include <stdint.h>
#include "control_policy.h"

typedef struct
{
    uint8_t ready_mask, relay_on, fan_level, buzzer_on;
    uint16_t fan_duty_permille;
} ActuatorState_t;

void Actuator_ServiceInit(void);
uint8_t Actuator_ServiceApply(uint8_t relay_on, uint8_t fan_level, uint8_t buzzer_on);
void Actuator_AllSafeOff(void);
void Actuator_GetState(ActuatorState_t *state);
#endif
