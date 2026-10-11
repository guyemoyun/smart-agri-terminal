/**
  ******************************************************************************
  * @file    app_main.c
  * @brief   应用主逻辑：环境采集、本地安全控制、OLED 页面及 LoRa 遥测
  * @note    调用关系：main() 完成 HAL/时钟/外设初始化后调用 app_main()，
  *          由它接管主循环（CubeMX 工程的用户业务统一从这里写，不写进 main.c）。
  *
  *          本文件包含：
  *          1. delay_ms()  —— 软件空循环毫秒延时（按键消抖等粗延时场合）
  *          2. delay_us()  —— DWT 微秒精确延时（DHT22 单总线时序依赖它）
  *          3. __io_putchar() —— GCC/newlib printf 重定向到 USART1（PA9 → CH340 → 电脑）
  *          4. app_main()  —— 主循环：按键/UI、采集、控制、LoRa 前台任务
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
#include "oled.h"
#include "sensor_service.h"
#include "control_service.h"
#include "terminal_ui.h"
#include "lora_service.h"
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
#if defined(__GNUC__)
__attribute__((optimize("O2")))
#endif
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



volatile uint16_t adc_values[2];

void app_main(void)
{
    uint32_t tick_heartbeat = HAL_GetTick();

    /* 执行器先初始化为关闭，默认 MANUAL，不会自动启动水泵或风扇。 */
    Sensor_ServiceInit();
    Control_ServiceInit(HAL_GetTick());
    Terminal_UIInit(HAL_GetTick());
    Lora_ServiceInit();
    OLED_Init();
    Debug_UART_Receive_Start();
    CO2_UART_Receive_Start();
    DHT22_Init();

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK ||
        HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_values, 2U) != HAL_OK)
    {
        Actuator_AllSafeOff();
        Error_Handler();
    }
    printf("Terminal ready: MANUAL, outputs off, duplex v3\r\n");

    while (1)
    {
        uint32_t now = HAL_GetTick();
        if ((now - tick_heartbeat) >= 500U)
        {
            tick_heartbeat = now;
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        }
        /* 控制与按键前后检查，采集/射频操作之间也能执行超时保护。 */
        Terminal_UITask(now);
        Control_ServiceTask(HAL_GetTick());
        Sensor_ServiceTask(HAL_GetTick());
        Control_ServiceTask(HAL_GetTick());
        Terminal_UITask(HAL_GetTick());
        Lora_ServiceTask(HAL_GetTick());
        Control_ServiceTask(HAL_GetTick());
    }
}
