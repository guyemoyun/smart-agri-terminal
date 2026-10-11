#ifndef LORA_RELIABLE_H
#define LORA_RELIABLE_H
#include "lora_protocol.h"
#define LORA_MAX_ATTEMPTS 3U
#define LORA_ACK_WAIT_MS 2000U
#define LORA_RETRY_DELAY_MS 100U

typedef enum { LORA_TX_IDLE=0,LORA_TX_READY,LORA_TX_SENDING,LORA_TX_WAITING,LORA_TX_COMPLETE,LORA_TX_EXHAUSTED } LoraReliableState_t;
typedef struct
{
    LoraReliableState_t state;
    LoraFrame_t message;
    uint8_t wire[LORA_MAX_FRAME],length,attempts;
    uint32_t phase_tick,ready_delay;
} LoraReliable_t;

void LoraReliable_Init(LoraReliable_t *tx);
uint8_t LoraReliable_Begin(LoraReliable_t *tx,const LoraFrame_t *message,uint32_t now);
uint8_t LoraReliable_NeedsSend(const LoraReliable_t *tx,uint32_t now);
void LoraReliable_MarkSending(LoraReliable_t *tx);
void LoraReliable_TxFinished(LoraReliable_t *tx,uint8_t success,uint32_t now);
void LoraReliable_Tick(LoraReliable_t *tx,uint32_t now);
uint8_t LoraReliable_AcceptReply(LoraReliable_t *tx,const LoraFrame_t *reply);
#endif
