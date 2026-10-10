#ifndef CONTROL_SERVICE_H
#define CONTROL_SERVICE_H
#include "control_policy.h"
#include "actuator_service.h"

typedef struct
{
    ControlState_t policy;
    ActuatorState_t actuator;
    uint8_t auto_irrigation_enabled;
} ControlSnapshot_t;

void Control_ServiceInit(uint32_t now);
void Control_ServiceTask(uint32_t now);
void Control_GetSnapshot(ControlSnapshot_t *snapshot);
ControlResult_t Control_SetMode(ControlMode_t mode, uint32_t now);
ControlResult_t Control_SetPump(uint8_t on, uint32_t now);
ControlResult_t Control_SetFan(uint8_t level, uint32_t now);
void Control_EmergencyStop(uint32_t now);
void Control_ToggleMute(uint32_t now);
ControlResult_t Control_SetConfig(const ControlConfig_t *config, uint32_t now);
#endif
