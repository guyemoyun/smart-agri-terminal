#include "terminal_commands.h"
#include <string.h>

void TerminalCommands_Init(TerminalCommands_t *engine,uint16_t node,uint16_t gateway,uint32_t session)
{
    memset(engine,0,sizeof(*engine));engine->node=node;engine->gateway=gateway;engine->session=session;
}

static uint8_t ArgumentValid(uint8_t opcode,uint8_t argument)
{
    switch(opcode)
    {
        case LORA_COMMAND_MODE: return (uint8_t)(argument<=CONTROL_MODE_SAFE);
        case LORA_COMMAND_FAN: return (uint8_t)(argument<=4U);
        case LORA_COMMAND_PUMP:
        case LORA_COMMAND_MUTE: return (uint8_t)(argument<=1U);
        case LORA_COMMAND_STOP:
        case LORA_COMMAND_STATUS: return (uint8_t)(argument==0U);
        default: return 0U;
    }
}

static void SnapshotResult(const ControlSnapshot_t *state,uint8_t opcode,uint8_t argument,
                           uint8_t status,uint32_t now,uint32_t high_water,uint8_t *data)
{
    data[0]=opcode;data[1]=argument;data[2]=status;data[3]=(uint8_t)state->policy.mode;
    data[4]=state->actuator.relay_on;data[5]=state->actuator.fan_level;data[6]=state->actuator.buzzer_on;
    data[7]=state->actuator.ready_mask;Lora_Write16(&data[8],state->actuator.fan_duty_permille);
    Lora_Write16(&data[10],state->policy.alarm_mask);
    data[12]=(uint8_t)((state->policy.muted?1U:0U)|(state->auto_irrigation_enabled?2U:0U));
    data[13]=0U;Lora_Write32(&data[14],now);Lora_Write32(&data[18],high_water);
}

static void Response(const LoraFrame_t *request,const uint8_t *data,LoraFrame_t *response)
{
    *response=(LoraFrame_t){.kind=LORA_KIND_RESULT,.flags=LORA_FLAG_ACK,
        .source=request->target,.target=request->source,.sequence=request->sequence,
        .session=request->session,.transaction=request->transaction,.payload_length=LORA_RESULT_SIZE};
    memcpy(response->payload,data,LORA_RESULT_SIZE);
}

TerminalCommandOutcome_t TerminalCommands_Process(TerminalCommands_t *engine,const LoraFrame_t *command,
    const TerminalExecutor_t *executor,uint32_t now,LoraFrame_t *response)
{
    if(engine==0 || command==0 || response==0 || executor==0 || executor->execute==0 || executor->snapshot==0 ||
        engine->session==0U || command->kind!=LORA_KIND_COMMAND || command->flags!=LORA_FLAG_RESULT ||
        command->payload_length!=LORA_COMMAND_SIZE || command->target!=engine->node ||
        command->target==LORA_BROADCAST_ADDRESS || command->source!=engine->gateway ||
        command->session!=engine->session || command->transaction==0U ||
        command->payload[0]<LORA_COMMAND_MODE || command->payload[0]>LORA_COMMAND_STATUS)
        return TERMINAL_COMMAND_DROP;
    const uint8_t opcode=command->payload[0],argument=command->payload[1];
    const uint32_t expires=Lora_Read32(&command->payload[2]);
    for(uint8_t i=0U;i<TERMINAL_COMMAND_CACHE;++i)
    {
        const TerminalCommandRecord_t *record=&engine->records[i];
        if(record->valid && record->transaction==command->transaction)
        {
            if(record->opcode!=opcode || record->argument!=argument || record->sequence!=command->sequence || record->expires!=expires)
                return TERMINAL_COMMAND_CONFLICT;
            /* 即使首次执行后的截止时间已过，也只返回原结果，绝不再次执行。 */
            Response(command,record->result,response);
            return TERMINAL_COMMAND_DUPLICATE;
        }
    }
    ControlSnapshot_t state;
    uint8_t result;
    TerminalCommandOutcome_t outcome=TERMINAL_COMMAND_NEW;
    if(command->transaction<=engine->high_water)
    {
        /* 缓存淘汰后高水位仍然保留，旧开泵命令不能被重新当作新命令。 */
        executor->snapshot(executor->context,&state);
        uint8_t data[LORA_RESULT_SIZE];SnapshotResult(&state,opcode,argument,LORA_RESULT_STALE,now,engine->high_water,data);
        Response(command,data,response);
        return TERMINAL_COMMAND_STALE;
    }
    const uint32_t remaining=expires-now;
    if(!ArgumentValid(opcode,argument)) result=CONTROL_INVALID_ARGUMENT;
    else if(remaining==0U || remaining>0x7FFFFFFFU) result=LORA_RESULT_EXPIRED;
    else if(remaining>LORA_COMMAND_TTL_MAX_MS) result=CONTROL_INVALID_ARGUMENT;
    else result=executor->execute(executor->context,opcode,argument,now);
    engine->high_water=command->transaction;
    executor->snapshot(executor->context,&state);
    TerminalCommandRecord_t *record=&engine->records[engine->next_slot];
    *record=(TerminalCommandRecord_t){.valid=1U,.opcode=opcode,.argument=argument,
        .sequence=command->sequence,.transaction=command->transaction,.expires=expires};
    SnapshotResult(&state,opcode,argument,result,now,engine->high_water,record->result);
    Response(command,record->result,response);
    engine->next_slot=(uint8_t)((engine->next_slot+1U)%TERMINAL_COMMAND_CACHE);
    return outcome;
}
