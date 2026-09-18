/**
  ******************************************************************************
  * @file    app_main.c
  * @brief   应用主逻辑：CO2/DHT22 传感器采集 + 蜂鸣器 PWM 温度报警 + 串口调试
  * @note    调用关系：main() 完成 HAL/时钟/外设初始化后调用 app_main()，
  *          由它接管主循环（CubeMX 工程的用户业务统一从这里写，不写进 main.c）。
  *
  *          本文件包含：
  *          1. delay_ms()  —— 软件空循环毫秒延时（按键消抖等粗延时场合）
  *          2. delay_us()  —— DWT 微秒精确延时（DHT22 单总线时序依赖它）
  *          3. __io_putchar() —— GCC/newlib printf 重定向到 USART1（PA9 → CH340 → 电脑）
  *          4. app_main()  —— 主循环：1s 任务(CO2+心跳灯) / 2s 任务(DHT22+温度控响度)
  *
  *          ⚠ 工程约定：printf 字符串只允许 ASCII（ARMCC 按 GBK 解析源码，
  *          UTF-8 中文字符串会触发 #870-D 警告且串口输出乱码）；注释可用中文。
  ******************************************************************************
  */
/* 头文件说明：
 * app_main.h —— 本模块接口（delay_ms/delay_us/app_main 声明）
 * main.h     —— CubeMX 引脚宏（LED_Pin 等）+ HAL 总头文件
 * tim.h      —— CubeMX 定时器初始化（htim1=LED PWM，htim4=蜂鸣器 PWM）
 * debug.h    —— USART1 调试串口变长接收（debug_buffer/debug_rx_len）
 * co2.h      —— CO2 驱动（USART2，0x2C 协议 6 字节定长帧）
 * dht22.h    —— DHT22 驱动（PC15 单总线）
 */
#include "app_main.h"
#include "main.h"
#include "PWM.h"
#include "tim.h"
#include "debug.h"
#include "stdio.h"
#include "usart.h"
#include "co2.h"
#include "dht22.h"
#include "light.h"
#include "soil.h"
#include "adc.h" // hadc1/hdma_adc1 与 MX_ADC1_Init 声明（HAL_ADC_Start_DMA 需要 &hadc1）
#include "oled_demo.h"
#include "sensor_service.h"
/**
  * @brief  软件空循环毫秒级延时（72MHz 主频下标定）
  * @note   原理：双层空循环消耗 CPU 周期实现延时。实测精度 1ms 误差 ±0.02ms、
  *         1000ms 误差 ±15ms，满足按键消抖等粗延时场合。
  *         ⚠ 风险1：忙等阻塞式延时，期间 CPU 空转，禁止在中断上下文调用；
  *         毫秒级以上精确延时建议用 HAL_Delay()（基于 SysTick）。
  *         ⚠ 风险2：calibrate 标定值与 72MHz 主频绑定，改 SystemClock_Config
  *         的主频后必须重新标定，否则延时成比例偏差。
  * @param  ms  uint32_t，延时毫秒数（值过大时会长时间霸占主循环）
  * @retval 无
  */
void delay_ms(uint32_t ms)
{
    volatile uint32_t i, j;
    // ? 72MHz主频精准校准值，一行修改完成适配，核心逻辑不变
    uint32_t calibrate = 8000;  // 该值严格对应72MHz下约1ms延时
    
    for(j = 0; j < ms; j++)
    {
        for(i = 0; i < calibrate; i++)
        {
            // volatile变量保证空循环不被编译器优化，延时有效
            ;
        }
    }
}

/**
  * @brief  微秒级精确延时：基于 Cortex-M3 DWT 周期计数器（Data Watchpoint & Trace）
  * @note   原理：DWT->CYCCNT 寄存器随内核时钟自动 +1，用"当前值-起始值"做差
  *         即可精确计时。72MHz 下分辨率 ≈ 13.9ns，比软件空循环精确得多。
  *         选型原因：TIM1 已被 LED PWM 占用、TIM4 已被蜂鸣器 PWM 占用，
  *         DWT 方案不消耗任何定时器外设，也无需 CubeMX 额外配置。
  *         ⚠ 采用懒加载：首次调用时才使能 DWT 计数器（static 标志保证只初始化一次）。
  *         ⚠ 同为忙等阻塞延时，禁止在中断上下文调用。
  * @param  us  uint32_t，延时微秒数（内部 us*72 需小于 2^32，即 us < 约 59.6 秒）
  * @retval 无
  */
