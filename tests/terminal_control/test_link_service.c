#include "lora_service.h"
#include "llcc68_p2p.h"
#include "terminal_telemetry.h"
#include "terminal_commands.h"
#include "lora_protocol.h"
#include <stdio.h>
#include <string.h>

llcc68_hal_context_t llcc68_ctx;
static uint32_t tick;
static unsigned int failures,init_calls,tx_starts,random_calls,execute_calls,pump_starts;
static uint8_t held,init_failure,chip_missing,rx_active,rx_pending,rx_failed,random_failed;
static uint8_t inbox[LORA_MAX_FRAME],inbox_length;
static ControlSnapshot_t control;
static LoraFrame_t last_sent;
static uint8_t last_wire[LORA_MAX_FRAME],last_length;
static llcc68_tx_result_t tx_result=LLCC68_TX_PENDING;
#define SESSION 0x12345678U
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
llcc68_status_t llcc68_get_random_numbers(const void *context,uint32_t *numbers,unsigned int count)
{CHECK(context==&llcc68_ctx && count==1);++random_calls;*numbers=random_failed?0U:SESSION;return LLCC68_STATUS_OK;}
llcc68_status_t llcc68_lora_send_start(const void *context,const uint8_t *frame,uint8_t length,uint32_t timeout)
{
 CHECK(context==&llcc68_ctx && length<=96 && timeout==1500);
 CHECK(Lora_Decode(frame,length,&last_sent)==LORA_DECODE_OK);
 memcpy(last_wire,frame,length);last_length=length;rx_active=0;++tx_starts;
 if(last_sent.kind==LORA_KIND_TELEMETRY)
 {CHECK(last_sent.payload[1]==2 && last_sent.payload[7]==24 && Terminal_Crc16(last_sent.payload,34)==0);}
 return LLCC68_STATUS_OK;
}
llcc68_tx_result_t llcc68_lora_tx_poll(const void *context,uint32_t now)
{CHECK(context==&llcc68_ctx && now==tick);return tx_result;}
llcc68_status_t llcc68_lora_rx_start(const void *context)
{CHECK(context==&llcc68_ctx);rx_active=1;return LLCC68_STATUS_OK;}
llcc68_rx_result_t llcc68_lora_rx_poll(const void *context,uint8_t *data,uint16_t capacity,uint8_t *length,llcc68_pkt_status_lora_t *status)
{
 CHECK(context==&llcc68_ctx && rx_active && capacity==96);
 if(rx_failed){rx_active=0;rx_failed=0;return LLCC68_RX_FAILED;}
 if(!rx_pending)return LLCC68_RX_PENDING;
 memcpy(data,inbox,inbox_length);*length=inbox_length;rx_pending=0;rx_active=0;
 *status=(llcc68_pkt_status_lora_t){.rssi_pkt_in_dbm=-75,.snr_pkt_in_db=8};
 return LLCC68_RX_PACKET;
}
static uint8_t Execute(void *context,uint8_t opcode,uint8_t argument,uint32_t now)
{
 (void)context;(void)now;++execute_calls;
 if(opcode==LORA_COMMAND_PUMP)
 {if(argument && !control.actuator.relay_on)++pump_starts;control.actuator.relay_on=argument;}
 if(opcode==LORA_COMMAND_FAN){control.actuator.fan_level=argument;control.actuator.fan_duty_permille=argument?1000U:0U;}
 return CONTROL_OK;
}
static void Snapshot(void *context,ControlSnapshot_t *state){(void)context;*state=control;}
const TerminalExecutor_t *TerminalCommands_ControlExecutor(void)
{static const TerminalExecutor_t executor={.execute=Execute,.snapshot=Snapshot};return &executor;}
static void Step(uint32_t now){tick=now;Lora_ServiceTask(now);}
static void Feed(const LoraFrame_t *frame)
{const size_t length=Lora_Encode(frame,inbox,sizeof(inbox));CHECK(length>0);inbox_length=(uint8_t)length;rx_pending=1;}
static void Ack(void)
{LoraFrame_t ack;CHECK(Lora_BuildAck(&last_sent,&ack));Feed(&ack);}
static LoraFrame_t Command(uint32_t id,uint8_t opcode,uint8_t argument,uint32_t expires)
{
 LoraFrame_t frame={.kind=LORA_KIND_COMMAND,.flags=LORA_FLAG_RESULT,.source=0,.target=1,
 .sequence=(uint16_t)id,.session=SESSION,.transaction=id,.payload_length=6};
 frame.payload[0]=opcode;frame.payload[1]=argument;Lora_Write32(&frame.payload[2],expires);return frame;
}
int main(void)
{
 LoraStatus_t link;control.actuator.ready_mask=7;
 Lora_ServiceInit();
 control.actuator.relay_on=1;Step(0);CHECK(init_calls==0);
 control.actuator.relay_on=0;control.actuator.fan_duty_permille=1000;Step(1);CHECK(init_calls==0);
 control.actuator.fan_duty_permille=0;control.actuator.buzzer_on=1;Step(2);CHECK(init_calls==0);
 control.actuator.buzzer_on=0;held=1;Step(3);CHECK(init_calls==0);
 held=0;init_failure=1;Step(4);CHECK(init_calls==1);
 Step(5003);CHECK(init_calls==1);
 init_failure=0;chip_missing=1;Step(5004);Lora_GetStatus(&link);
 CHECK(init_calls==2 && !link.ready && link.init_error_count==2);
 chip_missing=0;random_failed=1;Step(10004);Lora_GetStatus(&link);
 CHECK(!link.ready && !link.session && link.init_error_count==3);
 random_failed=0;Step(15004);Lora_GetStatus(&link);CHECK(link.ready && link.rx_active && link.session==SESSION);
 Step(15005);CHECK(last_sent.kind==LORA_KIND_HELLO && tx_starts==1);
 tx_result=LLCC68_TX_DONE;Step(15006);Lora_GetStatus(&link);
 CHECK(link.waiting_ack && link.tx_done_count==1 && link.ack_count==0 && !link.gateway_online);
 LoraFrame_t wrong;CHECK(Lora_BuildAck(&last_sent,&wrong));++wrong.sequence;Feed(&wrong);Step(15007);
 Lora_GetStatus(&link);CHECK(!link.gateway_online && link.ack_count==0);
 Ack();Step(15008);CHECK(last_sent.kind==LORA_KIND_TELEMETRY && tx_starts==2);
 Step(15009);Ack();Step(15010);Lora_GetStatus(&link);
 CHECK(link.gateway_online && link.ack_count==2 && !link.tx_busy && !link.waiting_ack);
 Step(20009);CHECK(tx_starts==2);
 Step(20010);CHECK(last_sent.kind==LORA_KIND_TELEMETRY && tx_starts==3);
 uint8_t first[96];const uint8_t first_length=last_length;memcpy(first,last_wire,first_length);
 Step(20011);Step(22011);CHECK(tx_starts==3);
 Step(22111);CHECK(tx_starts==4 && memcmp(first,last_wire,first_length)==0);
 Step(22112);Lora_GetStatus(&link);CHECK(link.retry_count==1);
 Ack();Step(22113);Lora_GetStatus(&link);CHECK(link.ack_count==3 && !link.tx_busy);

 LoraFrame_t command=Command(1,LORA_COMMAND_PUMP,1,30000);
 Feed(&command);Step(22120);
 CHECK(last_sent.kind==LORA_KIND_RESULT && execute_calls==1 && pump_starts==1 && last_sent.payload[4]==1);
 uint8_t original_result[LORA_RESULT_SIZE];memcpy(original_result,last_sent.payload,LORA_RESULT_SIZE);
 Step(22121);
 control.actuator.relay_on=0; /* 本地安全保护关闭，不允许重复命令重新开启。 */
 Feed(&command);Step(22122);CHECK(execute_calls==1 && !control.actuator.relay_on);
 Ack();Step(22123);CHECK(last_sent.kind==LORA_KIND_RESULT && memcmp(original_result,last_sent.payload,LORA_RESULT_SIZE)==0);
 Step(22124);Ack();Step(22125);Lora_GetStatus(&link);
 CHECK(link.duplicate_count==1 && link.command_count==1 && link.ack_count==5);
 LoraFrame_t conflict=command;conflict.payload[1]=0;Feed(&conflict);Step(22126);
 Lora_GetStatus(&link);CHECK(link.conflict_count==1 && execute_calls==1);
 LoraFrame_t addressed_wrong=Command(2,LORA_COMMAND_STATUS,0,30000);addressed_wrong.target=2;
 Feed(&addressed_wrong);Step(22127);CHECK(execute_calls==1);

 /* 在前一RESULT等待ACK时填满回传队列：没有结果空间的新命令不执行。 */
 for(uint32_t id=2;id<=6;++id)
 {
   LoraFrame_t query=Command(id,LORA_COMMAND_STATUS,0,30000);Feed(&query);Step(22130+id);
   if(id==2)Step(22133);
 }
 const unsigned int before=execute_calls;
 LoraFrame_t extra=Command(7,LORA_COMMAND_PUMP,1,30000);Feed(&extra);Step(22140);
 Lora_GetStatus(&link);CHECK(link.busy_drop_count==1 && execute_calls==before && pump_starts==1);
 /* 排空所有结果并确认，检查结果不被普通遥测抢占。 */
 for(unsigned int i=0;i<5;++i)
 {Ack();Step(22150+i*2);if(i<4){CHECK(last_sent.kind==LORA_KIND_RESULT);Step(22151+i*2);}}
 Lora_GetStatus(&link);CHECK(!link.tx_busy && !link.waiting_ack);
 const unsigned int sessions=random_calls;
 rx_failed=1;Step(23000);Lora_GetStatus(&link);CHECK(!link.ready);
 control.actuator.relay_on=1;Step(28000);CHECK(init_calls==4);
 control.actuator.relay_on=0;Step(28001);CHECK(init_calls==5 && random_calls==sessions);
 Step(28002);CHECK(last_sent.kind==LORA_KIND_HELLO && last_sent.session==SESSION);
 Step(28003);Ack();Step(28004);Step(28005);Ack();Step(28006);
 Feed(&command);Step(28007);CHECK(execute_calls==before && pump_starts==1 && last_sent.kind==LORA_KIND_RESULT);
 Step(28008);Ack();Step(28009);
 /* 遥测ACK丢失期间到达命令，RESULT应当立即让出普通遥测槽位。 */
 Step(33009);Step(33010);
 LoraFrame_t urgent=Command(8,LORA_COMMAND_STATUS,0,42000);Feed(&urgent);Step(33011);
 CHECK(last_sent.kind==LORA_KIND_RESULT && last_sent.transaction==8);
 Step(33012);Ack();Step(33013);
 /* 三次RESULT/遥测未确认后退出等待，不会无限重发。 */
 Step(38013);Step(38014);Step(40014);Step(40114);Step(40115);Step(42115);Step(42215);Step(42216);Step(44216);
 Lora_GetStatus(&link);CHECK(link.ack_timeout_count==1 && !link.tx_busy && !link.waiting_ack && !link.gateway_online);
 printf("duplex terminal service: %u assertions failed, %u starts, %u executions\n",failures,tx_starts,execute_calls);
 return failures?1:0;
}
