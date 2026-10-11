#ifndef GATEWAY_COMMAND_H
#define GATEWAY_COMMAND_H
#include "lora_reliable.h"
/* 与 HAL/ESP8266 解耦的网关事务核心，尚不代表已有网关板固件。 */
typedef struct
{
    LoraReliable_t tx;
    LoraFrame_t result,previous_result;
    uint16_t node;
    uint32_t session,last_transaction;
    uint8_t has_result,previous_valid;
} GatewayCommand_t;
void GatewayCommand_Init(GatewayCommand_t *client,uint16_t node,uint32_t session);
/* 初次发现或网关重启后，先读取同会话节点HELLO/遥测公布的命令高水位。 */
uint8_t GatewayCommand_ObserveNode(GatewayCommand_t *client,const LoraFrame_t *message);
uint8_t GatewayCommand_Begin(GatewayCommand_t *client,uint32_t transaction,uint16_t sequence,
    uint8_t opcode,uint8_t argument,uint32_t estimated_node_tick,uint32_t ttl_ms,uint32_t local_now);
/* 业务拒绝也完成事务，调用方还要检查 result.payload[2] 是否为成功。 */
uint8_t GatewayCommand_AcceptResult(GatewayCommand_t *client,const LoraFrame_t *result,LoraFrame_t *ack);
#endif
