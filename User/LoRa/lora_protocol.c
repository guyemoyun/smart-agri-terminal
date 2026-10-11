#include "lora_protocol.h"
#include <string.h>

uint16_t Lora_Read16(const uint8_t *data)
{ return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U)); }
uint32_t Lora_Read32(const uint8_t *data)
{ return (uint32_t)data[0] | ((uint32_t)data[1]<<8U) | ((uint32_t)data[2]<<16U) | ((uint32_t)data[3]<<24U); }
void Lora_Write16(uint8_t *data,uint16_t value)
{ data[0]=(uint8_t)value;data[1]=(uint8_t)(value>>8U); }
void Lora_Write32(uint8_t *data,uint32_t value)
{ for(uint8_t i=0U;i<4U;++i)data[i]=(uint8_t)(value>>(8U*i)); }
uint16_t Lora_Crc16(const uint8_t *data,size_t length)
{
    uint16_t crc=0xFFFFU;
    for(size_t i=0U;i<length;++i)
    {
        crc^=data[i];
        for(uint8_t bit=0U;bit<8U;++bit)
            crc=(uint16_t)((crc>>1U)^((crc&1U)?0xA001U:0U));
    }
    return crc;
}

static uint8_t ShapeValid(const LoraFrame_t *frame)
{
    if(frame->session==0U || frame->session==UINT32_MAX || frame->transaction==0U) return 0U;
    switch(frame->kind)
    {
        case LORA_KIND_HELLO: return (uint8_t)(frame->flags==LORA_FLAG_ACK && frame->payload_length==8U);
        case LORA_KIND_TELEMETRY: return (uint8_t)(frame->flags==LORA_FLAG_ACK && frame->payload_length==38U);
        case LORA_KIND_COMMAND: return (uint8_t)(frame->flags==LORA_FLAG_RESULT && frame->payload_length==LORA_COMMAND_SIZE);
        case LORA_KIND_RESULT: return (uint8_t)(frame->flags==LORA_FLAG_ACK && frame->payload_length==LORA_RESULT_SIZE);
        case LORA_KIND_ACK:
            return (uint8_t)(frame->flags==0U && frame->payload_length==3U &&
                (frame->payload[0]==LORA_KIND_HELLO || frame->payload[0]==LORA_KIND_TELEMETRY || frame->payload[0]==LORA_KIND_RESULT));
        default: return 0U;
    }
}

size_t Lora_Encode(const LoraFrame_t *frame,uint8_t *out,size_t capacity)
{
    if(frame==0 || out==0 || frame->payload_length>LORA_MAX_PAYLOAD || !ShapeValid(frame)) return 0U;
    const size_t length=LORA_HEADER_SIZE+(size_t)frame->payload_length+2U;
    if(capacity<length) return 0U;
    out[0]=0xA5U;out[1]=LORA_WIRE_VERSION;out[2]=frame->kind;out[3]=frame->flags;
    Lora_Write16(&out[4],frame->source);Lora_Write16(&out[6],frame->target);
    Lora_Write16(&out[8],frame->sequence);out[10]=frame->payload_length;out[11]=0U;
    Lora_Write32(&out[12],frame->session);Lora_Write32(&out[16],frame->transaction);
    memcpy(&out[LORA_HEADER_SIZE],frame->payload,frame->payload_length);
    Lora_Write16(&out[length-2U],Lora_Crc16(out,length-2U));
    return length;
}

LoraDecodeResult_t Lora_Decode(const uint8_t *data,size_t length,LoraFrame_t *frame)
{
    if(data==0 || frame==0) return LORA_DECODE_ARGUMENT;
    if(length<LORA_HEADER_SIZE+2U || length>LORA_MAX_FRAME) return LORA_DECODE_LENGTH;
    if(data[0]!=0xA5U || data[1]!=LORA_WIRE_VERSION || data[11]!=0U) return LORA_DECODE_HEADER;
    if(data[10]>LORA_MAX_PAYLOAD || length!=LORA_HEADER_SIZE+(size_t)data[10]+2U) return LORA_DECODE_LENGTH;
    if(Lora_Crc16(data,length)!=0U) return LORA_DECODE_CRC;
    LoraFrame_t parsed={0};
    parsed.kind=data[2];parsed.flags=data[3];parsed.source=Lora_Read16(&data[4]);
    parsed.target=Lora_Read16(&data[6]);parsed.sequence=Lora_Read16(&data[8]);parsed.payload_length=data[10];
    parsed.session=Lora_Read32(&data[12]);parsed.transaction=Lora_Read32(&data[16]);
    parsed.wire_crc=Lora_Read16(&data[length-2U]);
    memcpy(parsed.payload,&data[LORA_HEADER_SIZE],parsed.payload_length);
    if(!ShapeValid(&parsed)) return LORA_DECODE_SHAPE;
    *frame=parsed; /* 解析失败不覆盖调用者已有对象。 */
    return LORA_DECODE_OK;
}

uint8_t Lora_BuildAck(const LoraFrame_t *received,LoraFrame_t *ack)
{
    if(received==0 || ack==0 || received->flags!=LORA_FLAG_ACK || !ShapeValid(received)) return 0U;
    *ack=(LoraFrame_t){.kind=LORA_KIND_ACK,.source=received->target,.target=received->source,
        .sequence=received->sequence,.session=received->session,.transaction=received->transaction,.payload_length=3U};
    ack->payload[0]=received->kind;
    Lora_Write16(&ack->payload[1],received->wire_crc);
    return 1U;
}

uint8_t Lora_ResultValid(const LoraFrame_t *result)
{
    return (uint8_t)(result!=0 && result->kind==LORA_KIND_RESULT && ShapeValid(result) &&
        result->payload[0]>=LORA_COMMAND_MODE && result->payload[0]<=LORA_COMMAND_STATUS &&
        result->payload[2]<=LORA_RESULT_SAFETY_LOCK && result->payload[3]<=2U &&
        result->payload[4]<=1U && result->payload[5]<=4U && result->payload[6]<=1U &&
        result->payload[7]<=7U && Lora_Read16(&result->payload[8])<=1000U &&
        (result->payload[12]&0xFCU)==0U && result->payload[13]==0U &&
        Lora_Read32(&result->payload[18])>=result->transaction);
}
