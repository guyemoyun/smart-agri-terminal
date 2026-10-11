#include "lora_service.h"
#include "llcc68_p2p.h"
#include "terminal_telemetry.h"
#include "terminal_commands.h"
#include "lora_reliable.h"
#include "button_service.h"
#include <stdio.h>
#include <string.h>

#define INIT_RETRY_MS 5000U
#define REPORT_PERIOD_MS 5000U
#define GATEWAY_OFFLINE_MS 15000U
#define TX_TIMEOUT_MS 1500U
#define RESULT_QUEUE_SIZE 4U

static LoraStatus_t state;
static LoraReliable_t reliable;
static TerminalCommands_t commands;
static LoraFrame_t result_queue[RESULT_QUEUE_SIZE];
static uint8_t result_head,result_tail,result_count,hello_needed;
static uint32_t last_init_attempt,last_report,last_gateway_tick,next_transaction;
static uint16_t sequence;

static uint8_t ReliableActive(void)
{
    return (uint8_t)(reliable.state==LORA_TX_READY || reliable.state==LORA_TX_SENDING || reliable.state==LORA_TX_WAITING);
}

static void RefreshFlags(void)
{
    state.waiting_ack=(uint8_t)(reliable.state==LORA_TX_WAITING);
}

static void RadioFailed(uint32_t now)
{
    state.ready=0U;state.tx_busy=0U;state.rx_active=0U;state.gateway_online=0U;
    reliable.state=LORA_TX_EXHAUSTED;
    last_init_attempt=now;RefreshFlags();
}

void Lora_ServiceInit(void)
{
    memset(&state,0,sizeof(state));LoraReliable_Init(&reliable);
    memset(&commands,0,sizeof(commands));memset(result_queue,0,sizeof(result_queue));
    result_head=result_tail=result_count=0U;hello_needed=1U;sequence=0U;next_transaction=1U;
    last_init_attempt=HAL_GetTick()-INIT_RETRY_MS;
    last_report=HAL_GetTick()-REPORT_PERIOD_MS;last_gateway_tick=0U;
    printf("LoRa: duplex protocol v3 initialized, radio not checked\r\n");
}

void Lora_GetStatus(LoraStatus_t *status)
{
    if(status!=0) *status=state;
}

static uint8_t ResumeRX(uint32_t now)
{
    if(llcc68_lora_rx_start(&llcc68_ctx)!=LLCC68_STATUS_OK)
    {
        ++state.rx_error_count;RadioFailed(now);return 0U;
    }
    state.rx_active=1U;return 1U;
}

static void QueueResult(const LoraFrame_t *response)
{
    result_queue[result_tail]=*response;
    result_tail=(uint8_t)((result_tail+1U)%RESULT_QUEUE_SIZE);++result_count;
}

static void Receive(uint32_t now)
{
    if(!state.rx_active) return;
    uint8_t wire[LORA_MAX_FRAME],length=0U;
    llcc68_pkt_status_lora_t radio_status;
    const llcc68_rx_result_t result=llcc68_lora_rx_poll(&llcc68_ctx,wire,sizeof(wire),&length,&radio_status);
    if(result==LLCC68_RX_PENDING) return;
    /* SPI读取可能消耗时间，按实际消费时刻校验命令截止时间。 */
    now=HAL_GetTick();
    state.rx_active=0U;
    if(result==LLCC68_RX_FAILED) { ++state.rx_error_count;RadioFailed(now);return; }
    if(result==LLCC68_RX_DROPPED) { ++state.rx_error_count;(void)ResumeRX(now);return; }
    LoraFrame_t frame;
    if(Lora_Decode(wire,length,&frame)!=LORA_DECODE_OK || frame.source!=LORA_GATEWAY_ADDRESS ||
       frame.target!=LORA_NODE_ADDRESS || frame.session!=state.session ||
       (frame.kind!=LORA_KIND_COMMAND && frame.kind!=LORA_KIND_ACK))
    {
        ++state.rx_error_count;(void)ResumeRX(now);return;
    }
    ++state.rx_count;state.last_rssi_dbm=radio_status.rssi_pkt_in_dbm;state.last_snr_db=radio_status.snr_pkt_in_db;
    if(frame.kind==LORA_KIND_ACK)
    {
        /* 只有匹配 ACK 才确认事务或刷新网关在线状态，迟到无关 ACK 不计。 */
        if(LoraReliable_AcceptReply(&reliable,&frame))
        {
            ++state.ack_count;state.gateway_online=1U;last_gateway_tick=now;
            if(reliable.message.kind==LORA_KIND_HELLO) hello_needed=0U;
        }
    }
    else
    {
        state.gateway_online=1U;last_gateway_tick=now;
        if(result_count>=RESULT_QUEUE_SIZE) ++state.busy_drop_count;
        else
        {
            LoraFrame_t response;
            const TerminalCommandOutcome_t outcome=TerminalCommands_Process(&commands,&frame,
                TerminalCommands_ControlExecutor(),now,&response);
            if(outcome==TERMINAL_COMMAND_CONFLICT) ++state.conflict_count;
            else if(outcome==TERMINAL_COMMAND_DROP) ++state.rx_error_count;
            else
            {
                if(outcome==TERMINAL_COMMAND_DUPLICATE) ++state.duplicate_count;
                else ++state.command_count;
                QueueResult(&response);
                /* 正在等待普通上报的 ACK 时让出槽位，控制结果不能被遥测重试拖住。 */
                if(ReliableActive() && reliable.message.kind!=LORA_KIND_RESULT)
                {
                    LoraReliable_Init(&reliable);
                    last_report=now;
                }
            }
        }
    }
    (void)ResumeRX(now);RefreshFlags();
}

