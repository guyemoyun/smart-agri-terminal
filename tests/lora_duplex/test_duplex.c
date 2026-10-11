#include "lora_protocol.h"
#include "lora_reliable.h"
#include "gateway_command.h"
#include "terminal_commands.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

static unsigned int failures,cases,failed_cases,execute_count,snapshot_count,pump_starts;
static ControlSnapshot_t output;
#define CHECK(x) do { if(!(x)){++failures;printf("FAIL %d: %s\n",__LINE__,#x);} }while(0)
#define SESSION 0x12345678U

static uint8_t Execute(void *context,uint8_t opcode,uint8_t argument,uint32_t now)
{
    (void)context;(void)now;++execute_count;
    if(opcode==LORA_COMMAND_PUMP)
    {
        if(output.policy.mode!=CONTROL_MODE_MANUAL)return CONTROL_WRONG_MODE;
        if(argument && !output.actuator.relay_on)++pump_starts;
        output.actuator.relay_on=argument;
    }
    else if(opcode==LORA_COMMAND_FAN)
    {
        if(output.policy.mode!=CONTROL_MODE_MANUAL)return CONTROL_WRONG_MODE;
        output.actuator.fan_level=argument;
        output.actuator.fan_duty_permille=argument==4 ? 1000U : 0U;
    }
    else if(opcode==LORA_COMMAND_MODE)output.policy.mode=(ControlMode_t)argument;
    else if(opcode==LORA_COMMAND_MUTE)output.policy.muted=argument;
    else if(opcode==LORA_COMMAND_STOP)
    {output.policy.mode=CONTROL_MODE_SAFE;output.actuator.relay_on=0;output.actuator.fan_level=0;}
    return CONTROL_OK;
}
static void Snapshot(void *context,ControlSnapshot_t *state)
{(void)context;++snapshot_count;*state=output;}
static const TerminalExecutor_t executor={.execute=Execute,.snapshot=Snapshot};
static void Reset(void)
{
    execute_count=snapshot_count=pump_starts=0;
    output=(ControlSnapshot_t){.policy={.mode=CONTROL_MODE_MANUAL},.actuator={.ready_mask=7}};
}
static LoraFrame_t Command(uint32_t transaction,uint16_t sequence,uint8_t opcode,uint8_t argument,uint32_t expires)
{
    LoraFrame_t frame={.kind=LORA_KIND_COMMAND,.flags=LORA_FLAG_RESULT,.source=0,.target=1,
        .sequence=sequence,.session=SESSION,.transaction=transaction,.payload_length=6};
    frame.payload[0]=opcode;frame.payload[1]=argument;Lora_Write32(&frame.payload[2],expires);
    return frame;
}
static LoraFrame_t RoundTrip(const LoraFrame_t *input)
{
    uint8_t bytes[96];LoraFrame_t parsed={0};
    const size_t length=Lora_Encode(input,bytes,sizeof(bytes));
    CHECK(length>0 && Lora_Decode(bytes,length,&parsed)==LORA_DECODE_OK);
    return parsed;
}

static void CodecBoundaries(void)
{
    LoraFrame_t command=Command(1,65535,LORA_COMMAND_PUMP,1,10000),decoded;
    uint8_t wire[97];memset(wire,0xEE,sizeof(wire));
    CHECK(Lora_Encode(&command,wire,27)==0 && wire[0]==0xEE);
    const size_t length=Lora_Encode(&command,wire,96);
    CHECK(length==28 && wire[0]==0xA5 && wire[1]==3 && wire[8]==255 && wire[9]==255 && wire[28]==0xEE);
    CHECK(Lora_Crc16((const uint8_t *)"123456789",9)==0x4B37 && Lora_Crc16(wire,length)==0);
    CHECK(Lora_Decode(wire,length,&decoded)==LORA_DECODE_OK);
    CHECK(decoded.transaction==1 && decoded.session==SESSION && decoded.source==0 && decoded.target==1);
    uint8_t invalid[97];
    for(size_t index=0;index<length;++index)
    {
        for(unsigned int bit=0;bit<8;++bit)
        {
            memcpy(invalid,wire,length);invalid[index]^=(uint8_t)(1U<<bit);
            decoded=(LoraFrame_t){.transaction=0xAA};
            CHECK(Lora_Decode(invalid,length,&decoded)!=LORA_DECODE_OK && decoded.transaction==0xAA);
        }
    }
    for(size_t size=0;size<length;++size)CHECK(Lora_Decode(wire,size,&decoded)!=LORA_DECODE_OK);
    CHECK(Lora_Decode(wire,length+1,&decoded)==LORA_DECODE_LENGTH);
    memcpy(invalid,wire,length);invalid[11]=1;Lora_Write16(&invalid[length-2],Lora_Crc16(invalid,length-2));
    CHECK(Lora_Decode(invalid,length,&decoded)==LORA_DECODE_HEADER);
    memcpy(invalid,wire,length);invalid[3]=3;Lora_Write16(&invalid[length-2],Lora_Crc16(invalid,length-2));
    CHECK(Lora_Decode(invalid,length,&decoded)==LORA_DECODE_SHAPE);
    memcpy(invalid,wire,length);invalid[10]=255;CHECK(Lora_Decode(invalid,length,&decoded)==LORA_DECODE_LENGTH);
    CHECK(Lora_Decode(0,length,&decoded)==LORA_DECODE_ARGUMENT);
    command.session=0;CHECK(Lora_Encode(&command,wire,sizeof(wire))==0);
    command.session=SESSION;command.kind=LORA_KIND_ACK;command.flags=0;command.payload_length=3;
    command.payload[0]=LORA_KIND_COMMAND;CHECK(Lora_Encode(&command,wire,sizeof(wire))==0);
}

