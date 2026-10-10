#ifndef CO2_H
#define CO2_H
#include "main.h"
#include "usart.h"
#include "co2_parser.h"

#define CO2_RX_BUF_SIZE CO2_FRAME_SIZE
#define CO2_READ_OK         0U
#define CO2_READ_NO_DATA    1U
#define CO2_READ_BAD_FRAME  3U
#define CO2_READ_OVERFLOW   4U
#define CO2_READ_ARGUMENT   5U
#define CO2_READ_UART_ERROR 6U

extern uint8_t co2_buffer[CO2_RX_BUF_SIZE];
extern volatile uint8_t co2_rx_len;
void CO2_UART_Callback(uint16_t size);
void CO2_UART_Receive_Start(void);
void CO2_UART_Error(void);
uint8_t CO2_TryRead(uint16_t *co2_value);
/* 兼容旧示例，主业务应使用 CO2_TryRead，不等待串口帧到达。 */
uint8_t CO2_get_data(uint16_t *co2_value, uint32_t timeout);
#endif
