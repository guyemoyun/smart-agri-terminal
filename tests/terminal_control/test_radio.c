#include "llcc68_p2p.h"
#include "spi.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

GPIO_TypeDef mock_gpioa,mock_gpiob,mock_gpioc;
SPI_HandleTypeDef hspi1;
static uint32_t tick;
static unsigned int failures,delays,starts,clears,writes;
static uint8_t dio_high,irq_failure,tx_failure;
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
{CHECK(context==&llcc68_ctx && packet->pld_len_in_bytes==34 && packet->crc_is_on);return LLCC68_STATUS_OK;}
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
    CHECK(delays==0);
    printf("radio start/poll tests: %u assertions failed, %u starts, %u irq clears\n",failures,starts,clears);
    return failures ? 1:0;
}
