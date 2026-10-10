/* 老示例和初始化函数的链接占位：发送测试若意外调用这些 API 即失败。 */
#include "llcc68.h"
#include "llcc68_hal.h"
#include "adc.h"
#include "light.h"
#include "soil.h"
extern void Radio_Unexpected(const char *name);
ADC_HandleTypeDef hadc1;
volatile uint16_t adc_values[2];
llcc68_hal_status_t llcc68_hal_reset(const void *context)
{ (void)context; Radio_Unexpected("reset"); return LLCC68_HAL_STATUS_ERROR; }
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc,uint32_t *data,uint32_t count)
{ (void)adc; (void)data; (void)count; Radio_Unexpected("ADC"); return HAL_ERROR; }
uint16_t GetLux(void) { Radio_Unexpected("lux"); return 0U; }
uint8_t GetSoilHumidity(void) { Radio_Unexpected("soil"); return 0U; }

llcc68_status_t llcc68_clear_device_errors( const void* context )
{
    (void)context;
    Radio_Unexpected("llcc68_clear_device_errors"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_cal( const void* context, const llcc68_cal_mask_t param )
{
    (void)context; (void)param;
    Radio_Unexpected("llcc68_cal"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_cal_img_in_mhz( const void* context, const uint16_t freq1_in_mhz, const uint16_t freq2_in_mhz )
{
    (void)context; (void)freq1_in_mhz; (void)freq2_in_mhz;
    Radio_Unexpected("llcc68_cal_img_in_mhz"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_pa_cfg( const void* context, const llcc68_pa_cfg_params_t* params )
{
    (void)context; (void)params;
    Radio_Unexpected("llcc68_set_pa_cfg"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_ocp_value( const void* context, const uint8_t ocp_in_step_of_2_5_ma )
{
    (void)context; (void)ocp_in_step_of_2_5_ma;
    Radio_Unexpected("llcc68_set_ocp_value"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_reg_mode( const void* context, const llcc68_reg_mod_t mode )
{
    (void)context; (void)mode;
    Radio_Unexpected("llcc68_set_reg_mode"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_pkt_type( const void* context, const llcc68_pkt_type_t pkt_type )
{
    (void)context; (void)pkt_type;
    Radio_Unexpected("llcc68_set_pkt_type"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_rf_freq( const void* context, const uint32_t freq_in_hz )
{
    (void)context; (void)freq_in_hz;
    Radio_Unexpected("llcc68_set_rf_freq"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_lora_mod_params( const void* context, const llcc68_mod_params_lora_t* params )
{
    (void)context; (void)params;
    Radio_Unexpected("llcc68_set_lora_mod_params"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_tx_params( const void* context, const int8_t pwr_in_dbm,
                                      const llcc68_ramp_time_t ramp_time )
{
    (void)context; (void)pwr_in_dbm; (void)ramp_time;
    Radio_Unexpected("llcc68_set_tx_params"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_tx_modulation_workaround( const void* context, llcc68_pkt_type_t pkt_type, llcc68_lora_bw_t bw )
{
    (void)context; (void)pkt_type; (void)bw;
    Radio_Unexpected("llcc68_tx_modulation_workaround"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_cfg_tx_clamp( const void* context )
{
    (void)context;
    Radio_Unexpected("llcc68_cfg_tx_clamp"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_dio_irq_params( const void* context, const uint16_t irq_mask, const uint16_t dio1_mask,
                                           const uint16_t dio2_mask, const uint16_t dio3_mask )
{
    (void)context; (void)irq_mask; (void)dio1_mask; (void)dio2_mask; (void)dio3_mask;
    Radio_Unexpected("llcc68_set_dio_irq_params"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_buffer_base_address( const void* context, const uint8_t tx_base_address,
                                                const uint8_t rx_base_address )
{
    (void)context; (void)tx_base_address; (void)rx_base_address;
    Radio_Unexpected("llcc68_set_buffer_base_address"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_cfg_rx_boosted( const void* context, const bool state )
{
    (void)context; (void)state;
    Radio_Unexpected("llcc68_cfg_rx_boosted"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_fs( const void* context )
{
    (void)context;
    Radio_Unexpected("llcc68_set_fs"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_rx_with_timeout_in_rtc_step( const void* context, const uint32_t timeout_in_rtc_step )
{
    (void)context; (void)timeout_in_rtc_step;
    Radio_Unexpected("llcc68_set_rx_with_timeout_in_rtc_step"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_set_rx( const void* context, const uint32_t timeout_in_ms )
{
    (void)context; (void)timeout_in_ms;
    Radio_Unexpected("llcc68_set_rx"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_get_rx_buffer_status( const void* context, llcc68_rx_buffer_status_t* rx_buffer_status )
{
    (void)context; (void)rx_buffer_status;
    Radio_Unexpected("llcc68_get_rx_buffer_status"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_get_lora_pkt_status( const void* context, llcc68_pkt_status_lora_t* pkt_status )
{
    (void)context; (void)pkt_status;
    Radio_Unexpected("llcc68_get_lora_pkt_status"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_read_buffer( const void* context, const uint8_t offset, uint8_t* buffer, const uint8_t size )
{
    (void)context; (void)offset; (void)buffer; (void)size;
    Radio_Unexpected("llcc68_read_buffer"); return LLCC68_STATUS_ERROR;
}

llcc68_status_t llcc68_handle_rx_done( const void* context )
{
    (void)context;
    Radio_Unexpected("llcc68_handle_rx_done"); return LLCC68_STATUS_ERROR;
}
