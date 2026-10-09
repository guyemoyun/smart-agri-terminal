/**
 * @file llcc68_p2p.h
 * @author Jaychen (719095404@qq.com)
 * @brief lora点对点通信测试示例
 * @version 0.1
 * @date 2026-02-01
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#ifndef LLCC68_P2P_H
#define LLCC68_P2P_H

#include "llcc68.h"
#include "llcc68_hal.h"
#ifdef __cplusplus
extern "C" {
#endif

/************************ 硬件引脚定义 ************************/
#define LLCC68_SPI_HANDLE &hspi1
#define LLCC68_NSS_PORT GPIOA
#define LLCC68_NSS_PIN GPIO_PIN_4
#define LLCC68_RST_PORT GPIOB
#define LLCC68_RST_PIN GPIO_PIN_1
#define LLCC68_BUSY_PORT GPIOB
#define LLCC68_BUSY_PIN GPIO_PIN_0
#define LLCC68_DIO1_PORT GPIOB
#define LLCC68_DIO1_PIN GPIO_PIN_10

/************************ LoRa核心参数 ************************/
#define LORA_FREQ 470500000UL	   // 470.5MHz(中国频段范围：470.0MHz~510.0MHz)
#define LORA_SF LLCC68_LORA_SF9	   // 扩频因子
#define LORA_BW LLCC68_LORA_BW_125 // 带宽
#define LORA_CR LLCC68_LORA_CR_4_5 // 编码率
#define LORA_PREAMBLE_LEN 8		   // 前导码长度
#define LORA_PAYLOAD_LEN 255	   // 数据包长度, llcc68最多支持255字节
#define LORA_TX_POWER_DBM 22	   // 发射功率

extern llcc68_hal_context_t llcc68_ctx;
extern volatile uint8_t rx_data[LORA_PAYLOAD_LEN];

void DIO1_EXTI_Callback(void);
llcc68_status_t llcc68_init(const void *context);
llcc68_status_t llcc68_lora_send(const void *context, const uint8_t *data, uint8_t len, uint32_t timeout_in_ms);
llcc68_status_t llcc68_lora_receive_mode(const void *context, uint32_t timeout_in_ms);
llcc68_status_t llcc68_lora_receive_data(const void *context, uint8_t *data, uint16_t *len,
									llcc68_pkt_status_lora_t *pkt_status, uint32_t timeout_in_ms);					
void llcc68_p2p_demo(void);

#ifdef __cplusplus
}
#endif
#endif /* LLCC68_DEMO_H */
/************************ (C) COPYRIGHT Jaychen ********END OF FILE********/
