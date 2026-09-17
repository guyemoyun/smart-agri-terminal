//#include "debug.h"
//#include "usart.h" // STM32CubeMx生成

//void debug_test1(void)
//{
//    uint8_t buff[10];
//    while (1)
//    {
//        // 指定串口1接收10字节数据，HAL_MAX_DELAY阻塞永久等待，
//        // 直到接收到数据。如果接收到数据，则返回HAL_OK    。
//        // 如果你们不希望阻塞，可以指定一个超时时间，如1000 (1000ms)。
//        if (HAL_UART_Receive(&huart1, buff, 10, HAL_MAX_DELAY) == HAL_OK)
//        {
//            // 把收到的数据原封不动的发出去, HAL_MAX_DELAY阻塞永久等待
//            // 你也可以单独调用该函数主动发送你需要的数据
//            HAL_UART_Transmit(&huart1, buff, 10, HAL_MAX_DELAY);
//        }
//    }
//}

//#include "debug.h"
//#include "usart.h" // STM32CubeMx生成

//uint8_t buff[10];
//// 这个函数被HAL_UART_RxCpltCallback()调用
//void debug_UART_test2_Callback(void)
//{
//    // 把收到的数据原封不动的发出去
//    HAL_UART_Transmit(&huart1, buff, 10, HAL_MAX_DELAY);
//
//    // 重新用中断的方式接收10个字节的数据，继续接收
//    HAL_UART_Receive_IT(&huart1, buff, 10);
//}

//// 测试串口1中断接收定长数据
//void debug_test2(void)
//{
//    // 用中断的方式接收10个字节的数据
//    HAL_UART_Receive_IT(&huart1, buff, 10);
//}

//// 测试串口1中断接收变长数据(<=10字节，回显版)
//#include "debug.h"
//#include "usart.h" // STM32CubeMx生成

//uint8_t buff[10];

//// 这个函数被HAL_UARTEx_RxEventCallback()调用
//void debug_UART_test3_Callback(uint16_t Size)
//{
//    // 把收到的数据原封不动的发出去
//    HAL_UART_Transmit(&huart1, buff, Size, HAL_MAX_DELAY);
//
//    // 重新用中断的方式接收到10个字符或者碰到空闲帧, 则接收结束
//    HAL_UARTEx_ReceiveToIdle_IT(&huart1, buff, 10);
//}

//// 测试串口1中断接收变长数据
//void debug_test3(void)
//{
//    // 当接收到10个字符或者碰到空闲帧, 则接收结束(<=10个字节的都能收到)
//    HAL_UARTEx_ReceiveToIdle_IT(&huart1, buff, 10);
//}

/**
  * ======================= 串口1 中断接收变长数据 =======================
  * 原理：HAL_UARTEx_ReceiveToIdle_IT() 在【接收缓冲区满】或【总线空闲(IDLE)】
  *       时结束本次接收并触发 USART1 中断 → HAL 层转发到
  *       ISR_callback.c 的 HAL_UARTEx_RxEventCallback(huart, Size)，
  *       其中 Size = 本次实际收到的字节数（变长的关键）。
  * 用法：上电调用一次 Debug_UART_Receive_Start() 开启接收；
  *       每收到一帧，debug_rx_len 记录帧长，主循环读取 debug_buffer 处理，
  *       处理完把 debug_rx_len 清 0。回调内自动重开接收，无需干预。
  * =====================================================================
  */
#include "debug.h"
#include "usart.h" // STM32CubeMx生成
#include <string.h>

uint8_t debug_buffer[DEBUG_RX_BUF_SIZE]; // debug接收缓冲  DEBUG_RX_BUF_SIZE=>128
volatile uint16_t debug_rx_len;          // 中断写、主循环读，必须 volatile

/**
  * @brief  串口1 变长接收事件回调（由 ISR_callback.c 的 HAL_UARTEx_RxEventCallback 转发）
  * @param  Size: 本次接收到的实际字节数
  * @note   运行在中断上下文：只记录长度 + 立即重开接收，不做耗时处理（回显等放主循环）
  */
void debug_UART_Callback(uint16_t Size)
{
    debug_rx_len = Size; // 记录本帧长度，供主循环读取 debug_buffer 使用
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, debug_buffer, sizeof(debug_buffer)); // 继续开启接收数据
}

/**
  * @brief  启动串口1中断接收变长数据（上电只需调用一次）
  * @note   收满 DEBUG_RX_BUF_SIZE(128) 字节或线路空闲(IDLE) 即产生接收事件
  */
void Debug_UART_Receive_Start(void)
{
    debug_rx_len = 0;                                            // 清空接收长度标志
    memset(debug_buffer, 0, sizeof(debug_buffer));               // 清空接收缓冲区
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, debug_buffer, sizeof(debug_buffer)); // 开启空闲中断接收
}

