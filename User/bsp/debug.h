//#ifndef DEBUG_H
//#define DEBUG_H

//#include "main.h"
//void debug_UART_test2_Callback(void);
//void debug_test2(void);
//#endif // DEBUG_H

#ifndef DEBUG_H
#define DEBUG_H

#include "main.h"

/**
  * @brief  串口1调试接收缓冲区大小（字节）
  * @note   单帧最长可接收 DEBUG_RX_BUF_SIZE 个字节；
  *         缓冲区满时本次接收结束并触发回调，随后自动重新开启接收。
  */
#define DEBUG_RX_BUF_SIZE 128

/* 接收缓冲区与最近一帧的接收长度：
 * debug_rx_len > 0 表示收到一帧新数据（有效数据为 debug_buffer[0] ~ debug_buffer[debug_rx_len-1]）；
 * 主循环处理完数据后须把 debug_rx_len 清 0，作为"无新数据"标志。 */
extern uint8_t  debug_buffer[DEBUG_RX_BUF_SIZE]; // debug接收缓冲
extern volatile uint16_t debug_rx_len;           // 中断写、主循环读

void debug_UART_Callback(uint16_t Size); /* 被 ISR_callback.c 的 HAL_UARTEx_RxEventCallback() 调用 */
void Debug_UART_Receive_Start(void);     /* 上电调用一次：启动串口1中断接收变长数据 */

#endif // DEBUG_H