void delay_us(uint32_t us)
{
    static uint8_t initialized = 0;
    if (!initialized) // 首次调用：使能 DWT 周期计数器（只需一次）
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // 打开调试跟踪模块（DWT 的总开关）
        DWT->CYCCNT = 0;                          // 周期计数器清零
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;      // 启动计数（每时钟周期 +1，72 计数 = 1us）
        initialized = 1;
    }

    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000); // 72MHz 下每 us 计 72 个周期
    while ((DWT->CYCCNT - start) < ticks)              // 无符号减法，计数器溢出也安全
    {
        ;
    }
}

/**
  * @brief  GCC/newlib 标准输出底层接口，将 printf 字符发送到 USART1。
  *         此后 printf("...") 直接经 PA9 → 板载 CH340K → USB 输出到电脑串口助手
  * @note   Core/Src/syscalls.c 的 _write() 会调用本函数。发送设置 100ms 超时，
  *         避免串口异常时永久阻塞。
  *         printf 仍属于阻塞式输出，
  *         禁止在中断回调（如 debug_UART_Callback）中调用。
  *         ⚠ 编码约束：字符串只能 ASCII，原因见文件头工程约定。
  * @param  ch  int，待输出的字符（ASCII 码值）
  * @note   本接口由 newlib 系统调用层使用，无 FILE 参数。
  * @retval int，原样返回已输出的字符值
  */
int __io_putchar(int ch)
{
    uint8_t data = (uint8_t)ch;

    if (HAL_UART_Transmit(&huart1, &data, 1, 100) != HAL_OK)
    {
        return EOF;
    }

    return ch;
}
// #include "app_main.h"
// #include "main.h"
// #include "tim.h" // 包含STM32CubeMX生成的TIM头文件
// #include "debug.h"
// #include "co2.h"
// #include "dht22.h"



/**
  * @brief  应用主函数：接管 main() 的 while(1)，承载全部业务逻辑（本函数不返回）
  * @note   主循环采用【非阻塞时间片轮询】结构（裸机开发最常用的多任务模式）：
  *           - HAL_GetTick() 提供毫秒时基，每个任务有自己的节拍变量 tick_x；
  *           - "当前时刻 - 上次执行时刻 >= 周期" 成立才执行该任务，执行完刷新节拍；
  *           - 各任务按各自周期独立运行，互不干扰。
  *         任务划分：
  *           任务1（每 1s）：PC13 心跳灯翻转 + 读取 CO2 浓度并打印；
  *           任务2（每 2s）：读取 DHT22 温湿度 → 温度映射为蜂鸣器 PWM 占空比（越热越响）。
  *         ⚠ 风险：CO2_get_data() 忙等最长 1000ms、DHT22_ReadData() 忙等约 4~5ms，
  *         会推迟其他任务的节拍（本工程周期宽松可接受）；任务继续增多时应改为
  *         状态机/事件驱动架构，避免主循环被阻塞调用拖垮。
  * @retval 无（死循环，永不返回）
  */
//void app_main(void)
//{
//    uint32_t tick_1S = HAL_GetTick();   /* 任务1节拍：上次执行的毫秒时基（超周期即触发） */
//    uint32_t tick_2S = HAL_GetTick();   /* 任务2节拍 */
//    uint16_t co2_value = 0;             /* CO2 浓度（ppm），CO2_get_data 的出参 */
//    float temp, humi;                   /* DHT22 出参：温度(℃)/湿度(%RH)。⚠ 未初始化，仅读取成功(ret==0)后才允许使用 */
//    uint8_t ret = 0;                    /* 各传感器公共返回值：0=成功，非0=各自错误码 */

