#include "gateway_command.h"
#include <string.h>

void GatewayCommand_Init(GatewayCommand_t *client,uint16_t node,uint32_t session)
{
    memset(client,0,sizeof(*client));client->node=node;client->session=session;
}
uint8_t GatewayCommand_ObserveNode(GatewayCommand_t *client,const LoraFrame_t *message)
{
    if(client==0 || message==0 || message->source!=client->node || message->target!=LORA_GATEWAY_ADDRESS ||
        message->session!=client->session || message->flags!=LORA_FLAG_ACK) return 0U;
    uint32_t high_water;
    if(message->kind==LORA_KIND_HELLO && message->payload_length==8U)
        high_water=Lora_Read32(&message->payload[4]);
    else if(message->kind==LORA_KIND_TELEMETRY && message->payload_length==38U)
        high_water=Lora_Read32(&message->payload[34]);
    else return 0U;
    if(high_water>client->last_transaction) client->last_transaction=high_water;
    return 1U;
}

uint8_t GatewayCommand_Begin(GatewayCommand_t *client,uint32_t transaction,uint16_t sequence,
    uint8_t opcode,uint8_t argument,uint32_t estimated_node_tick,uint32_t ttl_ms,uint32_t local_now)
{
    if(client==0 || client->node==LORA_GATEWAY_ADDRESS || client->node==LORA_BROADCAST_ADDRESS ||
        ttl_ms==0U || ttl_ms>LORA_COMMAND_TTL_MAX_MS || transaction==0U || transaction<=client->last_transaction ||
        opcode<LORA_COMMAND_MODE || opcode>LORA_COMMAND_STATUS) return 0U;
    if((opcode==LORA_COMMAND_MODE && argument>2U) || (opcode==LORA_COMMAND_FAN && argument>4U) ||
        ((opcode==LORA_COMMAND_PUMP || opcode==LORA_COMMAND_MUTE) && argument>1U) ||
        ((opcode==LORA_COMMAND_STOP || opcode==LORA_COMMAND_STATUS) && argument!=0U)) return 0U;
    LoraFrame_t command={.kind=LORA_KIND_COMMAND,.flags=LORA_FLAG_RESULT,
        .source=LORA_GATEWAY_ADDRESS,.target=client->node,.sequence=sequence,
        .session=client->session,.transaction=transaction,.payload_length=LORA_COMMAND_SIZE};
    command.payload[0]=opcode;command.payload[1]=argument;
    Lora_Write32(&command.payload[2],estimated_node_tick+ttl_ms);
    if(!LoraReliable_Begin(&client->tx,&command,local_now)) return 0U;
    if(client->has_result)
    {
        client->previous_result=client->result;
        client->previous_valid=1U;
    }
    client->has_result=0U;client->last_transaction=transaction;
    return 1U;
}

uint8_t GatewayCommand_AcceptResult(GatewayCommand_t *client,const LoraFrame_t *result,LoraFrame_t *ack)
{
    if(client==0 || result==0 || ack==0 || !Lora_ResultValid(result)) return 0U;
    if(LoraReliable_AcceptReply(&client->tx,result))
    {
        client->result=*result;client->has_result=1U;
        const uint32_t high_water=Lora_Read32(&result->payload[18]);
        if(high_water>client->last_transaction) client->last_transaction=high_water;
        return Lora_BuildAck(result,ack);
    }
    /* 当前及上一条RESULT都保留确认能力，不会把旧结果覆盖到新事务状态。 */
    for(uint8_t i=0U;i<2U;++i)
    {
        const LoraFrame_t *cached=i==0U ? &client->result : &client->previous_result;
        const uint8_t valid=i==0U ? client->has_result : client->previous_valid;
        if(valid && result->source==cached->source && result->target==cached->target &&
            result->sequence==cached->sequence && result->session==cached->session &&
            result->transaction==cached->transaction && result->wire_crc==cached->wire_crc &&
            memcmp(result->payload,cached->payload,LORA_RESULT_SIZE)==0)
            return Lora_BuildAck(result,ack);
    }
    return 0U;
}
