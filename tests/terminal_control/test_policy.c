#include "control_policy.h"
#include "button_state.h"
#include "co2_parser.h"
#include "terminal_telemetry.h"
#include "terminal_view.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>

static unsigned int failures, cases;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static ControlInput_t Valid(uint32_t now)
{
    return (ControlInput_t){.temperature_x10=250, .co2_ppm=800, .soil_level=2,
        .soil_valid=1, .dht_valid=1, .co2_valid=1, .ready_mask=ACTUATOR_READY_ALL,
        .soil_tick=now, .dht_tick=now, .co2_tick=now};
}

static void StartupAndConfig(void)
{
    ControlConfig_t config;
    ControlState_t state;
    ControlInput_t input=Valid(0);
    ControlPolicy_DefaultConfig(&config);
    CHECK(ControlPolicy_ConfigValid(&config));
    CHECK(!config.auto_irrigation_enabled);
    ControlPolicy_Init(&state,0);
    CHECK(state.mode==CONTROL_MODE_MANUAL && !state.relay_on && !state.fan_level);
    input.soil_valid=input.dht_valid=input.co2_valid=0;
    ControlPolicy_Update(&state,&config,&input,0);
    CHECK(state.alarm_mask==0);
    ControlPolicy_Update(&state,&config,&input,8000);
    CHECK(state.alarm_mask==(ALARM_SOIL_INVALID|ALARM_DHT_INVALID|ALARM_CO2_INVALID));
    config.temperature_on_x10=config.temperature_off_x10;
    CHECK(!ControlPolicy_ConfigValid(&config));
    ControlPolicy_Update(&state,&config,&input,8001);
    CHECK(state.mode==CONTROL_MODE_SAFE && (state.alarm_mask&ALARM_CONFIG));
    CHECK(!state.relay_on && !state.fan_level);
}

static void FanHysteresis(void)
{
    ControlConfig_t config;
    ControlState_t state;
    ControlInput_t input=Valid(100);
    ControlPolicy_DefaultConfig(&config); ControlPolicy_Init(&state,100);
    CHECK(ControlPolicy_SetMode(&state,&config,&input,CONTROL_MODE_AUTO,100)==CONTROL_OK);
    input.temperature_x10=300;
    ControlPolicy_Update(&state,&config,&input,100);
    CHECK(state.fan_level==2 && (state.alarm_mask&ALARM_TEMPERATURE));
    input.temperature_x10=290;
    ControlPolicy_Update(&state,&config,&input,101);
    CHECK(state.fan_level==2);
    input.co2_ppm=1200;
    ControlPolicy_Update(&state,&config,&input,102);
    CHECK(state.fan_level==3);
    input.co2_ppm=1100; input.temperature_x10=280;
    ControlPolicy_Update(&state,&config,&input,103);
    CHECK(state.fan_level==3);
    input.co2_ppm=1000;
    ControlPolicy_Update(&state,&config,&input,104);
    CHECK(!state.fan_level);
    input.temperature_x10=400; input.co2_ppm=1800;
    ControlPolicy_Update(&state,&config,&input,105);
    input.co2_valid=0;
    ControlPolicy_Update(&state,&config,&input,106);
    CHECK(state.fan_level==2);
    input.dht_valid=0;
    ControlPolicy_Update(&state,&config,&input,107);
    CHECK(!state.fan_level);
}

static void PumpTimeoutAndCooldown(void)
{
    ControlConfig_t config;
    ControlState_t state;
    ControlInput_t input=Valid(100);
    ControlPolicy_DefaultConfig(&config); ControlPolicy_Init(&state,100);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,100)==CONTROL_OK);
    CHECK(state.pump_start_tick==100);
    input=Valid(5000);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,5000)==CONTROL_OK);
    CHECK(state.pump_start_tick==100);
    input=Valid(10100);
    ControlPolicy_Update(&state,&config,&input,10100);
    CHECK(state.mode==CONTROL_MODE_SAFE && !state.relay_on && (state.alarm_mask&ALARM_PUMP_TIMEOUT));
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,10100)==CONTROL_WRONG_MODE);
    CHECK(ControlPolicy_SetMode(&state,&config,&input,CONTROL_MODE_MANUAL,10100)==CONTROL_OK);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,10101)==CONTROL_COOLDOWN);
    input=Valid(70100);
    ControlPolicy_Update(&state,&config,&input,70100);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,70100)==CONTROL_OK);
    CHECK(state.relay_on);
    input.soil_valid=0;
    ControlPolicy_Update(&state,&config,&input,70101);
    CHECK(!state.relay_on);
}

