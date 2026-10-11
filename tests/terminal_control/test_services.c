#include "control_service.h"
#include "terminal_ui.h"
#include "terminal_view.h"
#include "button_service.h"
#include "motor_control.h"
#include "terminal_commands.h"
#include "tim.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>

TIM_TypeDef mock_tim4;
TIM_HandleTypeDef htim4;
GPIO_TypeDef mock_gpiob,mock_gpioc;
static uint32_t tick;
static unsigned int failures,refreshes;
static int16_t temperature_x10=250;
static char shown[4][17];
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)

uint32_t HAL_GetTick(void){return tick;}
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state)
{if(state==GPIO_PIN_SET)port->output|=pin;else port->output&=~(uint32_t)pin;}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin)
{return port->input&pin ? GPIO_PIN_SET : GPIO_PIN_RESET;}
void Mock_SetCompare(TIM_HandleTypeDef *timer,uint32_t channel,uint32_t value)
{if(channel==TIM_CHANNEL_3)timer->Instance->CCR3=value;else timer->Instance->CCR4=value;}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer,uint32_t channel)
{timer->Instance->CCER|=channel==TIM_CHANNEL_3 ? TIM_CCER_CC3E : TIM_CCER_CC4E;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer,uint32_t channel)
{timer->Instance->CCER&=~(channel==TIM_CHANNEL_3 ? TIM_CCER_CC3E : TIM_CCER_CC4E);return HAL_OK;}
void Sensor_GetSnapshot(SensorData_t *sensor)
{
    *sensor=(SensorData_t){.temperature=(float)temperature_x10/10.0f,.humidity=60.0f,
        .co2_ppm=800,.soil_level=2,.light_lux=200,.light_valid=1,.soil_valid=1,.dht22_valid=1,
        .co2_valid=1,.adc_update_tick=tick,.dht22_update_tick=tick,.co2_update_tick=tick};
}
void Lora_GetStatus(LoraStatus_t *link){memset(link,0,sizeof(*link));}
void OLED_Fill_Area(uint8_t x0,uint8_t y0,uint8_t x1,uint8_t y1,uint8_t fill)
{CHECK(x0==0 && y0%16==0 && x1==127 && y1==y0+15 && fill==0);}
void OLED_ShowString(uint8_t x,uint8_t y,const char *text,uint8_t font)
{CHECK(x==0 && y<64 && y%16==0 && font==16 && strlen(text)<=16);strcpy(shown[y/16],text);}
void OLED_RefreshPages(uint8_t first,uint8_t last)
{CHECK(first<8 && first%2==0 && last==first+1);++refreshes;}

static void Step(uint32_t now)
{tick=now;Terminal_UITask(now);Control_ServiceTask(now);}
static void Press(uint16_t pin,uint32_t now,uint32_t duration)
{
    mock_gpiob.input&=~(uint32_t)pin;Button_NotifyIRQ(pin);Step(now);Step(now+20);
    Step(now+duration);
    mock_gpiob.input|=pin;
    /* 释放沿没有 EXTI 通知，先等待下一次 10ms 采样，再满足 20ms 消抖。 */
    Step(now+duration+11);Step(now+duration+31);
}

