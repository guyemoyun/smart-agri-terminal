/**
  ******************************************************************************
  * @file    dht22.c
  * @brief   DHT22 温湿度传感器驱动（单总线半双工，数据脚 PC15）
  * @note    通信原理（主机掌控全部时序）：
  *          ① 主机拉低 ≥1ms 发起始信号 → 释放总线拉高 20~40us；
  *          ② 传感器应答：拉低 80us + 拉高 80us；
  *          ③ 传感器连发 40 位：每位以约 50us 低电平开头，随后高电平宽度定值——
  *             26~28us = "0"，70us = "1"（本驱动用"延时 30us 后采样"法区分）；
  *          ④ 40 位 = 湿度16位 + 温度16位(最高位=负号) + 校验和8位(前4字节和低8位)。
  *          ⚠ 时序精度依赖 delay_us()（DWT 实现），改主频会连带影响时序。
  *          ⚠ 传感器两次读取需 ≥2s 恢复时间（上层主循环 2s 周期已满足）。
  *          ⚠ PC15 数据线底板无外部上拉，依赖传感器模块板载上拉；主机读数时
  *             引脚切浮空输入，写起始信号时才切推挽输出（避免总线驱动冲突）。
  ******************************************************************************
  */
#include "dht22.h"
#include "main.h"
#include "app_main.h"
#include <stdio.h>

/************************* 私有函数声明 *************************/
/* 以下 4 个 static 函数仅本文件内部使用；对外只暴露 DHT22_Init / DHT22_ReadData */

static void DHT22_SetOutputMode(void);      /* 数据脚切推挽输出（仅发起始信号的几 ms 内使用） */
static void DHT22_SetInputMode(void);       /* 数据脚切浮空输入（读数据期间释放总线） */
static uint8_t DHT22_SendStartSignal(void); /* 发起始信号+等应答 @return 0=成功 1/2=超时 */
static uint8_t DHT22_ReadByte(void);        /* 按位收 1 字节 @return 字节值，0xFF=位等待超时 */

/************************* 引脚模式配置 *************************/
/**
  * @brief  数据脚切为推挽输出（仅主机发送起始信号的几毫秒内使用）
  * @note   单总线是"类开漏"约定：主机与传感器都靠拉低说话。输出态只在起始信号
  *         期间短暂存在，随后必须切回输入释放总线，否则推挽输出会与传感器
  *         驱动"顶牛"，可能损坏引脚。
  * @retval 无
  */
static void DHT22_SetOutputMode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &GPIO_InitStruct);
}

/**
  * @brief  数据脚切为浮空输入，把总线控制权交给传感器
  * @note   之后引脚电平完全由传感器与上拉电阻决定，主机只读不写。
  *         输入配置为 NOPULL：上拉由传感器模块板载电阻提供（底板无上拉）。
  * @retval 无
  */
static void DHT22_SetInputMode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;     // 输入模式
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &GPIO_InitStruct);
}


/**
  * @brief  DHT22 初始化（预留扩展点）
  * @note   引脚时钟与 GPIO 模式已由 CubeMX 在 MX_GPIO_Init() 中配置完成，
  *         此处当前无需额外动作；保留函数以维持驱动接口完整性，
  *         使用约定：首次调用 DHT22_ReadData() 前先调用一次本函数。
  * @retval 无
  */
void DHT22_Init(void)
{
    // __HAL_RCC_GPIOA_CLK_ENABLE();  // 使能GPIOA时钟（根据实际引脚修改）
    
    // DHT22_SetOutputMode();
    // HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);  // 空闲状态为高电平
}

/**
  * @brief  读取一次 DHT22 温湿度（完整单总线通信，忙等阻塞约 4~5ms）
  * @note   分步流程：
  *           ① SendStartSignal()：主机发起始信号并等待传感器应答；
  *           ② 连续 5 次 ReadByte() 收满 40 位数据；
  *           ③ 校验：前 4 字节之和的低 8 位 == 第 5 字节，过滤误码；
  *           ④ 解析：湿度 = (b0<<8|b1)/10 %RH；
  *              温度 = (b2<<8|b3)/10 ℃，b2 最高位为 1 表示负温（清符号位后取负）。
  *         ⚠ 风险：忙等阻塞主循环；且 DHT22 两次读取需间隔 ≥2s，勿连续快读。
  *         ⚠ 校验失败返回 3 时 temp/humi 未被写入，调用方必须检查返回值再使用。
  * @param  temp  float*，出参：温度（℃，精度 0.1℃，支持负温），仅返回 0 时有效
  * @param  humi  float*，出参：相对湿度（%RH，精度 0.1%），仅返回 0 时有效
  * @return uint8_t  0:成功  1:起始应答超时  2:数据位接收超时  3:校验和失败
  */
