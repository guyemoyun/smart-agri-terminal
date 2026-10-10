/**
 * @file llcc68_p2p.c 
 * @author Jaychen (719095404@qq.com)
 * @brief lora点对点通信测试示例
 * @version 0.1
 * @date 2026-02-01
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include "llcc68_p2p.h"
#include "llcc68.h"
#include "llcc68_regs.h"
#include "stm32f1xx_hal.h"
#include "main.h"
#include "spi.h"
#include <stdio.h>
#include <string.h>

#include "adc.h"

#include "dht22.h"
#include "light.h"    // GetLux 声明（原代码误写为 Getlux，函数名区分大小写导致 L6218E）
#include "soil.h"     // GetSoilHumidity 声明
#include "app_main.h" // adc_values（ADC+DMA 共享缓冲）extern 声明

llcc68_hal_context_t llcc68_ctx = {
	.hspi = LLCC68_SPI_HANDLE,
	.nss_port = LLCC68_NSS_PORT,
	.nss_pin = LLCC68_NSS_PIN,
	.rst_port = LLCC68_RST_PORT,
	.rst_pin = LLCC68_RST_PIN,
	.busy_port = LLCC68_BUSY_PORT,
	.busy_pin = LLCC68_BUSY_PIN,
	.dio1_port = LLCC68_DIO1_PORT,
	.dio1_pin = LLCC68_DIO1_PIN};

static volatile bool tx_done = false;
static volatile bool rx_done = false;
static volatile bool rx_timeout = false;
static volatile bool irq_pending = false;
volatile uint8_t rx_data[LORA_PAYLOAD_LEN] = {0};
static bool async_tx_busy;
static bool irq_read_failed;
static uint32_t async_tx_tick, async_tx_timeout;

/**
 * @brief DIO1中断回调函数,需要在外部中断中被调用
 *
 */
void DIO1_EXTI_Callback(void)
{
    /* DIO1 ISR 只记录事件，SPI 状态读取统一放到主循环。 */
    irq_pending = true;
}

void llcc68_process_irq(void)
{
    llcc68_irq_mask_t irq_status = 0;

    /* 同时检查 DIO1 电平，弥补初始化或短临界区期间遗漏的边沿。 */
    if (!irq_pending && HAL_GPIO_ReadPin(llcc68_ctx.dio1_port, llcc68_ctx.dio1_pin) != GPIO_PIN_SET)
    {
        return;
    }

    irq_pending = false;
    if (llcc68_get_and_clear_irq_status(&llcc68_ctx, &irq_status) != LLCC68_STATUS_OK)
    {
        irq_read_failed = true;
        irq_pending = true;
        return;
    }

    if ((irq_status & LLCC68_IRQ_TX_DONE) != 0U)
    {
        tx_done = true;
    }
    if ((irq_status & LLCC68_IRQ_RX_DONE) != 0U)
    {
        rx_done = true;
    }
    if ((irq_status & LLCC68_IRQ_TIMEOUT) != 0U)
    {
        rx_timeout = true;
    }
    if ((irq_status & LLCC68_IRQ_CRC_ERROR) != 0U)
    {
        printf("LLCC68: RX CRC error\r\n");
    }
}

/**
 * @brief 初始化llcc68
 *
 * @param context 上下文
 * @return llcc68_status_t
 */