static void AckMatchingAndRetry(void)
{
    LoraReliable_t tx;LoraReliable_Init(&tx);
    LoraFrame_t hello={.kind=LORA_KIND_HELLO,.flags=LORA_FLAG_ACK,.source=1,.target=0,
        .sequence=1,.session=SESSION,.transaction=1,.payload_length=8};
    CHECK(LoraReliable_Begin(&tx,&hello,100));
    uint8_t original[96];memcpy(original,tx.wire,tx.length);
    LoraFrame_t received=RoundTrip(&tx.message),ack;
    CHECK(Lora_BuildAck(&received,&ack));
    CHECK(!LoraReliable_AcceptReply(&tx,&ack));
    CHECK(LoraReliable_NeedsSend(&tx,100));LoraReliable_MarkSending(&tx);
    CHECK(!LoraReliable_AcceptReply(&tx,&ack));
    LoraReliable_TxFinished(&tx,1,3000);
    CHECK(tx.state==LORA_TX_WAITING && !LoraReliable_NeedsSend(&tx,3000));
    LoraFrame_t wrong=ack;wrong.source=2;CHECK(!LoraReliable_AcceptReply(&tx,&wrong));
    wrong=ack;++wrong.sequence;CHECK(!LoraReliable_AcceptReply(&tx,&wrong));
    wrong=ack;++wrong.session;CHECK(!LoraReliable_AcceptReply(&tx,&wrong));
    wrong=ack;++wrong.transaction;CHECK(!LoraReliable_AcceptReply(&tx,&wrong));
    wrong=ack;wrong.payload[1]^=1;CHECK(!LoraReliable_AcceptReply(&tx,&wrong));
    wrong=ack;wrong.payload[0]=LORA_KIND_RESULT;CHECK(!LoraReliable_AcceptReply(&tx,&wrong));
    LoraReliable_Tick(&tx,4999);CHECK(tx.state==LORA_TX_WAITING);
    LoraReliable_Tick(&tx,5000);CHECK(tx.state==LORA_TX_READY && tx.attempts==1);
    CHECK(!LoraReliable_NeedsSend(&tx,5099));CHECK(LoraReliable_NeedsSend(&tx,5100));
    CHECK(memcmp(original,tx.wire,tx.length)==0);
    /* 迟到ACK在下一次发送前仍可完成事务，避免无谓重发。 */
    CHECK(LoraReliable_AcceptReply(&tx,&ack));CHECK(tx.state==LORA_TX_COMPLETE);
    CHECK(!Lora_BuildAck(&ack,&wrong));
}