uint8_t DHT22_ReadData(float *temp, float *humi)
{
    uint8_t buf[5] = {0};  // 存储40位数据：湿度高8位、湿度低8位、温度高8位、温度低8位、校验和
    uint8_t i, ret = 0;

    // ① 发送起始信号并等待应答
    ret = DHT22_SendStartSignal();
    if(ret != 0)
    {
        return 1;  // 响应超时
    }
    
    // 2. 读取40位数据（5个字节）
    for(i = 0; i < 5; i++)
    {
        buf[i] = DHT22_ReadByte();
        if(buf[i] == 0xFF)  // 读取字节超时
        {
            return 2;
        }
    }
    
    DHT22_SetOutputMode();
    // 主机释放总线（拉高）
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);

    // 3. 校验数据（前4字节之和的低8位等于第5字节）
    if(((buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF) != buf[4])
    {
        return 3;  // 校验失败
    }
    
    // 4. 解析温湿度数据
    // 湿度：(buf[0]<<8 | buf[1]) / 10.0 （单位：%RH）
    *humi = (float)((buf[0] << 8) | buf[1]) / 10.0f;
    
    // 温度：(buf[2]<<8 | buf[3]) / 10.0 （单位：℃，buf[2]最高位为1表示负温度）
    if(buf[2] & 0x80)  // 负温度
    {
        *temp = (float)(((buf[2] & 0x7F) << 8) | buf[3]) / -10.0f;
    }
    else  // 正温度
    {
        *temp = (float)((buf[2] << 8) | buf[3]) / 10.0f;
    }
    
    return 0;  // 读取成功
}

/************************* 私有函数实现 *************************/
/**
  * @brief  发送主机起始信号并等待传感器应答
  * @note   时序分解：
  *           拉低 ≥1ms（规格要求 500~800us，取 1ms 留裕量）
  *           → 拉高 30us（释放总线）→ 切输入 →
  *           等传感器拉低（应答开始，超时 100us 判定无传感器）
  *           → 等传感器结束 80us 应答低电平（拉高）。
  *         每个等待循环都带超时计数：传感器未接/接触不良/线断时返回错误码，
  *         而不是死循环卡死主循环。
  * @return uint8_t  0:应答成功  1:等待应答超时  2:应答低电平阶段超时
  */
static uint8_t DHT22_SendStartSignal(void)
{
    uint32_t timeout = 0;

    // ① 主机拉低总线至少 500us（实际拉 1ms，裕量充足）
    DHT22_SetOutputMode();
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);  
    
    // 2. 主机释放总线（拉高），等待从机响应
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
    delay_us(30);  // 拉高30us
    DHT22_SetInputMode();
    
    timeout = 100;
    while(HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_SET)
    {
        delay_us(1);
        if(--timeout == 0)
        {
            return 1;  // 响应超时
        }
    }

    // 4. 等待从机释放总线（从机拉高总线80us）
    timeout = 100;
    while(HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_RESET)
    {
        delay_us(1);
        if(--timeout == 0)
        {
            return 2;  // 响应超时
        }
    } 
    return 0;  // 响应成功
}

/**
  * @brief  从单总线读取 1 个字节（8 bit，高位在前）
  * @note   每个位以约 50us 低电平开头，随后的高电平宽度决定位值。
  *         判别采用"延时采样法"：等到线变高后 delay_us(30) 再采样——
  *           已变低 → 高电平只持续 26~28us → 判 0；
  *           仍为高 → 高电平将持续 70us → 判 1。
  *         30us 恰好落在 0/1 两种高电平宽度之间，一次采样即可判别，实现最简。
  *         ⚠ 判别裕量提示：0 的高电平上限 28us 与采样点 30us 只有约 2us 裕量，
  *         若偶发校验失败，可把采样点后移到 40~50us 增大裕量。
  *         每个等待循环带超时计数（约 100us），线卡死时返回 0xFF 交由上层处理。
  * @return uint8_t  读到的字节值；0xFF 表示某位等待超时（上层按错误处理）
  */
static uint8_t DHT22_ReadByte(void)
{
    uint8_t byte = 0;
    uint32_t timeout = 0;
    uint8_t i;

    for(i = 0; i < 8; i++)
    {
        byte <<= 1;  // 高位在前：先收到的 bit 存高位，左移腾出低位
        // 等待从机拉低总线
        timeout = 100;
        while(HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_SET)
        {
            delay_us(1);
            if(--timeout == 0)
            {
                return 0xFF;  // 超时
            }
        }

        // 等待从机拉高总线（每bit起始信号：50us低电平）
        timeout = 60;
        while(HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_RESET)
        {
            delay_us(1);
            if(--timeout == 0)
            {
                return 0xFF;  // 超时
            }
        }
        
        // 检测高电平持续时间：
        // 0bit：26~28us 高电平；1bit：70us 高电平
        delay_us(30);  // 等待30us后检测电平
        if(HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) == GPIO_PIN_SET)
        {
            byte |= 0x01;  // 高电平持续超过50us，为1
        }   
    }
    
    return byte;
}

