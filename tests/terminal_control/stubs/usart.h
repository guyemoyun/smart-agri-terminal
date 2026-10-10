#ifndef TERMINAL_TEST_USART_H
#define TERMINAL_TEST_USART_H
#include "main.h"
typedef struct { uint8_t placeholder; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart2;
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *uart,uint8_t *buffer,uint16_t size);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
#endif
