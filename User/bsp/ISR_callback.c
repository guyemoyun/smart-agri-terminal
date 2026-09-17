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

/**
  * @brief  GPIO 外部中断统一回调（重写 HAL 弱符号）：所有 EXTI 中断最终进入此函数
  * @note   中断链路：按键按下引脚被拉低 → 下降沿触发 EXTI → NVIC 分发到
  *         EXTI15_10_IRQHandler()（stm32f1xx_it.c）→ HAL 清挂起标志后回调本函数。
  *         SW1(PB12)/SW2(PB13) 共用 EXTI15_10 一条中断线，必须用形参 GPIO_Pin 区分。
  *         ⚠ 当前只实现 SW1 → 翻转 PC13 板载灯；SW2 分支按需添加（参考 SW1 写法）。
  *         ⚠ 消抖三板斧：延时 15ms 躲抖动 → 复读引脚确认仍为低 → while 等松手再消抖。
  *         ⚠ 回调运行在中断上下文，内部的忙等会阻塞同/低优先级中断，课程级可用；
  *         工程上建议改为"中断记时间戳 + 主循环消抖"。
  * @param  GPIO_Pin  uint16_t，触发中断的引脚号（SW1=GPIO_PIN_12，SW2=GPIO_PIN_13）
  * @retval 无
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
        if (GPIO_Pin == SW1_Pin)   // 判断是否是KEY1引脚触发的中断
        {
            delay_ms(15);  // 消抖按下瞬间的电压
            if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET)
            {
                    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // 翻转LED
                    delay_ms(15);// 消抖松开时瞬间的电压
                    while(HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET); // 等待高电平按键松开 
            }
        }
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
//#include "ISR_callback.h"
//#include "app_main.h"
//#include "debug.h"

//// 定义自己的EXTI中断调用的函数
//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
//{
//        if (GPIO_Pin == SW1_Pin)   // 判断是否是KEY1引脚触发的中断
//        {
//            delay_ms(15);  // 消抖按下瞬间的电压
//            if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET)
//            {
//                    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // 翻转LED
//                    delay_ms(15);// 消抖松开时瞬间的电压
//                    while(HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET); // 等待高电平按键松开 
//            }
//        }
//}

//// 定义自己的UART接收完成调用的函数
//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if(huart->Instance == USART1)    // 串口1接收定长数据触发中断
//    {
//        debug_UART_test2_Callback();    
//    }
//}
// 定义自己的EXTI中断调用的函数
/*
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SW1_Pin)	// 判断是否是SW1引脚触发的中断
    {
        delay_ms(15);  // 消抖按下瞬间的电压
        if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET)
        {
            HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);	// 翻转LED
            while(HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET); // 等待高电平按键松开 
            delay_ms(15);// 消抖松开时瞬间的电压
  
        }
    }
}
*/
/**
  * @brief  简易软件延时函数（毫秒级，非精确延时）
  * @note   通过空循环消耗 CPU 周期实现延时。延时精度与系统主频（本工程 72MHz）
  *         和编译器优化等级有关，仅适合按键消抖这类对精度要求不高的场合；
  *         需要精确延时时请使用 HAL_Delay()。
  * @param  ms: 需要延时的毫秒数
  * @retval 无
  */

//void delay_ms(uint32_t ms)
//{
//	volatile uint32_t i,j;           /* volatile 修饰：防止编译器把空循环优化掉 */
//	uint32_t calibrate = 8000;       /* 内层循环标定值：72MHz 主频下约消耗 1ms */

//	for(j=0;j<ms;j++)                /* 外层循环：控制延时总毫秒数 */
//{
		//for(i=0;i<calibrate;i++)      /* 内层循环：消耗约 1ms 时间片 */
		//{

		//}
	//}
//}

/**
  * @brief  GPIO 外部中断回调函数（所有 EXTI 中断最终都会进入这里）
  * @note   完整中断链路：
  *           按键按下 → PB12/PB13 被拉低产生下降沿（下降沿触发，内部上拉）
  *           → EXTI 挂起标志置位 → NVIC 分发到 EXTI15_10_IRQHandler()（stm32f1xx_it.c）
  *           → HAL_GPIO_EXTI_IRQHandler() 自动清除挂起标志后回调本函数
  *         注意：SW1/SW2 共用 EXTI15_10 这一条中断线，因此必须用形参 GPIO_Pin
  *           区分本次中断到底来自哪个引脚。
  *         消抖原理：检测到下降沿后先延时 15ms，等机械抖动平息后再读引脚电平，
  *           若仍为低电平才确认是有效按下；松开瞬间抖动同理再消抖一次。
  * @param  GPIO_Pin: 触发中断的引脚号（SW1 = GPIO_PIN_12，SW2 = GPIO_PIN_13）
  * @retval 无
  */
//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
//{
//  /* ---------- SW1（PB12，低电平有效）：每按一次翻转 LED2（PA8，低电平点亮） ---------- */
//  if (GPIO_Pin == SW1_Pin) // 判断是否是 SW1 引脚触发的中断
//  {
//    delay_ms(15); // 延时消抖：躲过按键按下瞬间的机械抖动（一般 5~10ms）
//    if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET) // 复读引脚确认仍为低电平，排除抖动造成的误触发
//    {
//      HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin); // 翻转 LED2：每按一次在亮/灭之间切换一次
//      while(HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET); // 死等按键松开（读回高电平），防止长按期间反复翻转
//      delay_ms(15); // 松开瞬间同样存在抖动，再消抖一次
//    }
//  }
//  /* ---------- SW2（PB13，低电平有效）：每按一次翻转蜂鸣器（PB9，高电平响） ---------- */
//  if (GPIO_Pin == SW2_Pin) // 判断是否是 SW2 引脚触发的中断
//  {
//    delay_ms(15); // 延时消抖：躲过按下瞬间的机械抖动
//    if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET) // 复读引脚确认确实按下（低电平有效）
//    {
//      HAL_GPIO_TogglePin(BEEP_GPIO_Port, BEEP_Pin); // 翻转蜂鸣器：有源蜂鸣器，高电平响，每按一次在响/停之间切换
//      while(HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET); // 等待按键松开，避免长按反复翻转
//      delay_ms(15); // 松开瞬间消抖
//    }
//  }
//}
