#include "lora_reliable.h"
#include <string.h>

void LoraReliable_Init(LoraReliable_t *tx) { memset(tx,0,sizeof(*tx)); }

uint8_t LoraReliable_Begin(LoraReliable_t *tx,const LoraFrame_t *message,uint32_t now)
{
    if(tx==0 || message==0 || (message->flags!=LORA_FLAG_ACK && message->flags!=LORA_FLAG_RESULT)) return 0U;
    if(tx->state==LORA_TX_READY || tx->state==LORA_TX_SENDING || tx->state==LORA_TX_WAITING) return 0U;
    uint8_t wire[LORA_MAX_FRAME];
    const size_t length=Lora_Encode(message,wire,sizeof(wire));
    if(length==0U) return 0U;
    LoraReliable_Init(tx);
    memcpy(tx->wire,wire,length);tx->length=(uint8_t)length;
    (void)Lora_Decode(wire,length,&tx->message);
    tx->state=LORA_TX_READY;tx->phase_tick=now;
    return 1U;
}

uint8_t LoraReliable_NeedsSend(const LoraReliable_t *tx,uint32_t now)
{ return (uint8_t)(tx->state==LORA_TX_READY && (now-tx->phase_tick)>=tx->ready_delay); }

void LoraReliable_MarkSending(LoraReliable_t *tx)
{
    if(tx->state!=LORA_TX_READY || tx->attempts>=LORA_MAX_ATTEMPTS) return;
    ++tx->attempts;tx->state=LORA_TX_SENDING;
}

static void RetryOrFinish(LoraReliable_t *tx,uint32_t now)
{
    tx->phase_tick=now;
    if(tx->attempts>=LORA_MAX_ATTEMPTS) tx->state=LORA_TX_EXHAUSTED;
    else { tx->state=LORA_TX_READY;tx->ready_delay=LORA_RETRY_DELAY_MS; }
}

void LoraReliable_TxFinished(LoraReliable_t *tx,uint8_t success,uint32_t now)
{
    if(tx->state!=LORA_TX_SENDING) return;
    if(!success) { RetryOrFinish(tx,now);return; }
    tx->state=LORA_TX_WAITING;tx->phase_tick=now;
}

void LoraReliable_Tick(LoraReliable_t *tx,uint32_t now)
{
    if(tx->state==LORA_TX_WAITING && (now-tx->phase_tick)>=LORA_ACK_WAIT_MS) RetryOrFinish(tx,now);
}

uint8_t LoraReliable_AcceptReply(LoraReliable_t *tx,const LoraFrame_t *reply)
{
    if(reply==0 || tx->attempts==0U ||
        (tx->state!=LORA_TX_WAITING && tx->state!=LORA_TX_READY)) return 0U;
    const LoraFrame_t *sent=&tx->message;
    if(reply->source!=sent->target || reply->target!=sent->source || reply->sequence!=sent->sequence ||
        reply->session!=sent->session || reply->transaction!=sent->transaction) return 0U;
    if(sent->flags==LORA_FLAG_ACK)
    {
        if(reply->kind!=LORA_KIND_ACK || reply->flags!=0U || reply->payload_length!=3U ||
            reply->payload[0]!=sent->kind || Lora_Read16(&reply->payload[1])!=sent->wire_crc) return 0U;
    }
    else
    {
        /* COMMAND 的链路 ACK 不能代替业务执行结果。 */
        if(!Lora_ResultValid(reply) || reply->payload[0]!=sent->payload[0] ||
            reply->payload[1]!=sent->payload[1]) return 0U;
    }
    tx->state=LORA_TX_COMPLETE;
    return 1U;
}
