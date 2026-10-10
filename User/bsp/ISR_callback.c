/**
  ******************************************************************************
  * @file    ISR_callback.c
  * @brief   HAL 中断回调统一分发层：EXTI 按键回调 + UART 接收事件分发
  * @note    HAL 库把所有外设中断最终汇聚到几个弱符号回调函数（weak），
  *          用户在任意 .c 中重写同名函数即可接管。本文件集中管理所有回调，
  *          避免重名冲突（全工程同一弱符号只能重写一次！），职责只做"分发"，
  *          具体业务转给各模块（debug.c / CO2.c），保证中断里快进快出。
  ******************************************************************************
  */
#include "ISR_callback.h"
#include "app_main.h"
#include "debug.h"
#include "co2.h" // CO2_UART_Callback 声明（缺少会报 #223-D implicit declaration）
#include "llcc68_p2p.h"
#include "button_service.h"

/* DIO1 与两按键共享 EXTI15_10，只通知主循环，不执行 SPI 或消抖等待。 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == LORA_DIO1_Pin) DIO1_EXTI_Callback();
    else Button_NotifyIRQ(GPIO_Pin);
}

/**
  * @brief  UART 接收事件统一回调（重写 HAL 弱符号）：ReceiveToIdle 模式下
  *         "缓冲区收满" 或 "总线空闲(IDLE) 判帧结束" 时由 HAL 调用
  * @note   按串口实例分发（huart->Instance 区分）：
  *           USART1（PA9/PA10 电脑调试口）→ debug_UART_Callback()：记帧长+重开接收
  *           USART2（PA2/PA3 CO2 模块口） → CO2_UART_Callback()：记帧长+重开接收
  *         ⚠ 运行在中断上下文：只允许"记录长度 + 重开接收"这类快进快出操作，
  *         数据的业务处理一律放主循环；禁止在回调里 printf/阻塞发送。
  * @param  huart  UART_HandleTypeDef*，触发事件的串口实例（据此分发到不同模块）
  * @param  Size   uint16_t，本次接收到的实际字节数
  * @retval 无
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if(huart->Instance == USART1) // 串口1接收变长数据触发中断（收满128字节或总线空闲IDLE）
    {
        debug_UART_Callback(Size); // 转发到 debug.c：记录帧长 debug_rx_len 并自动重开接收
    }
    if(huart->Instance == USART2) // 串口2接收定长数据触发中断
    {
        CO2_UART_Callback(Size);    
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) CO2_UART_Error();
}