static void AutoIrrigationOptIn(void)
{
    ControlConfig_t config;
    ControlState_t state;
    ControlInput_t input=Valid(0);
    ControlPolicy_DefaultConfig(&config); ControlPolicy_Init(&state,0);
    input.soil_level=1;
    CHECK(ControlPolicy_SetMode(&state,&config,&input,CONTROL_MODE_AUTO,0)==CONTROL_OK);
    ControlPolicy_Update(&state,&config,&input,0); CHECK(!state.relay_on);
    config.auto_irrigation_enabled=1;
    ControlPolicy_Update(&state,&config,&input,1); CHECK(state.relay_on);
    input.soil_level=2; ControlPolicy_Update(&state,&config,&input,2); CHECK(state.relay_on);
    input.soil_level=3; ControlPolicy_Update(&state,&config,&input,3); CHECK(!state.relay_on);
    input.soil_level=1; ControlPolicy_Update(&state,&config,&input,4); CHECK(!state.relay_on);
    input=Valid(60003); input.soil_level=1;
    ControlPolicy_Update(&state,&config,&input,60003); CHECK(state.relay_on);
    config.auto_irrigation_enabled=0;
    ControlPolicy_Update(&state,&config,&input,60004); CHECK(!state.relay_on);
}

static void ManualLeaseAndArguments(void)
{
    ControlConfig_t config;
    ControlState_t state;
    ControlInput_t input=Valid(0);
    ControlPolicy_DefaultConfig(&config); ControlPolicy_Init(&state,0);
    CHECK(ControlPolicy_SetFan(&state,4,0)==CONTROL_OK);
    CHECK(ControlPolicy_SetFan(&state,4,59999)==CONTROL_OK);
    input=Valid(60000); ControlPolicy_Update(&state,&config,&input,60000);
    CHECK(!state.fan_level);
    CHECK(ControlPolicy_SetFan(&state,5,60001)==CONTROL_INVALID_ARGUMENT);
    CHECK(!state.fan_level);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,2,60001)==CONTROL_INVALID_ARGUMENT);
    input.soil_level=0;
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,60001)==CONTROL_SENSOR_INVALID);
    input=Valid(100);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,4000)==CONTROL_SENSOR_INVALID);
    CHECK(ControlPolicy_SetMode(&state,&config,&input,(ControlMode_t)99,4000)==CONTROL_INVALID_ARGUMENT);
}

static void EmergencyMuteAndFault(void)
{
    ControlConfig_t config;
    ControlState_t state;
    ControlInput_t input=Valid(0);
    ControlPolicy_DefaultConfig(&config); ControlPolicy_Init(&state,0);
    CHECK(ControlPolicy_SetFan(&state,4,0)==CONTROL_OK);
    ControlPolicy_StopAll(&state,1);
    CHECK(state.mode==CONTROL_MODE_SAFE && !state.fan_level && !state.relay_on);
    ControlPolicy_Update(&state,&config,&input,2); CHECK(state.alarm_mask&ALARM_EMERGENCY);
    ControlPolicy_SetMuted(&state,1);
    ControlPolicy_Update(&state,&config,&input,3); CHECK(!state.buzzer_on && state.muted);
    input.temperature_x10=350;
    ControlPolicy_Update(&state,&config,&input,4);
    CHECK(!state.muted && (state.alarm_mask&ALARM_EMERGENCY));
    input.ready_mask=ACTUATOR_READY_RELAY;
    CHECK(ControlPolicy_SetMode(&state,&config,&input,CONTROL_MODE_AUTO,5)==CONTROL_NOT_READY);
    ControlPolicy_Update(&state,&config,&input,5); CHECK(state.alarm_mask&ALARM_ACTUATOR);
    input=Valid(6);
    ControlPolicy_Update(&state,&config,&input,6); CHECK(state.mode==CONTROL_MODE_SAFE);
    CHECK(ControlPolicy_SetMode(&state,&config,&input,CONTROL_MODE_MANUAL,6)==CONTROL_OK);
    ControlPolicy_Update(&state,&config,&input,6); CHECK(!(state.alarm_mask&ALARM_EMERGENCY));
}

