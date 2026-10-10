#include "lora_service.h"
#include "llcc68_p2p.h"
#include "terminal_telemetry.h"
#include <stdio.h>
#include <string.h>

llcc68_hal_context_t llcc68_ctx;
static uint32_t tick;
static unsigned int failures,init_calls,tx_starts;
static uint8_t held,init_failure,chip_missing;
static ControlSnapshot_t control;
static llcc68_tx_result_t tx_result=LLCC68_TX_PENDING;
#define CHECK(x) do { if(!(x)){++failures;printf("FAIL %d: %s\n",__LINE__,#x);} }while(0)
uint32_t HAL_GetTick(void){return tick;}
uint8_t Button_IsHeld(void){return held;}
void Control_GetSnapshot(ControlSnapshot_t *snapshot){*snapshot=control;}
void Sensor_GetSnapshot(SensorData_t *sensor)
{*sensor=(SensorData_t){.temperature=25,.humidity=50,.soil_level=2,.co2_ppm=800,.light_lux=100,
 .light_valid=1,.soil_valid=1,.dht22_valid=1,.co2_valid=1,.adc_update_tick=tick,.dht22_update_tick=tick,.co2_update_tick=tick};}
llcc68_status_t llcc68_init(const void *context)
{CHECK(context==&llcc68_ctx);++init_calls;return init_failure ? LLCC68_STATUS_ERROR : LLCC68_STATUS_OK;}
llcc68_status_t llcc68_get_status(const void *context,llcc68_chip_status_t *chip)
{CHECK(context==&llcc68_ctx);memset(chip,0,sizeof(*chip));if(!chip_missing)chip->chip_mode=LLCC68_CHIP_MODE_STBY_XOSC;return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_lora_send_start(const void *context,const uint8_t *frame,uint8_t length,uint32_t timeout)
{
 CHECK(context==&llcc68_ctx && length==34 && timeout==1500);
 CHECK(frame[1]==2 && frame[7]==24 && Terminal_Crc16(frame,34)==0);
 CHECK(frame[17]==15 && frame[26]==7);++tx_starts;return LLCC68_STATUS_OK;
}
llcc68_tx_result_t llcc68_lora_tx_poll(const void *context,uint32_t now)
{CHECK(context==&llcc68_ctx && now==tick);return tx_result;}
static void Step(uint32_t now){tick=now;Lora_ServiceTask(now);}
int main(void)
{
 LoraStatus_t link;
 control.actuator.ready_mask=7;
 Lora_ServiceInit();
 control.actuator.relay_on=1;Step(0);CHECK(init_calls==0);
 control.actuator.relay_on=0;control.actuator.fan_duty_permille=1000;Step(1);CHECK(init_calls==0);
 control.actuator.fan_duty_permille=0;control.actuator.buzzer_on=1;Step(2);CHECK(init_calls==0);
 control.actuator.buzzer_on=0;held=1;Step(3);CHECK(init_calls==0);
 held=0;init_failure=1;Step(4);CHECK(init_calls==1);
 Step(5003);CHECK(init_calls==1);
 init_failure=0;chip_missing=1;Step(5004);Lora_GetStatus(&link);
 CHECK(init_calls==2 && !link.ready && link.init_error_count==2);
 chip_missing=0;Step(10004);Lora_GetStatus(&link);CHECK(link.ready);
 Step(10005);Lora_GetStatus(&link);CHECK(link.tx_busy && tx_starts==1 && !link.tx_done_count);
 Step(10006);Lora_GetStatus(&link);CHECK(link.tx_busy && tx_starts==1 && !link.tx_done_count);
 tx_result=LLCC68_TX_DONE;Step(10007);Lora_GetStatus(&link);CHECK(!link.tx_busy && link.tx_done_count==1);
 Step(15006);CHECK(tx_starts==1);
 Step(15007);CHECK(tx_starts==2);
 tx_result=LLCC68_TX_FAILED;Step(15008);Lora_GetStatus(&link);
 CHECK(!link.ready && !link.tx_busy && link.tx_error_count==1);
 control.actuator.relay_on=1;Step(25008);CHECK(init_calls==3);
 control.actuator.relay_on=0;Step(25009);CHECK(init_calls==4);
 printf("radio service gating/statistics: %u assertions failed\n",failures);
 return failures ? 1:0;
}