//    /* ---------- 外设/模块初始化（顺序无强依赖，失败会进 Error_Handler 死循环） ---------- */
//    Debug_UART_Receive_Start(); // 启动 USART1 中断变长接收（电脑→板子，空闲帧判定）
//    CO2_UART_Receive_Start();   // 启动 USART2 中断接收（CO2 模块→板子，6 字节定长帧）
//    DHT22_Init();               // 初始化 DHT22（引脚已由 CubeMX 配好，保留扩展点）
//    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4); // 启动 TIM4_CH4 PWM（PB9 蜂鸣器，20kHz 载波；Pulse=0 上电静音）

//    // HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);         // 启动定时器1的PWM输出
//    // __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_1, 300); // 设置定时器1，CCR1为300，这时PWM占空比30%

//    while (1) // 主循环：所有任务在这里轮询执行，任何任务都不允许长期霸占 CPU
//    {
//        /* ========== 任务1：每 1s 周期（心跳灯 + CO2 采集打印） ========== */
//        if (HAL_GetTick() - tick_1S >= 1000)
//        {
//            tick_1S = HAL_GetTick(); // 先刷新节拍再干活：保证周期稳定不受任务耗时影响
//            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // PC13 心跳灯：1Hz 翻转，灯闪 = 主循环还活着（调试巡检依据）
//            ret = CO2_get_data(&co2_value, 1000); // 阻塞最长 1s 等 CO2 帧（协议/校验细节见 CO2.c）
//            if (ret)
//            {
//                printf("CO2 sensor error, ret = %d\n", ret); // 错误码含义见 CO2_get_data 注释
//            }
//            else
//            {
//                printf("CO2: %hu ppm\n", co2_value); // %hu 对应 uint16_t；%hhu 只取低 8 位会算错值
//            }
//        }

//        /* ========== 任务2：每 2s 周期（DHT22 温湿度 + 温度→蜂鸣器响度报警） ========== */
//        if (HAL_GetTick() - tick_2S >= 2000) // 2s 周期同时满足 DHT22 两次读取需 ≥2s 恢复时间的规格约束
//        {
//            tick_2S = HAL_GetTick();
//            // 读取温湿度数据（阻塞约 4~5ms，单总线时序见 dht22.c）
//            ret = DHT22_ReadData(&temp, &humi);
//            if (!ret) // 读取成功才允许使用 temp/humi（失败时二者为无效旧值）
//            {
//                /* 温度→响度映射（占空比 = CCR/(ARR+1) = CCR/3600，PWM1 模式占空比越大越响）：
//                 *   T <= 25℃          → CCR=0，静音（温度正常）
//                 *   25℃ < T < 35℃     → 线性：每升高 1℃，占空比 +10%（温度越高叫得越凶）
//                 *   T >= 35℃          → CCR=3600（100%），满功率持续报警
//                 * 调曲线只改三个数：静音阈值 25.0f / 报警阈值 35.0f / 斜率 360.0f(=3600÷10℃)
//                 */
//                uint16_t beep_ccr = 0;
//                if (temp >= 35.0f)
//                {
//                    beep_ccr = 3600;
//                }
//                else if (temp > 25.0f)
//                {
//                    beep_ccr = (uint16_t)((temp - 25.0f) * 360.0f);
//                }
//                __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, beep_ccr); // 运行中改占空比只需写 CCR，无需重新 Init
//                printf("Temp: %.1f C, Humi: %.1f %%RH, Beep: %d %%\n",
//                       temp, humi, (int)((beep_ccr * 100UL) / 3600)); // 同步打印响度百分比便于调试
//            }
//            else
//            {
//                __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0); // 读取失败先静音；若想要"传感器故障报警"可改为 3600
//                printf("DHT22 read fail, ret = %d\n", ret); // ret 含义见 dht22.h：1/2=超时 3=校验失败
//            }
//        }
//    }
//}