llcc68_status_t llcc68_init(const void *context)
{
	llcc68_status_t status;
    async_tx_busy = false;
    irq_read_failed = false;
    irq_pending = false;
    tx_done = rx_done = rx_timeout = false;

	// 硬件复位（llcc68_hal_reset 返回 llcc68_hal_status_t，显式转换消除 -Wenum-conversion 警告）
	status = (llcc68_status_t)llcc68_hal_reset(context);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 reset failed!\r\n");
		return status;
	}
	status = llcc68_set_standby(context, LLCC68_STANDBY_CFG_RC);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 set STDBY_RC failed!\r\n");
		return status;
	}
	HAL_Delay(50);

	// 进入待机模式（XOSC）
	status = llcc68_set_standby(context, LLCC68_STANDBY_CFG_XOSC);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 set standby failed!\r\n");
		return status;
	}
	HAL_Delay(1000); // XOSC启动需等待稳定

	// 清除复位后残留错误（避免影响后续校准）
	status = llcc68_clear_device_errors(context);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 clear errors failed!\r\n");
		return status;
	}

	llcc68_cal_mask_t cal_mask = LLCC68_CAL_PLL | LLCC68_CAL_ADC_PULSE | LLCC68_CAL_ADC_BULK_N | LLCC68_CAL_ADC_BULK_P | LLCC68_CAL_IMAGE;
	status = llcc68_cal(context, cal_mask);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 cal failed!\r\n");
		return status;
	}

	// 中国频段（470-510MHz）Image校准（手册9.2.1）
	status = llcc68_cal_img_in_mhz(context, 470, 510);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 image cal failed!\r\n");
		return status;
	}

	// 配置PA参数（优化功率与可靠性）
	llcc68_pa_cfg_params_t pa_cfg = {
		.pa_duty_cycle = 0x03, // 手册13.1.14.1：+20dBm最优占空比
		.hp_max = 0x05,		   // 手册4.4.1：+20dBm对应最大增益等级
		.device_sel = 0x00,	   // 内置PA（外部PA需设为0x01）
		.pa_lut = 0x01		   // 启用PA校准表
	};
	status = llcc68_set_pa_cfg(context, &pa_cfg);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 set PA cfg failed!\r\n");
		return status;
	}

	// 配置OCP过流保护（手册5.1，避免PA损坏）
	uint8_t ocp_value = LLCC68_OCP_PARAM_VALUE_140_MA; // 内置PA默认140mA
	status = llcc68_set_ocp_value(context, ocp_value);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 set OCP failed!\r\n");
		return status;
	}

	// 设置工作模式为DCDC
	status = llcc68_set_reg_mode(context, LLCC68_REG_MODE_LDO);
	if (status != LLCC68_STATUS_OK)
		return status;

	// 设置包类型为LoRa,核心射频参数配置
	status = llcc68_set_pkt_type(context, LLCC68_PKT_TYPE_LORA);
	if (status != LLCC68_STATUS_OK)
		return status;

	// 设置射频频率, 只有选择包类型后，才能设置
	status = llcc68_set_rf_freq(context, LORA_FREQ);
	if (status != LLCC68_STATUS_OK)
		return status;

	llcc68_mod_params_lora_t lora_mod_params = {
		.sf = LORA_SF,
		.bw = LORA_BW,
		.cr = LORA_CR,
		.ldro = 0 // 关闭低数据率优化
	};
	// 设置LoRa调制参数,只有选择包类型后，才能设置
	status = llcc68_set_lora_mod_params(context, &lora_mod_params);
	if (status != LLCC68_STATUS_OK)
		return status;

	// 设置发射功率
	status = llcc68_set_tx_params(context, LORA_TX_POWER_DBM, LLCC68_RAMP_200_US);
	if (status != LLCC68_STATUS_OK)
		return status;

	// 优化500kHz带宽调制质量（即使当前用125kHz，提前兼容）
	status = llcc68_tx_modulation_workaround(context, LLCC68_PKT_TYPE_LORA, LORA_BW);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 tx modulation workaround failed!\r\n");
		return status;
	}

	// 增强PA抗天线失配能力
	status = llcc68_cfg_tx_clamp(context);
	if (status != LLCC68_STATUS_OK)
	{
		printf("LLCC68 cfg tx clamp failed!\r\n");
		return status;
	}

	// 配置DIO中断映射：DIO1映射TX_DONE和RX_DONE
	status = llcc68_set_dio_irq_params(context,
									   LLCC68_IRQ_TX_DONE | LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT, // 使能系统中断
									   LLCC68_IRQ_TX_DONE | LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT, // DIO1映射TX/RX完成
									   LLCC68_IRQ_NONE,												 // DIO2无映射
									   LLCC68_IRQ_NONE);											 // DIO3无映射
	if (status != LLCC68_STATUS_OK)
		return status;

	// 配置缓冲区基地址（TX:0, RX:0）
	status = llcc68_set_buffer_base_address(context, 0, 0);
	if (status != LLCC68_STATUS_OK)
		return status;

	// 清除所有中断标志
	llcc68_clear_irq_status(context, LLCC68_IRQ_ALL);
	printf("LLCC68 init success! Chip mode: STDBY_XOSC, RF freq: %lu Hz\r\n", LORA_FREQ);
	return LLCC68_STATUS_OK;
}

/**
 * @brief LoRa发送函数
 *
 * @param context 上下文
 * @param data 数据
 * @param len 数据长度
 * @param timeout_in_ms 超时时间,单位ms
 * @return llcc68_status_t
 */
