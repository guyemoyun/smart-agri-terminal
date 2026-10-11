#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>

#define LORA_WIRE_VERSION 3U
#define LORA_HEADER_SIZE 20U
#define LORA_MAX_FRAME 96U
#define LORA_MAX_PAYLOAD (LORA_MAX_FRAME - LORA_HEADER_SIZE - 2U)
#define LORA_GATEWAY_ADDRESS 0U
#define LORA_NODE_ADDRESS 1U
#define LORA_BROADCAST_ADDRESS 0xFFFFU
#define LORA_FLAG_ACK 1U
#define LORA_FLAG_RESULT 2U
#define LORA_COMMAND_TTL_MAX_MS 30000U
#define LORA_RESULT_SIZE 22U
#define LORA_COMMAND_SIZE 6U

typedef enum
{
    LORA_KIND_HELLO=1, LORA_KIND_TELEMETRY=2, LORA_KIND_COMMAND=3,
    LORA_KIND_RESULT=4, LORA_KIND_ACK=5
} LoraKind_t;
typedef enum
{
    LORA_COMMAND_MODE=1, LORA_COMMAND_FAN=2, LORA_COMMAND_PUMP=3,
    LORA_COMMAND_MUTE=4, LORA_COMMAND_STOP=5, LORA_COMMAND_STATUS=6
} LoraCommand_t;
/* 0～5 复用 ControlResult_t；以下状态表示协议层拒绝。 */
#define LORA_RESULT_EXPIRED 6U
#define LORA_RESULT_STALE 7U
#define LORA_RESULT_SAFETY_LOCK 8U

typedef struct
{
    uint8_t kind, flags, payload_length;
    uint16_t source, target, sequence, wire_crc;
    uint32_t session, transaction;
    uint8_t payload[LORA_MAX_PAYLOAD];
} LoraFrame_t;

typedef enum
{
    LORA_DECODE_OK=0, LORA_DECODE_ARGUMENT, LORA_DECODE_LENGTH,
    LORA_DECODE_HEADER, LORA_DECODE_CRC, LORA_DECODE_SHAPE
} LoraDecodeResult_t;

uint16_t Lora_Read16(const uint8_t *data);
uint32_t Lora_Read32(const uint8_t *data);
void Lora_Write16(uint8_t *data,uint16_t value);
void Lora_Write32(uint8_t *data,uint32_t value);
uint16_t Lora_Crc16(const uint8_t *data,size_t length);
size_t Lora_Encode(const LoraFrame_t *frame,uint8_t *out,size_t capacity);
LoraDecodeResult_t Lora_Decode(const uint8_t *data,size_t length,LoraFrame_t *frame);
uint8_t Lora_BuildAck(const LoraFrame_t *received,LoraFrame_t *ack);
uint8_t Lora_ResultValid(const LoraFrame_t *result);
#endif