static void RetryExhaustionAndWrap(void)
{
    LoraReliable_t tx;LoraReliable_Init(&tx);
    const uint32_t start=UINT32_MAX-100U;
    LoraFrame_t hello={.kind=LORA_KIND_HELLO,.flags=LORA_FLAG_ACK,.source=1,.target=0,
        .sequence=65535,.session=SESSION,.transaction=UINT32_MAX,.payload_length=8};
    CHECK(LoraReliable_Begin(&tx,&hello,start));
    for(uint8_t attempt=1;attempt<=3;++attempt)
    {
        const uint32_t sent=start+(uint32_t)(attempt-1U)*2100U;
        CHECK(LoraReliable_NeedsSend(&tx,sent));LoraReliable_MarkSending(&tx);
        LoraReliable_TxFinished(&tx,1,sent);
        CHECK(tx.attempts==attempt);
        LoraReliable_Tick(&tx,sent+1999);CHECK(tx.state==LORA_TX_WAITING);
        LoraReliable_Tick(&tx,sent+2000);
    }
    CHECK(tx.state==LORA_TX_EXHAUSTED && tx.attempts==3 && !LoraReliable_NeedsSend(&tx,start+10000));
}

static void DuplicateConflictCacheEviction(void)
{
    Reset();TerminalCommands_t engine;TerminalCommands_Init(&engine,1,0,SESSION);
    LoraFrame_t command=Command(1,1,LORA_COMMAND_PUMP,1,10000),result;
    CHECK(TerminalCommands_Process(&engine,&command,&executor,100,&result)==TERMINAL_COMMAND_NEW);
    CHECK(execute_count==1 && pump_starts==1 && result.payload[2]==CONTROL_OK);
    uint8_t original[LORA_RESULT_SIZE];memcpy(original,result.payload,LORA_RESULT_SIZE);
    output.actuator.relay_on=0; /* 模拟水泵已经被本地安全保护关闭。 */
    CHECK(TerminalCommands_Process(&engine,&command,&executor,20000,&result)==TERMINAL_COMMAND_DUPLICATE);
    CHECK(execute_count==1 && pump_starts==1 && !output.actuator.relay_on && memcmp(original,result.payload,LORA_RESULT_SIZE)==0);
    LoraFrame_t conflict=command;conflict.payload[1]=0;
    CHECK(TerminalCommands_Process(&engine,&conflict,&executor,20000,&result)==TERMINAL_COMMAND_CONFLICT);
    conflict=command;++conflict.sequence;
    CHECK(TerminalCommands_Process(&engine,&conflict,&executor,20000,&result)==TERMINAL_COMMAND_CONFLICT);
    for(uint32_t id=2;id<=10;++id)
    {
        LoraFrame_t query=Command(id,(uint16_t)id,LORA_COMMAND_STATUS,0,30000);
        CHECK(TerminalCommands_Process(&engine,&query,&executor,20000,&result)==TERMINAL_COMMAND_NEW);
    }
    const unsigned int before=execute_count;
    CHECK(TerminalCommands_Process(&engine,&command,&executor,20000,&result)==TERMINAL_COMMAND_STALE);
    CHECK(result.payload[2]==LORA_RESULT_STALE && execute_count==before && pump_starts==1);
}

static void CommandValidationAndTime(void)
{
    Reset();TerminalCommands_t engine;TerminalCommands_Init(&engine,1,0,SESSION);
    LoraFrame_t command=Command(1,1,LORA_COMMAND_FAN,4,1000),result;
    command.source=2;CHECK(TerminalCommands_Process(&engine,&command,&executor,0,&result)==TERMINAL_COMMAND_DROP);
    command.source=0;command.target=65535;CHECK(TerminalCommands_Process(&engine,&command,&executor,0,&result)==TERMINAL_COMMAND_DROP);
    command.target=1;command.session^=1;CHECK(TerminalCommands_Process(&engine,&command,&executor,0,&result)==TERMINAL_COMMAND_DROP);
    command.session=SESSION;command.transaction=0;CHECK(TerminalCommands_Process(&engine,&command,&executor,0,&result)==TERMINAL_COMMAND_DROP);
    command.transaction=1;
    CHECK(TerminalCommands_Process(&engine,&command,&executor,1000,&result)==TERMINAL_COMMAND_NEW);
    CHECK(result.payload[2]==LORA_RESULT_EXPIRED && execute_count==0);
    command=Command(2,2,LORA_COMMAND_FAN,5,2000);
    CHECK(TerminalCommands_Process(&engine,&command,&executor,1000,&result)==TERMINAL_COMMAND_NEW);
    CHECK(result.payload[2]==CONTROL_INVALID_ARGUMENT && execute_count==0);
    command=Command(3,3,LORA_COMMAND_FAN,4,40000);
    CHECK(TerminalCommands_Process(&engine,&command,&executor,1000,&result)==TERMINAL_COMMAND_NEW);
    CHECK(result.payload[2]==CONTROL_INVALID_ARGUMENT && execute_count==0);
    command=Command(4,65535,LORA_COMMAND_FAN,4,100U);
    CHECK(TerminalCommands_Process(&engine,&command,&executor,UINT32_MAX-100U,&result)==TERMINAL_COMMAND_NEW);
    CHECK(result.payload[2]==CONTROL_OK && execute_count==1 && output.actuator.fan_level==4);
    command=Command(5,0,LORA_COMMAND_FAN,0,200U);
    CHECK(TerminalCommands_Process(&engine,&command,&executor,101U,&result)==TERMINAL_COMMAND_NEW);
    CHECK(output.actuator.fan_level==0 && execute_count==2);
    command=Command(UINT32_MAX,10,LORA_COMMAND_STATUS,0,1000);
    CHECK(TerminalCommands_Process(&engine,&command,&executor,102U,&result)==TERMINAL_COMMAND_NEW);
    command=Command(1,11,LORA_COMMAND_PUMP,1,1000);
    CHECK(TerminalCommands_Process(&engine,&command,&executor,103U,&result)!=TERMINAL_COMMAND_NEW);
    CHECK(!output.actuator.relay_on);
}