llcc68_status_t llcc68_lora_send_start(const void *context, const uint8_t *data,
                                     uint8_t length, uint32_t timeout_ms)
{
    llcc68_status_t status;
    if (context != &llcc68_ctx || data == NULL || length == 0U ||
        timeout_ms == 0U || timeout_ms > 60000U || async_tx_busy)
        return LLCC68_STATUS_ERROR;
    status = llcc68_set_standby(context, LLCC68_STANDBY_CFG_RC);
    if (status != LLCC68_STATUS_OK) return status;
    status = llcc68_clear_irq_status(context, LLCC68_IRQ_ALL);
    if (status != LLCC68_STATUS_OK) return status;
    tx_done = false;
    rx_timeout = false;
    irq_pending = false;
    irq_read_failed = false;
    const llcc68_pkt_params_lora_t packet = {
        .preamble_len_in_symb = LORA_PREAMBLE_LEN, .header_type = LLCC68_LORA_PKT_EXPLICIT,
        .pld_len_in_bytes = length, .crc_is_on = true, .invert_iq_is_on = false
    };
    status = llcc68_set_lora_pkt_params(context, &packet);
    if (status != LLCC68_STATUS_OK) return status;
    status = llcc68_write_buffer(context, 0U, data, length);
    if (status != LLCC68_STATUS_OK) return status;
    status = llcc68_set_tx(context, timeout_ms);
    if (status != LLCC68_STATUS_OK) return status;
    async_tx_tick = HAL_GetTick();
    async_tx_timeout = timeout_ms;
    async_tx_busy = true;
    return LLCC68_STATUS_OK;
}

llcc68_tx_result_t llcc68_lora_tx_poll(const void *context, uint32_t now)
{
    if (context != &llcc68_ctx || !async_tx_busy) return LLCC68_TX_FAILED;
    llcc68_process_irq();
    if (irq_read_failed || rx_timeout || (!tx_done && (now - async_tx_tick) >= async_tx_timeout))
    {
        async_tx_busy = false;
        (void)llcc68_set_standby(context, LLCC68_STANDBY_CFG_RC);
        (void)llcc68_clear_irq_status(context, LLCC68_IRQ_ALL);
        return LLCC68_TX_FAILED;
    }
    if (!tx_done) return LLCC68_TX_PENDING;
    async_tx_busy = false;
    return LLCC68_TX_DONE;
}

/* 兼容旧课堂示例；主业务使用 start/poll，不调用这个等待接口。 */
llcc68_status_t llcc68_lora_send(const void *context, const uint8_t *data, uint8_t length, uint32_t timeout_ms)
{
    llcc68_status_t status = llcc68_lora_send_start(context, data, length, timeout_ms);
    if (status != LLCC68_STATUS_OK) return status;
    for (;;)
    {
        const llcc68_tx_result_t result = llcc68_lora_tx_poll(context, HAL_GetTick());
        if (result == LLCC68_TX_DONE) return LLCC68_STATUS_OK;
        if (result == LLCC68_TX_FAILED) return LLCC68_STATUS_ERROR;
        HAL_Delay(1U);
    }
}

llcc68_status_t llcc68_lora_receive_mode(const void *context, uint32_t timeout_in_ms)
{
	llcc68_status_t status;
	llcc68_pkt_params_lora_t lora_pkt_params = {
		.preamble_len_in_symb = LORA_PREAMBLE_LEN, // 与发送端一致
		.header_type = LLCC68_LORA_PKT_EXPLICIT,   // 与发送端一致
		.pld_len_in_bytes = LORA_PAYLOAD_LEN,	   // 最大接收长度
		.crc_is_on = true,						   // 与发送端一致
		.invert_iq_is_on = false				   // 与发送端一致
	};

	// 设置接收模式为Boosted
	status = llcc68_cfg_rx_boosted(context, true);
	if (status != LLCC68_STATUS_OK)
	{
		printf("Failed to set RX boosted mode.\r\n");
		// 可以选择返回错误或继续
	}
	// 先设包类型和参数，再进入 FS 模式
	status = llcc68_set_pkt_type(context, LLCC68_PKT_TYPE_LORA);
	if (status != LLCC68_STATUS_OK)
		return status;
	status = llcc68_set_lora_pkt_params(context, &lora_pkt_params);
	if (status != LLCC68_STATUS_OK)
		return status;
	// 最后进入FS模式
	status = llcc68_set_fs(context);
	if (status != LLCC68_STATUS_OK)
		return status;

	// 设置为接收模式
	if(timeout_in_ms==0){
		status = llcc68_set_rx_with_timeout_in_rtc_step(context,LLCC68_RX_CONTINUOUS);
	}
	else{
		status = llcc68_set_rx(context, timeout_in_ms);
	}
	if (status != LLCC68_STATUS_OK)
	{
		printf("Set RX mode failed!\r\n");
	}

	return status;
}

