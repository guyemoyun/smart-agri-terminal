#ifndef CO2_H
#define CO2_H

#include "main.h"
#include "usart.h"

#define CO2_RX_BUF_SIZE 6U

extern volatile uint8_t co2_rx_len;

void CO2_UART_Callback(uint16_t Size);
void CO2_UART_Receive_Start(void);
uint8_t CO2_TakeFrame(uint8_t frame[CO2_RX_BUF_SIZE]);
uint8_t CO2_ParseFrame(const uint8_t frame[CO2_RX_BUF_SIZE], uint16_t *co2_value);

/* Legacy blocking API retained for compatibility; new code should use the non-blocking APIs above. */
uint8_t CO2_get_data(uint16_t *co2_value, uint32_t timeout);

#endif
