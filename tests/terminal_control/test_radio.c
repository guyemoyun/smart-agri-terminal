#include "llcc68_p2p.h"
#include "spi.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

GPIO_TypeDef mock_gpioa,mock_gpiob,mock_gpioc;
SPI_HandleTypeDef hspi1;
static uint32_t tick;
static unsigned int failures,delays,starts,clears,writes;
static uint8_t dio_high,irq_failure,tx_failure,buffer_failure;
static uint8_t radio_bytes[255],radio_length;
static unsigned int read_calls,rx_starts;
static llcc68_irq_mask_t irq_value;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
void Radio_Unexpected(const char *name){++failures;printf("Unexpected API: %s\n",name);}
uint32_t HAL_GetTick(void){return tick;}
void HAL_Delay(uint32_t delay){tick+=delay;++delays;}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin)
{CHECK(port==GPIOB && pin==GPIO_PIN_10);return dio_high ? GPIO_PIN_SET : GPIO_PIN_RESET;}
llcc68_status_t llcc68_set_standby(const void *context,llcc68_standby_cfg_t mode)
{CHECK(context==&llcc68_ctx && mode==LLCC68_STANDBY_CFG_RC);return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_clear_irq_status(const void *context,llcc68_irq_mask_t irq)
{CHECK(context==&llcc68_ctx && irq==LLCC68_IRQ_ALL);++clears;irq_value=0;dio_high=0;return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_set_lora_pkt_params(const void *context,const llcc68_pkt_params_lora_t *packet)
{CHECK(context==&llcc68_ctx && (packet->pld_len_in_bytes==34 || packet->pld_len_in_bytes==255) && packet->crc_is_on);return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_write_buffer(const void *context,uint8_t offset,const uint8_t *data,uint8_t length)
{CHECK(context==&llcc68_ctx && offset==0 && data!=0 && length==34);++writes;return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_set_tx(const void *context,uint32_t timeout)
{CHECK(context==&llcc68_ctx && timeout==1500);++starts;return tx_failure ? LLCC68_STATUS_ERROR : LLCC68_STATUS_OK;}
llcc68_status_t llcc68_get_and_clear_irq_status(const void *context,llcc68_irq_mask_t *irq)
{
    CHECK(context==&llcc68_ctx);
    if(irq_failure)return LLCC68_STATUS_ERROR;
    *irq=irq_value;irq_value=0;dio_high=0;return LLCC68_STATUS_OK;
}

llcc68_status_t llcc68_cfg_rx_boosted(const void *context,bool enable)
{CHECK(context==&llcc68_ctx && enable);return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_set_rx_with_timeout_in_rtc_step(const void *context,uint32_t timeout)
{CHECK(context==&llcc68_ctx && timeout==LLCC68_RX_CONTINUOUS);++rx_starts;return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_get_rx_buffer_status(const void *context,llcc68_rx_buffer_status_t *buffer)
{CHECK(context==&llcc68_ctx);buffer->pld_len_in_bytes=radio_length;buffer->buffer_start_pointer=10;return buffer_failure ? LLCC68_STATUS_ERROR : LLCC68_STATUS_OK;}
llcc68_status_t llcc68_get_lora_pkt_status(const void *context,llcc68_pkt_status_lora_t *packet)
{CHECK(context==&llcc68_ctx);*packet=(llcc68_pkt_status_lora_t){.rssi_pkt_in_dbm=-80,.snr_pkt_in_db=9};return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_read_buffer(const void *context,uint8_t offset,uint8_t *out,uint8_t size)
{CHECK(context==&llcc68_ctx && offset==10 && size==radio_length);++read_calls;memcpy(out,radio_bytes,size);return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_handle_rx_done(const void *context)
{CHECK(context==&llcc68_ctx);return LLCC68_STATUS_OK;}

int main(void)
{
    uint8_t frame[34]={0};
    CHECK(llcc68_lora_send_start(&llcc68_ctx,0,34,1500)!=LLCC68_STATUS_OK);
    CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,0,1500)!=LLCC68_STATUS_OK);
    CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,0)!=LLCC68_STATUS_OK);
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,0)==LLCC68_TX_FAILED);
    CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    CHECK(!delays && starts==1 && writes==1);
    CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)!=LLCC68_STATUS_OK && starts==1);
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,100)==LLCC68_TX_PENDING);
    irq_value=LLCC68_IRQ_TX_DONE;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,101)==LLCC68_TX_DONE && !delays);
    tick=200;CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    irq_value=LLCC68_IRQ_TX_DONE;dio_high=1; /* 未收到边沿，电平兜底仍能消费。 */
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,201)==LLCC68_TX_DONE);
    tick=300;CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,1799)==LLCC68_TX_PENDING);
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,1800)==LLCC68_TX_FAILED);
    tick=2000;CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    irq_value=LLCC68_IRQ_TIMEOUT;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,2001)==LLCC68_TX_FAILED);
    tick=3000;CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    irq_failure=1;DIO1_EXTI_Callback();CHECK(llcc68_lora_tx_poll(&llcc68_ctx,3001)==LLCC68_TX_FAILED);
    irq_failure=0;tick=UINT32_MAX-10U;
    CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,1488)==LLCC68_TX_PENDING);
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,1489)==LLCC68_TX_FAILED);
    tx_failure=1;CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_ERROR);
    tx_failure=0;CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    irq_value=LLCC68_IRQ_TX_DONE;DIO1_EXTI_Callback();CHECK(llcc68_lora_tx_poll(&llcc68_ctx,tick)==LLCC68_TX_DONE);
    uint8_t received[97];memset(received,0xEE,sizeof(received));
    uint8_t length=0;
    llcc68_pkt_status_lora_t packet;
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_FAILED);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_PENDING);
    memset(radio_bytes,0xA5,96);radio_length=96;
    irq_value=LLCC68_IRQ_RX_DONE;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_PACKET);
    CHECK(length==96 && received[96]==0xEE && packet.rssi_pkt_in_dbm==-80 && read_calls==1);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    radio_length=97;irq_value=LLCC68_IRQ_RX_DONE;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_DROPPED && length==0 && read_calls==1);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    radio_length=34;irq_value=LLCC68_IRQ_RX_DONE|LLCC68_IRQ_CRC_ERROR;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_DROPPED && read_calls==1);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    irq_value=LLCC68_IRQ_HEADER_ERROR;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_DROPPED);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    buffer_failure=1;irq_value=LLCC68_IRQ_RX_DONE;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_FAILED && read_calls==1);
    buffer_failure=0;CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    irq_failure=1;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_rx_poll(&llcc68_ctx,received,96,&length,&packet)==LLCC68_RX_FAILED);
    irq_failure=0;
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    CHECK(llcc68_lora_send_start(&llcc68_ctx,frame,34,1500)==LLCC68_STATUS_OK);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_ERROR);
    irq_value=LLCC68_IRQ_TX_DONE;DIO1_EXTI_Callback();
    CHECK(llcc68_lora_tx_poll(&llcc68_ctx,tick)==LLCC68_TX_DONE);
    CHECK(llcc68_lora_rx_start(&llcc68_ctx)==LLCC68_STATUS_OK);
    CHECK(rx_starts==8 && delays==0);
    printf("radio start/poll tests: %u assertions failed, %u starts, %u irq clears\n",failures,starts,clears);
    return failures ? 1:0;
}