static void TickWrap(void)
{
    ControlConfig_t config;
    ControlState_t state;
    const uint32_t start=UINT32_MAX-100U;
    ControlInput_t input=Valid(start);
    ControlPolicy_DefaultConfig(&config); config.pump_max_ms=200U;
    ControlPolicy_Init(&state,start);
    CHECK(ControlPolicy_SetPump(&state,&config,&input,1,start)==CONTROL_OK);
    input=Valid(start+199U); ControlPolicy_Update(&state,&config,&input,start+199U); CHECK(state.relay_on);
    input=Valid(start+200U); ControlPolicy_Update(&state,&config,&input,start+200U);
    CHECK(!state.relay_on && state.mode==CONTROL_MODE_SAFE);
}

static void Buttons(void)
{
    ButtonState_t button;
    ButtonState_Init(&button,0);
    CHECK(!ButtonState_Update(&button,1,0));
    CHECK(!ButtonState_Update(&button,0,5));
    CHECK(!ButtonState_Update(&button,1,10));
    CHECK(!ButtonState_Update(&button,1,30));
    CHECK(!ButtonState_Update(&button,0,100));
    CHECK(ButtonState_Update(&button,0,120)==BUTTON_EVENT_SHORT);
    CHECK(!ButtonState_Update(&button,0,121));
    CHECK(!ButtonState_Update(&button,1,200));
    CHECK(!ButtonState_Update(&button,1,220));
    CHECK(ButtonState_Update(&button,1,1720)==BUTTON_EVENT_LONG);
    CHECK(!ButtonState_Update(&button,1,2000));
    CHECK(!ButtonState_Update(&button,0,2001));
    CHECK(!ButtonState_Update(&button,0,2021));
    ButtonState_Init(&button,UINT32_MAX-100U);
    CHECK(!ButtonState_Update(&button,1,UINT32_MAX-100U));
    CHECK(!ButtonState_Update(&button,1,UINT32_MAX-80U));
    CHECK(ButtonState_Update(&button,1,1419U)==BUTTON_EVENT_LONG);
}

static void Parser(void)
{
    CO2Parser_t parser;
    uint16_t ppm=0;
    const uint8_t valid[]={0x2C,0x03,0xE8,0,0,0x17};
    CO2Parser_Init(&parser);
    CHECK(!CO2Parser_Feed(&parser,0x80,&ppm));
    for(unsigned int i=0;i<5;++i)CHECK(!CO2Parser_Feed(&parser,valid[i],&ppm));
    CHECK(CO2Parser_Feed(&parser,valid[5],&ppm)==1 && ppm==1000);
    uint8_t broken[]={0x2C,0x03,0xE8,0,0,0x18};
    for(unsigned int i=0;i<5;++i)CHECK(!CO2Parser_Feed(&parser,broken[i],&ppm));
    CHECK(CO2Parser_Feed(&parser,broken[5],&ppm)==2);
    CHECK(!CO2Parser_Feed(&parser,0x2C,&ppm));
    for(unsigned int i=0;i<6;++i)
    {
        const uint8_t result=CO2Parser_Feed(&parser,valid[i],&ppm);
        if(i==5)CHECK(result==1 && ppm==1000);
    }
}