static void GatewayLostResultClosedLoop(void)
{
    Reset();TerminalCommands_t node;TerminalCommands_Init(&node,1,0,SESSION);
    GatewayCommand_t gateway;GatewayCommand_Init(&gateway,1,SESSION);
    CHECK(!GatewayCommand_Begin(&gateway,1,1,LORA_COMMAND_FAN,5,100,10000,0));
    CHECK(GatewayCommand_Begin(&gateway,1,1,LORA_COMMAND_PUMP,1,100,10000,0));
    CHECK(!GatewayCommand_Begin(&gateway,2,2,LORA_COMMAND_STATUS,0,100,10000,0));
    LoraReliable_MarkSending(&gateway.tx);LoraReliable_TxFinished(&gateway.tx,1,10);
    LoraFrame_t received=RoundTrip(&gateway.tx.message),response,ack;
    CHECK(TerminalCommands_Process(&node,&received,&executor,100,&response)==TERMINAL_COMMAND_NEW);
    CHECK(pump_starts==1);
    /* 首个RESULT丢失，网关按原事务重发。 */
    LoraReliable_Tick(&gateway.tx,2010);CHECK(gateway.tx.state==LORA_TX_READY);
    LoraReliable_MarkSending(&gateway.tx);LoraReliable_TxFinished(&gateway.tx,1,2200);
    received=RoundTrip(&gateway.tx.message);
    CHECK(TerminalCommands_Process(&node,&received,&executor,2300,&response)==TERMINAL_COMMAND_DUPLICATE);
    CHECK(pump_starts==1 && execute_count==1);
    LoraReliable_t result_tx;LoraReliable_Init(&result_tx);CHECK(LoraReliable_Begin(&result_tx,&response,2300));
    LoraReliable_MarkSending(&result_tx);LoraReliable_TxFinished(&result_tx,1,2310);
    received=RoundTrip(&result_tx.message);
    CHECK(GatewayCommand_AcceptResult(&gateway,&received,&ack));
    CHECK(gateway.has_result && gateway.result.payload[2]==CONTROL_OK && gateway.tx.state==LORA_TX_COMPLETE);
    /* RESULT的ACK丢失，终端重发；网关再次ACK但不当成新命令。 */
    LoraReliable_Tick(&result_tx,4310);LoraReliable_MarkSending(&result_tx);LoraReliable_TxFinished(&result_tx,1,4320);
    received=RoundTrip(&result_tx.message);
    CHECK(GatewayCommand_AcceptResult(&gateway,&received,&ack));
    CHECK(LoraReliable_AcceptReply(&result_tx,&ack) && result_tx.state==LORA_TX_COMPLETE);
    CHECK(execute_count==1 && pump_starts==1);
    CHECK(!GatewayCommand_Begin(&gateway,1,1,LORA_COMMAND_STATUS,0,4500,10000,4500));
    const LoraFrame_t earlier=received;
    CHECK(GatewayCommand_Begin(&gateway,2,2,LORA_COMMAND_PUMP,1,4500,10000,4500));
    CHECK(GatewayCommand_AcceptResult(&gateway,&received,&ack));
    CHECK(!gateway.has_result && gateway.tx.state==LORA_TX_READY);
    LoraReliable_MarkSending(&gateway.tx);LoraReliable_TxFinished(&gateway.tx,1,4510);
    output.policy.mode=CONTROL_MODE_SAFE;
    received=RoundTrip(&gateway.tx.message);
    CHECK(TerminalCommands_Process(&node,&received,&executor,4520,&response)==TERMINAL_COMMAND_NEW);
    received=RoundTrip(&response);CHECK(GatewayCommand_AcceptResult(&gateway,&received,&ack));
    CHECK(gateway.tx.state==LORA_TX_COMPLETE && gateway.result.payload[2]==CONTROL_WRONG_MODE);
    CHECK(GatewayCommand_AcceptResult(&gateway,&earlier,&ack));
    CHECK(gateway.result.transaction==2 && gateway.result.payload[2]==CONTROL_WRONG_MODE);
}