llcc68_status_t llcc68_lora_receive_data(const void *context, uint8_t *data, uint16_t *len,
										 llcc68_pkt_status_lora_t *pkt_status, uint32_t timeout_in_ms)
{
	llcc68_status_t status = LLCC68_STATUS_ERROR;
    if (context == NULL || data == NULL || len == NULL || pkt_status == NULL)
        return LLCC68_STATUS_ERROR;
    *len = 0U;
	printf("Waiting for packet...\r\n");
	// 等待接收完成或超时
	uint32_t start = HAL_GetTick();
	while (!rx_done && !rx_timeout)
	{
        llcc68_process_irq();
		if (HAL_GetTick() - start > timeout_in_ms + 10U)
		{
			rx_timeout = false;
			printf("Receive timeout...!\r\n");
			return status;
		}
		HAL_Delay(1);
	}

	if (rx_done)
	{
		// 获取接收状态
		llcc68_rx_buffer_status_t rx_buffer_status;
		status = llcc68_get_rx_buffer_status(context, &rx_buffer_status);
		if (status != LLCC68_STATUS_OK)
		{
			printf("Get RX buffer status failed!\r\n");
			goto rx;
		}
		// 获取包状态
		status = llcc68_get_lora_pkt_status(context, pkt_status);
		if (status != LLCC68_STATUS_OK)
		{
			printf("Get packet status failed!\r\n");
			goto rx;
		}

		// 从FIFO读取数据
		*len = rx_buffer_status.pld_len_in_bytes;
		status = llcc68_read_buffer(context, rx_buffer_status.buffer_start_pointer, data, rx_buffer_status.pld_len_in_bytes);
		if (status != LLCC68_STATUS_OK)
		{
			printf("Read buffer failed!\r\n");
			goto rx;
		}
		printf("Reception completed\r\n");
	}
	else
	{
		printf("Receive timeout...\r\n");
		goto rx;
	}
	// 处理接收完成后的清理工作（重要！）
	llcc68_handle_rx_done(context);
rx:
	//  清理接收完成后的标志位
	rx_done = false;
	rx_timeout = false;
	return status;
}

// LoRa测试函数
void llcc68_p2p_demo(void)
{
	uint32_t start = HAL_GetTick();
	llcc68_status_t status;

	// 初始化LLCC68
	while (1)
	{
		status = llcc68_init(&llcc68_ctx);

		if (status != LLCC68_STATUS_OK)
		{
			printf("LLCC68初始化失败！\r\n");
		}
		else
		{
			printf("LLCC68初始化成功！\r\n");
			break;
		}
		HAL_Delay(1000);
	}
	// 测试数据
	uint16_t tx_data[3] = {0};
	uint16_t tx_cnt = 0;              // 发送计数：接收端看到 1,2,3... 递增即链路正常

	/* 启动 ADC1 + DMA。
	 * ⚠ 必须用全局 adc_values：GetLux()/GetSoilHumidity() 读的就是这个缓冲。
	 *   原代码用局部 adc1_vol，DMA 搬到局部数组里，传感器函数读的却是
	 *   另一个数组 → 永远读到 0/旧值。 */
	HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_values, 2);

	while (1)
	{
		if (HAL_GetTick() - start > 3000)
		{
				tx_data[0] = GetLux();            // 光照 lux
				tx_data[1] = GetSoilHumidity();   // 土壤等级（探头未装则恒为1）
				tx_data[2] = tx_cnt++;            // 递增计数，用于验证链路连通性

				start = HAL_GetTick();
				printf("LoRa TX: lux=%d soil=%d cnt=%d\r\n", tx_data[0], tx_data[1], tx_data[2]);
				// 发送测试（长度 4→6：含新增的计数字段）
				llcc68_lora_send(&llcc68_ctx, (uint8_t *)tx_data, 6, 1000);
		}
	}
}