static void Telemetry(void)
{
    SensorData_t sensor={.temperature=-0.1f,.humidity=100.0f,.co2_ppm=1000,.light_lux=65535,.soil_level=3,
        .light_valid=1,.soil_valid=1,.dht22_valid=1,.co2_valid=1,.adc_update_tick=100,
        .dht22_update_tick=100,.co2_update_tick=100};
    ControlSnapshot_t control={.policy={.mode=CONTROL_MODE_MANUAL,.alarm_mask=ALARM_TEMPERATURE},
        .actuator={.ready_mask=7,.relay_on=1,.fan_level=4,.fan_duty_permille=1000}};
    uint8_t frame[TELEMETRY_FRAME_SIZE+1U]; memset(frame,0xEE,sizeof(frame));
    CHECK(Terminal_EncodeTelemetry(frame,33,1,65535,&sensor,&control,100)==0);
    CHECK(frame[0]==0xEE);
    CHECK(Terminal_EncodeTelemetry(frame,34,1,65535,&sensor,&control,100)==34);
    CHECK(frame[0]==0xA5 && frame[1]==2 && frame[7]==24 && frame[17]==15);
    CHECK(frame[8]==255 && frame[9]==255 && frame[10]==0xE8 && frame[11]==3);
    CHECK(frame[22]==0xE8 && frame[23]==3 && frame[20]==4 && frame[34]==0xEE);
    CHECK(Terminal_Crc16((const uint8_t *)"123456789",9)==0x4B37);
    CHECK(Terminal_Crc16(frame,34)==0);
    sensor.temperature=NAN;
    CHECK(Terminal_EncodeTelemetry(frame,34,1,1,&sensor,&control,100)==34 && !(frame[17]&4));
    CHECK(Terminal_EncodeTelemetry(frame,34,1,1,&sensor,&control,9000)==34 && frame[17]==0);
    CHECK(Terminal_EncodeTelemetry(0,34,1,1,&sensor,&control,100)==0);
}

static void Views(void)
{
    SensorData_t sensor={.temperature=-0.1f,.humidity=100.0f,.co2_ppm=65535,.light_lux=65535,.soil_level=4,
        .light_valid=1,.soil_valid=1,.dht22_valid=1,.co2_valid=1};
    ControlSnapshot_t control={.policy={.mode=CONTROL_MODE_MANUAL},
        .actuator={.ready_mask=7,.relay_on=1,.fan_level=4,.fan_duty_permille=1000}};
    LoraStatus_t link={.ready=1,.tx_done_count=UINT32_MAX,.tx_error_count=UINT32_MAX,.init_error_count=UINT32_MAX};
    TerminalRows_t rows;
    for(uint8_t page=0;page<TERMINAL_PAGE_COUNT;++page)
    {
        TerminalView_Format(page,&sensor,&control,&link,0,&rows);
        for(unsigned int i=0;i<4;++i)CHECK(strlen(rows.row[i])<=16);
        if(page==0)CHECK(strstr(rows.row[0],"-0.1")!=0 && strstr(rows.row[0],"100.0")!=0);
        if(page==1)CHECK(strstr(rows.row[2],"100%")!=0);
        if(page==2)CHECK(strstr(rows.row[1],"Out:1")!=0);
        if(page==3)CHECK(strcmp(rows.row[3],"Cmd:0")==0);
    }
    TerminalView_Format(0,&sensor,&control,&link,9000,&rows);
    CHECK(strstr(rows.row[0],"--.-")!=0 && strstr(rows.row[1],"--")!=0);
}

static void Run(const char *name,void (*test)(void))
{
    const unsigned int before=failures; ++cases; test();
    if(before==failures)printf("PASS %s\n",name);
}
int main(void)
{
    Run("startup_config",StartupAndConfig); Run("fan_hysteresis",FanHysteresis);
    Run("pump_timeout_cooldown",PumpTimeoutAndCooldown); Run("irrigation_opt_in",AutoIrrigationOptIn);
    Run("manual_lease_arguments",ManualLeaseAndArguments); Run("emergency_mute_fault",EmergencyMuteAndFault);
    Run("time_wrap",TickWrap); Run("button_debounce_longpress",Buttons);
    Run("co2_parser_resync",Parser); Run("telemetry_crc_boundaries",Telemetry); Run("oled_line_boundaries",Views);
    printf("%u cases; %u assertions failed\n",cases,failures);
    return failures ? 1 : 0;
}