int main(void)
{
    ControlSnapshot_t state;
    htim4.Instance=TIM4;mock_tim4.ARR=3599;mock_tim4.CCMR2=TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE;
    mock_gpiob.input=SW1_Pin|SW2_Pin;
    Control_ServiceInit(0);Terminal_UIInit(0);Step(0);
    Control_GetSnapshot(&state);
    CHECK(state.policy.mode==CONTROL_MODE_MANUAL && !state.actuator.relay_on && !state.actuator.fan_level);
    CHECK(refreshes==4 && strstr(shown[0],"T:")!=0);
    Step(1);CHECK(refreshes==4); /* 未变化页面不重复刷新。 */
    Press(SW1_Pin,100,100);CHECK(Terminal_UIGetPage()==TERMINAL_PAGE_FAN);
    Press(SW2_Pin,400,100);Control_GetSnapshot(&state);
    CHECK(state.actuator.fan_level==1 && state.actuator.fan_duty_permille==300);
    CHECK(strstr(shown[2],"30%")!=0);
    Press(SW1_Pin,700,100);CHECK(Terminal_UIGetPage()==TERMINAL_PAGE_PUMP);
    Press(SW2_Pin,1000,100);Control_GetSnapshot(&state);CHECK(state.actuator.relay_on);
    Press(SW2_Pin,1400,1600);Control_GetSnapshot(&state);
    CHECK(state.policy.mode==CONTROL_MODE_SAFE && !state.actuator.relay_on && !state.actuator.fan_level);
    CHECK(Terminal_UIGetPage()==TERMINAL_PAGE_FAULT && (state.policy.alarm_mask&ALARM_EMERGENCY));
    Press(SW1_Pin,3500,1600);Control_GetSnapshot(&state);CHECK(state.policy.mode==CONTROL_MODE_MANUAL);
    Press(SW1_Pin,5500,1600);Control_GetSnapshot(&state);CHECK(state.policy.mode==CONTROL_MODE_AUTO);
    temperature_x10=350;tick=7500;Control_ServiceTask(tick);Control_GetSnapshot(&state);
    CHECK(state.actuator.fan_level==2 && !state.actuator.relay_on && !state.auto_irrigation_enabled);
    /* 两键同轮长按，紧急停止必须压过 AUTO/MANUAL 切换。 */
    mock_gpiob.input=0;Button_NotifyIRQ(SW1_Pin);Step(8000);Step(8020);Step(9520);
    Control_GetSnapshot(&state);CHECK(state.policy.mode==CONTROL_MODE_SAFE && !state.actuator.fan_level);
    mock_gpiob.input=SW1_Pin|SW2_Pin;Step(9531);Step(9551);
    Control_GetSnapshot(&state);CHECK(state.policy.mode==CONTROL_MODE_SAFE);
    CHECK(Control_SetFan(4,tick)==CONTROL_WRONG_MODE);
    ControlConfig_t invalid;ControlPolicy_DefaultConfig(&invalid);invalid.pump_max_ms=0;
    CHECK(Control_SetConfig(&invalid,tick)==CONTROL_INVALID_ARGUMENT);
    temperature_x10=250;tick=80000;
    CHECK(Control_SetMode(CONTROL_MODE_MANUAL,tick)==CONTROL_OK);
    CHECK(Control_SetPump(1,tick)==CONTROL_OK);
    tick=90000;
    CHECK(Control_SetPump(1,tick)==CONTROL_WRONG_MODE);
    Control_GetSnapshot(&state);
    CHECK(state.policy.mode==CONTROL_MODE_SAFE && !state.actuator.relay_on &&
          (state.policy.alarm_mask&ALARM_PUMP_TIMEOUT));
    TerminalCommands_t remote;
    TerminalCommands_Init(&remote,1,0,0x12345678U);
    LoraFrame_t command={.kind=LORA_KIND_COMMAND,.flags=LORA_FLAG_RESULT,.source=0,.target=1,
        .sequence=1,.session=0x12345678U,.transaction=1,.payload_length=LORA_COMMAND_SIZE},response;
    command.payload[0]=LORA_COMMAND_MODE;command.payload[1]=CONTROL_MODE_MANUAL;
    Lora_Write32(&command.payload[2],tick+10000U);
    CHECK(TerminalCommands_Process(&remote,&command,TerminalCommands_ControlExecutor(),tick,&response)==TERMINAL_COMMAND_NEW);
    CHECK(response.payload[2]==LORA_RESULT_SAFETY_LOCK);
    Control_GetSnapshot(&state);CHECK(state.policy.mode==CONTROL_MODE_SAFE);
    CHECK(Control_SetMode(CONTROL_MODE_MANUAL,tick)==CONTROL_OK); /* 本地明确解锁。 */
    command.transaction=2;command.sequence=2;command.payload[0]=LORA_COMMAND_FAN;command.payload[1]=4;
    CHECK(TerminalCommands_Process(&remote,&command,TerminalCommands_ControlExecutor(),tick,&response)==TERMINAL_COMMAND_NEW);
    CHECK(response.payload[2]==CONTROL_OK && response.payload[5]==4 && Lora_Read16(&response.payload[8])==1000);
    Control_GetSnapshot(&state);CHECK(state.actuator.fan_level==4);
    command.transaction=3;command.sequence=3;command.payload[0]=LORA_COMMAND_STOP;command.payload[1]=0;
    CHECK(TerminalCommands_Process(&remote,&command,TerminalCommands_ControlExecutor(),tick,&response)==TERMINAL_COMMAND_NEW);
    CHECK(response.payload[2]==CONTROL_OK && response.payload[3]==CONTROL_MODE_SAFE && response.payload[5]==0);
    command.transaction=4;command.sequence=4;command.payload[0]=LORA_COMMAND_MUTE;command.payload[1]=1;
    CHECK(TerminalCommands_Process(&remote,&command,TerminalCommands_ControlExecutor(),tick,&response)==TERMINAL_COMMAND_NEW);
    CHECK(response.payload[12]&1U);
    command.transaction=5;command.sequence=5;command.payload[0]=LORA_COMMAND_PUMP;command.payload[1]=1;
    CHECK(TerminalCommands_Process(&remote,&command,TerminalCommands_ControlExecutor(),tick,&response)==TERMINAL_COMMAND_NEW);
    CHECK(response.payload[2]==CONTROL_WRONG_MODE && response.payload[4]==0);
    printf("local UI/control integration: %u assertions failed, %u row refreshes\n",failures,refreshes);
    return failures ? 1:0;
}
