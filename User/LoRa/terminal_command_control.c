#include "terminal_commands.h"

static uint8_t Execute(void *context,uint8_t opcode,uint8_t argument,uint32_t now)
{
    (void)context;
    Control_ServiceTask(now);
    ControlSnapshot_t state;Control_GetSnapshot(&state);
    /* 本地紧急停止和泵超时只能通过本地确认解除，远程模式命令不能解锁。 */
    if(opcode==LORA_COMMAND_MODE && argument!=CONTROL_MODE_SAFE &&
        (state.policy.latched_alarm&(ALARM_EMERGENCY|ALARM_PUMP_TIMEOUT)))
        return LORA_RESULT_SAFETY_LOCK;
    switch(opcode)
    {
        case LORA_COMMAND_MODE: return (uint8_t)Control_SetMode((ControlMode_t)argument,now);
        case LORA_COMMAND_FAN: return (uint8_t)Control_SetFan(argument,now);
        case LORA_COMMAND_PUMP: return (uint8_t)Control_SetPump(argument,now);
        case LORA_COMMAND_MUTE: Control_SetMuted(argument,now);return CONTROL_OK;
        case LORA_COMMAND_STOP: Control_EmergencyStop(now);return CONTROL_OK;
        case LORA_COMMAND_STATUS: return CONTROL_OK;
        default: return CONTROL_INVALID_ARGUMENT;
    }
}
static void Snapshot(void *context,ControlSnapshot_t *snapshot)
{ (void)context;Control_GetSnapshot(snapshot); }
const TerminalExecutor_t *TerminalCommands_ControlExecutor(void)
{
    static const TerminalExecutor_t executor={.execute=Execute,.snapshot=Snapshot};
    return &executor;
}