static uint8_t BeginMessage(uint32_t now)
{
    LoraFrame_t message;
    if(result_count!=0U)
    {
        message=result_queue[result_head];
        result_head=(uint8_t)((result_head+1U)%RESULT_QUEUE_SIZE);--result_count;
    }
    else
    {
        if((now-last_report)<REPORT_PERIOD_MS) return 0U;
        /* 事务号不循环复用；耗尽后等待新上电会话，不重复旧身份。 */
        if(next_transaction==0U) return 0U;
        message=(LoraFrame_t){.flags=LORA_FLAG_ACK,.source=LORA_NODE_ADDRESS,
            .target=LORA_GATEWAY_ADDRESS,.sequence=sequence++,.session=state.session,
            .transaction=next_transaction++};
        if(hello_needed)
        {
            message.kind=LORA_KIND_HELLO;message.payload_length=8U;
            Lora_Write32(message.payload,now);
            Lora_Write32(&message.payload[4],commands.high_water);
        }
        else
        {
            SensorData_t sensor;ControlSnapshot_t control;
            Sensor_GetSnapshot(&sensor);Control_GetSnapshot(&control);
            message.kind=LORA_KIND_TELEMETRY;message.payload_length=TELEMETRY_FRAME_SIZE+4U;
            if(Terminal_EncodeTelemetry(message.payload,sizeof(message.payload),LORA_NODE_ADDRESS,
                message.sequence,&sensor,&control,now)!=TELEMETRY_FRAME_SIZE) return 0U;
            Lora_Write32(&message.payload[TELEMETRY_FRAME_SIZE],commands.high_water);
        }
    }
    if(!LoraReliable_Begin(&reliable,&message,now)) { ++state.tx_error_count;return 0U; }
    state.last_sequence=message.sequence;return 1U;
}

void Lora_ServiceTask(uint32_t now)
{
    if(state.gateway_online && (now-last_gateway_tick)>=GATEWAY_OFFLINE_MS) state.gateway_online=0U;
    if(!state.ready)
    {
        ControlSnapshot_t control;Control_GetSnapshot(&control);
        if(control.actuator.relay_on || control.actuator.fan_duty_permille || control.actuator.buzzer_on ||
            Button_IsHeld() || (now-last_init_attempt)<INIT_RETRY_MS) return;
        last_init_attempt=now;
        llcc68_chip_status_t chip;
        if(llcc68_init(&llcc68_ctx)!=LLCC68_STATUS_OK || llcc68_get_status(&llcc68_ctx,&chip)!=LLCC68_STATUS_OK ||
            (chip.chip_mode!=LLCC68_CHIP_MODE_STBY_XOSC && chip.chip_mode!=LLCC68_CHIP_MODE_STBY_RC))
        { ++state.init_error_count;return; }
        if(state.session==0U)
        {
            uint32_t session;
            if(llcc68_get_random_numbers(&llcc68_ctx,&session,1U)!=LLCC68_STATUS_OK || session==0U || session==UINT32_MAX)
            { ++state.init_error_count;return; }
            state.session=session;
            TerminalCommands_Init(&commands,LORA_NODE_ADDRESS,LORA_GATEWAY_ADDRESS,session);
        }
        state.ready=1U;hello_needed=1U;
        LoraReliable_Init(&reliable);
        last_report=HAL_GetTick()-REPORT_PERIOD_MS;
        (void)ResumeRX(HAL_GetTick());
        return;
    }
    if(state.tx_busy)
    {
        const llcc68_tx_result_t result=llcc68_lora_tx_poll(&llcc68_ctx,now);
        if(result==LLCC68_TX_PENDING) return;
        now=HAL_GetTick();
        state.tx_busy=0U;
        LoraReliable_TxFinished(&reliable,(uint8_t)(result==LLCC68_TX_DONE),now);
        if(result==LLCC68_TX_DONE) ++state.tx_done_count;
        else { ++state.tx_error_count;RadioFailed(now);return; }
        (void)ResumeRX(now);RefreshFlags();return;
    }
    Receive(now);
    if(!state.ready) return;
    LoraReliable_Tick(&reliable,now);
    if(reliable.state==LORA_TX_COMPLETE || reliable.state==LORA_TX_EXHAUSTED)
    {
        const uint8_t hello_complete=(uint8_t)(reliable.state==LORA_TX_COMPLETE && reliable.message.kind==LORA_KIND_HELLO);
        if(reliable.state==LORA_TX_EXHAUSTED)
        {
            ++state.ack_timeout_count;state.gateway_online=0U;
        }
        last_report=now;LoraReliable_Init(&reliable);
        /* HELLO 成功后尽快上报；其他事务不因 ACK 计数巧合而打破周期。 */
        if(hello_complete) last_report=now-REPORT_PERIOD_MS;
    }
    if(!ReliableActive()) (void)BeginMessage(now);
    if(LoraReliable_NeedsSend(&reliable,now))
    {
        if(reliable.attempts!=0U) ++state.retry_count;
        LoraReliable_MarkSending(&reliable);state.rx_active=0U;
        if(llcc68_lora_send_start(&llcc68_ctx,reliable.wire,reliable.length,TX_TIMEOUT_MS)!=LLCC68_STATUS_OK)
        { ++state.tx_error_count;RadioFailed(now);return; }
        state.tx_busy=1U;
    }
    RefreshFlags();
}
