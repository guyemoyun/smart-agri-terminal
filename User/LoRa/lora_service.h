#ifndef LORA_SERVICE_H
#define LORA_SERVICE_H
#include <stdint.h>

typedef struct
{
    uint8_t ready,tx_busy,rx_active,waiting_ack,gateway_online;
    uint16_t last_sequence;
    int8_t last_rssi_dbm,last_snr_db;
    uint32_t session;
    uint32_t tx_done_count,tx_error_count,init_error_count;
    uint32_t ack_count,retry_count,ack_timeout_count,rx_count,rx_error_count;
    uint32_t command_count,duplicate_count,conflict_count,busy_drop_count;
} LoraStatus_t;
void Lora_ServiceInit(void);
void Lora_ServiceTask(uint32_t now);
void Lora_GetStatus(LoraStatus_t *status);
#endif
