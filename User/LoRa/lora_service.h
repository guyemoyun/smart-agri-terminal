#ifndef LORA_SERVICE_H
#define LORA_SERVICE_H
#include <stdint.h>

typedef struct
{
    uint8_t ready, tx_busy;
    uint16_t last_sequence;
    uint32_t tx_done_count, tx_error_count, init_error_count;
} LoraStatus_t;
void Lora_ServiceInit(void);
void Lora_ServiceTask(uint32_t now);
void Lora_GetStatus(LoraStatus_t *status);
#endif
