/**
  ******************************************************************************
  * @file    CO2.c
  * @brief   CO2 传感器驱动（USART2：PA2-TX / PA3-RX，9600bps，中断接收）
  * @note    协议：模块每秒主动上报一帧 6 字节定长数据（大端序）：
  *            [0]=0x2C 帧头 | [1]=浓度高8位 | [2]=浓度低8位 | [3][4]=保留 | [5]=校验和
  *            校验和 = 前 5 字节累加和的低 8 位
  *          接收设计：ReceiveToIdle 中断收满 6 字节（恰好一帧）→ 回调记录帧长并重开
  *          接收；主循环调用 CO2_get_data() 阻塞取数（超时 + 帧头校验 + 和校验）。
  *          ⚠ 硬件注意：模块座供电为 VCC5V，5V TTL 电平直连 PA2/PA3。
  ******************************************************************************
  */
#include "CO2.h"
#include <string.h>

uint8_t co2_buffer[CO2_RX_BUF_SIZE]; // 接收缓冲（长度=6，恰好一帧）
volatile uint8_t co2_rx_len;         // 最近一帧的接收长度；中断写、主循环读，必须 volatile 防止被优化

/**
  * @brief  CO2 串口接收事件回调（由 ISR_callback.c 的 HAL_UARTEx_RxEventCallback 分发）
  * @note   运行在中断上下文：只做"记录帧长 + 立即重开接收"两件事，快进快出；
  *         帧校验与浓度解析统一放在主循环的 CO2_get_data() 中，不在中断里处理。
  * @param  Size  uint16_t，本次接收到的字节数（正常应为 6，恰好一帧）
  * @retval 无
  */
void CO2_UART_Callback(uint16_t Size)
{
    co2_rx_len = Size;                                                    // 记录帧长，主循环据此判断"收到新帧"
    HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_buffer, sizeof(co2_buffer)); // 开启接收数据
    // printf("co2_rx_len = %d, uart1_buf = %s\n", co2_rx_len, co2_buffer);
}
/**
  * @brief  启动 CO2 串口中断接收（上电调用一次即可）
  * @note   清零帧长标志与缓冲区后开启 ReceiveToIdle 中断接收；
  *         之后每帧结束由 CO2_UART_Callback 自动重开接收，无需重复调用。
  * @retval 无
  */
void CO2_UART_Receive_Start(void)
{
    co2_rx_len = 0;                            // 清帧长标志（0 = 暂无新数据）
    memset(co2_buffer, 0, sizeof(co2_buffer)); // 清空接收缓冲，防止残留旧帧数据
    HAL_UARTEx_ReceiveToIdle_IT(&huart2, co2_buffer, sizeof(co2_buffer));
}

/**
  * @brief  阻塞式获取一帧 CO2 浓度（带超时保护 + 帧头校验 + 和校验）
  * @note   分步流程：
  *           ① 忙等中断收到完整一帧（co2_rx_len==6），带超时防止模块未接时死循环；
  *           ② 帧头校验：第 1 字节必须为 0x2C，否则说明帧错位/脏数据；
  *           ③ 和校验：前 5 字节累加低 8 位 == 第 6 字节，过滤传输误码；
  *           ④ 解析：浓度(ppm) = 缓冲[1]<<8 | 缓冲[2]（大端序）。
  *         ⚠ 风险1：忙等阻塞主循环，最长 timeout 毫秒（本工程传 1000）。
  *         ⚠ 风险2：等待条件是"收满 6 字节"。若模块发送出现字节间隙，IDLE 会提前
  *            以 <6 字节结束接收，本函数将等到下一帧拼满才返回（数据晚一拍）；
  *            若实测偶发异常，可把等待条件改为 co2_rx_len > 0 并加强帧头校验。
  * @param  co2_value  uint16_t*，出参：解析出的浓度值（ppm），仅返回 0 时有效
  * @param  timeout    uint32_t，等待完整帧的最长毫秒数
  * @return uint8_t   0:成功  1:超时未收到完整帧  2:帧头(模块地址)错误  3:校验和错误
  */
uint8_t CO2_get_data(uint16_t *co2_value, uint32_t timeout)
{
    uint32_t start_time = HAL_GetTick(); // 超时计时起点（HAL_GetTick 为毫秒时基）
    /* ① 等待中断侧收满一帧，带超时保护 */
    while (1)
    {
        if (co2_rx_len == CO2_RX_BUF_SIZE)
        {
            break;
        }
        if (HAL_GetTick() - start_time > timeout) // 无符号减法，毫秒时基回绕也安全
        {
            return 1;
        }
    }
    
    // for (uint8_t i = 0; i < co2_rx_len; i++)
    // {
    //  printf("%02x ", co2_buffer[i]);
    // }
    co2_rx_len = 0; // 重新接收

    // 1. 校验模块地址（第1字节为0x2C）
    if (co2_buffer[0] != 0x2C)
    {
        return 2;
    }

    // ② 和校验：前 5 字节累加和的低 8 位应等于第 6 字节
    uint8_t check_sum = 0;
    for (uint8_t i = 0; i < CO2_RX_BUF_SIZE - 1; i++)
    {
        check_sum += co2_buffer[i];
    }
    // printf("check_sum = %02x\n", check_sum);
    if (check_sum != co2_buffer[CO2_RX_BUF_SIZE - 1]) // 校验和不匹配 → 判定为误码帧
    {
        return 3;
    }

    // ③ 解析浓度：高字节在前（大端序），单位 ppm
    *co2_value = (co2_buffer[1] << 8) | co2_buffer[2];

    return 0;
}