static void BadResultCannotComplete(void)
{
    Reset();GatewayCommand_t gateway;GatewayCommand_Init(&gateway,1,SESSION);
    CHECK(GatewayCommand_Begin(&gateway,1,1,LORA_COMMAND_FAN,4,0,10000,0));
    LoraReliable_MarkSending(&gateway.tx);LoraReliable_TxFinished(&gateway.tx,1,0);
    TerminalCommands_t node;TerminalCommands_Init(&node,1,0,SESSION);
    LoraFrame_t command=RoundTrip(&gateway.tx.message),result,ack;
    CHECK(TerminalCommands_Process(&node,&command,&executor,1,&result)==TERMINAL_COMMAND_NEW);
    result=RoundTrip(&result);
    LoraFrame_t wrong=result;wrong.payload[1]=3;
    CHECK(!GatewayCommand_AcceptResult(&gateway,&wrong,&ack));
    wrong=result;wrong.payload[2]=99;CHECK(!GatewayCommand_AcceptResult(&gateway,&wrong,&ack));
    wrong=result;wrong.target=2;CHECK(!GatewayCommand_AcceptResult(&gateway,&wrong,&ack));
    wrong=result;Lora_Write16(&wrong.payload[8],1001);CHECK(!GatewayCommand_AcceptResult(&gateway,&wrong,&ack));
    CHECK(gateway.tx.state==LORA_TX_WAITING && !gateway.has_result);
    CHECK(GatewayCommand_AcceptResult(&gateway,&result,&ack));
}

static void GatewayRestartCursor(void)
{
    GatewayCommand_t gateway;GatewayCommand_Init(&gateway,1,SESSION);
    LoraFrame_t hello={.kind=LORA_KIND_HELLO,.flags=LORA_FLAG_ACK,.source=1,.target=0,
        .session=SESSION,.transaction=40,.payload_length=8};
    Lora_Write32(&hello.payload[4],500);
    hello=RoundTrip(&hello);
    CHECK(GatewayCommand_ObserveNode(&gateway,&hello));
    CHECK(gateway.last_transaction==500);
    CHECK(!GatewayCommand_Begin(&gateway,1,1,LORA_COMMAND_PUMP,1,0,10000,0));
    CHECK(GatewayCommand_Begin(&gateway,501,1,LORA_COMMAND_STATUS,0,0,10000,0));
    LoraFrame_t wrong=hello;wrong.session++;
    CHECK(!GatewayCommand_ObserveNode(&gateway,&wrong));
    CHECK(gateway.last_transaction==501);
    Lora_Write32(&hello.payload[4],400);
    CHECK(GatewayCommand_ObserveNode(&gateway,&hello) && gateway.last_transaction==501);
}

static void Run(const char *name,void(*test)(void))
{
    const unsigned int before=failures;++cases;test();
    if(before==failures)printf("PASS %s\n",name);else ++failed_cases;
}
int main(void)
{
    Run("codec_crc_every_bit_bounds",CodecBoundaries);Run("ack_matching_late_reply",AckMatchingAndRetry);
    Run("three_attempts_tick_wrap",RetryExhaustionAndWrap);Run("dedup_conflict_eviction",DuplicateConflictCacheEviction);
    Run("address_session_ttl_parameters",CommandValidationAndTime);Run("gateway_node_loss_duplicate_loop",GatewayLostResultClosedLoop);
    Run("bad_result_not_success",BadResultCannotComplete);Run("gateway_restart_cursor",GatewayRestartCursor);
    printf("%u/%u duplex groups passed; %u assertions failed\n",cases-failed_cases,cases,failures);
    return failures?1:0;
}