// 替代main函数中的while循环
// void app_main(void)
// {
	/* 上电初始电平设置（对照原理图确定极性）：
   * PC13 拉低 —— 核心板板载 LED（低电平点亮）
   * PB15 拉低 —— LED3（低电平点亮）
   * PA8  拉低 —— LED2（低电平点亮）
   * PB9  拉低 —— 蜂鸣器（高电平响，初始保持静音，等待 SW2 翻转）
   */
//	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
//	HAL_GPIO_WritePin(GPIOB, LED3_Pin, GPIO_PIN_RESET);
//  HAL_GPIO_WritePin(GPIOA, LED2_Pin, GPIO_PIN_RESET);
//	HAL_GPIO_WritePin(GPIOB, BEEP_Pin, GPIO_PIN_RESET);
	
//	LED_PWM();
//	debug_test1();  // 测试串口1轮询接收
//	debug_test2();
//	debug_test3();

	// printf("Hello world!\r\n"); // printf 已通过 fputc 重定向到串口1，上电打印一次
	// uint32_t tick_1S = HAL_GetTick();
    // uint16_t co2_value = 0; // CO2浓度值
    // uint8_t ret = 0;
	// Debug_UART_Receive_Start(); // 启动调试串口接收
    // CO2_UART_Receive_Start();   // 启动CO2传感器串口接收

	// while (1)
	// {
	// // 	HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

	// // 	// 串口命令控制 LED3：debug_rx_len>0 表示收到一帧，
	// // 	// 有效数据为 debug_buffer[0] ~ debug_buffer[debug_rx_len-1]。
	// // 	// 电脑串口助手发送 '1' → LED3 点亮；发送 '0' → LED3 熄灭。
	// // 	// 逐字节检查，兼容发送 "1\r\n"、"01 1" 这类带结束符/多命令的数据。
	// // 	if (debug_rx_len > 0)
	// // 	{
	// // 		for (i = 0; i < debug_rx_len; i++)
	// // 		{
	// // 			if (debug_buffer[i] == '1')      // 字符 '1'（ASCII 0x31）
	// // 			{
	// // 				HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
    // //                 printf("LED3 on\r\n"); // LED3 低电平点亮 → 开
	// // 			}
	// // 			else if (debug_buffer[i] == '0') // 字符 '0'（ASCII 0x30）
	// // 			{
	// // 				HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
    // //                 printf("LED3 off\r\n"); // LED3 熄灭 → 关闭
	// // 			}
	// // 		}
	// // 		debug_rx_len = 0; // 处理完清标志，等待下一帧
			

	// // 	}

	// // 	// printf("running %d s\r\n", print_cnt++); // 周期打印，验证 printf 格式化输出
    // // HAL_Delay(1000);
	//     if (HAL_GetTick() - tick_1S >= 1000)
    //     {
    //         tick_1S = HAL_GetTick();
    //         HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    //         ret = CO2_get_data(&co2_value, 1000);
    //         if (ret)
    //         {
    //             printf("CO2 sensor error, ret = %d\n", ret); // 串口输出用 ASCII：ARMCC 按 GBK 解析字符串，UTF-8 中文会触发 #870-D
    //         }
    //         else
    //         {
    //             printf("CO2: %hu ppm\n", co2_value);
    //         }
    //     }
	// }
// }
//#include "app_main.h"
//#include "main.h"
//#include "tim.h" // 包含STM32CubeMX生成的TIM头文件
//#include "debug.h"
//#include "co2.h"
//#include "dht22.h"
//#include "light.h"

volatile uint16_t adc_values[2];

void app_main(void)
{
    uint32_t tick_heartbeat = HAL_GetTick();

    oled_demo();
    Debug_UART_Receive_Start();
    CO2_UART_Receive_Start();
    DHT22_Init();

    /* STM32F1 ADC 在首次转换前执行校准，降低偏置误差。 */
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_values, 2) != HAL_OK)
    {
        Error_Handler();
    }

    Sensor_ServiceInit();
    printf("System ready\r\n");

    while (1)
    {
        uint32_t now = HAL_GetTick();

        if ((now - tick_heartbeat) >= 500U)
        {
            tick_heartbeat = now;
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        }

        Sensor_ServiceTask(now);
    }
}
