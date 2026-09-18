/**
  ******************************************************************************
  * @file    CO2.h
  * @brief   CO2 传感器驱动对外接口（USART2 中断接收 + 0x2C 协议解析）
  ******************************************************************************
  */
#ifndef CO2_H
#define CO2_H

#include "main.h"
#include "usart.h"

#define CO2_RX_BUF_SIZE 6 /* 一帧字节数：帧头1+浓度2+保留2+校验1（必须与协议一致，勿改） */

extern uint8_t co2_buffer[CO2_RX_BUF_SIZE]; // CO2接收缓冲
extern volatile uint8_t co2_rx_len;         // CO2接收长度

void CO2_UART_Callback(uint16_t Size);
void CO2_UART_Receive_Start(void);
uint8_t CO2_get_data(uint16_t *co2_value, uint32_t timeout);
#endif
