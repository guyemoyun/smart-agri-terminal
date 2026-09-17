/**
  ******************************************************************************
  * @file    app_main.h
  * @brief   应用主逻辑对外接口
  * @note    提供主循环入口 app_main() 与两个软件延时函数。
  *          延时函数实现在 app_main.c，供本模块及其他需要时序的驱动模块
  *          （如 dht22.c 单总线时序）共同调用，全工程统一延时来源，避免重复实现。
  ******************************************************************************
  */
#ifndef __APP_MAIN_H	// 防止头文件重复包含（include guard）
#define __APP_MAIN_H
#include "main.h"
// 函数声明
/* ADC+DMA 双通道采样缓冲区（定义在 app_main.c）：
 *   [0]=Rank1=ADC_CHANNEL_0=光照(PA0)  [1]=Rank2=ADC_CHANNEL_1=土壤(PA1)
 * ⚠ volatile 必需：DMA 控制器在后台持续改写本数组，CPU 读取时不能被编译器
 *    优化缓存，否则可能读到旧值。light.c / soil.c 通过本 extern 声明访问。
 * ⚠ 命名必须与 app_main.c 的定义完全一致（曾因 adcl_values/adc_values 拼写
 *    不一致导致链接期 L6218E undefined symbol）。 */
extern volatile uint16_t adc_values[2];
void delay_ms(uint32_t ms);  /* 软件空循环毫秒延时（粗延时，按键消抖用） */
void delay_us(uint32_t us);  /* DWT 微秒精确延时（DHT22 等 us 级时序用） */
void app_main(void);         /* 应用主循环入口：main() 初始化外设后调用，不返回 */
#endif
