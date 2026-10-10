#include "actuator_service.h"
#include "motor_control.h"
#include "tim.h"
#include <stdio.h>

TIM_TypeDef mock_tim4;
TIM_HandleTypeDef htim4;
GPIO_TypeDef mock_gpiob,mock_gpioc;
static unsigned int failures,start3,start4,nonzero_start;
static uint32_t active3,active4;
static uint8_t buzzer_fails=1;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)

void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state)
{
    if(state==GPIO_PIN_SET)port->output|=pin;else port->output&=~(uint32_t)pin;
}
void Mock_SetCompare(TIM_HandleTypeDef *timer,uint32_t channel,uint32_t value)
{
    if(channel==TIM_CHANNEL_3)
    {
        timer->Instance->CCR3=value;
        if(!(timer->Instance->CCMR2&TIM_CCMR2_OC3PE))active3=value;
    }
    else
    {
        timer->Instance->CCR4=value;
        if(!(timer->Instance->CCMR2&TIM_CCMR2_OC4PE))active4=value;
    }
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer,uint32_t channel)
{
    if(channel==TIM_CHANNEL_3)
    {
        ++start3;
        if(active3 || timer->Instance->CCR3)++nonzero_start;
        timer->Instance->CCER|=TIM_CCER_CC3E;
    }
    else
    {
        ++start4;
        if(active4 || timer->Instance->CCR4)++nonzero_start;
        timer->Instance->CCER|=TIM_CCER_CC4E;
    }
    timer->Instance->CR1|=TIM_CR1_CEN;
    return channel==TIM_CHANNEL_4 && buzzer_fails ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer,uint32_t channel)
{
    timer->Instance->CCER&=~(channel==TIM_CHANNEL_3 ? TIM_CCER_CC3E : TIM_CCER_CC4E);
    if(!timer->Instance->CCER)timer->Instance->CR1&=~TIM_CR1_CEN;
    return HAL_OK;
}
static void Update(void){active3=mock_tim4.CCR3;active4=mock_tim4.CCR4;}
int main(void)
{
    ActuatorState_t state;
    htim4.Instance=TIM4;
    mock_tim4.ARR=3599;
    mock_tim4.CCMR2=TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE;
    Actuator_ServiceInit();
    Actuator_GetState(&state);
    CHECK(state.ready_mask==(ACTUATOR_READY_MOTOR|ACTUATOR_READY_RELAY));
    CHECK(!Actuator_ServiceApply(1,4,1));
    CHECK(!active3 && !active4 && !(mock_gpioc.output&RELAY_Pin));
    buzzer_fails=0;
    Actuator_ServiceInit(); Actuator_GetState(&state);
    CHECK(state.ready_mask==ACTUATOR_READY_ALL && !nonzero_start);
    CHECK(Actuator_ServiceApply(1,4,1)); Update(); Actuator_GetState(&state);
    CHECK(active3==3600 && active4==3600 && (mock_gpioc.output&RELAY_Pin));
    CHECK(state.relay_on && state.fan_level==4 && state.fan_duty_permille==1000 && state.buzzer_on);
    CHECK(Actuator_ServiceApply(0,0,1));
    CHECK(active3==0 && mock_tim4.CCR4==3600 && !(mock_gpioc.output&RELAY_Pin));
    CHECK(Actuator_ServiceApply(0,4,0)); Update();
    CHECK(active3==3600 && active4==0);
    CHECK((mock_tim4.CCMR2&(TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE))==(TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE));
    CHECK(!Actuator_ServiceApply(1,5,1));
    CHECK(!active3 && !active4 && !(mock_gpioc.output&RELAY_Pin));
    CHECK(Actuator_ServiceApply(1,3,1)); Update();
    const unsigned int starts=start3+start4;
    Actuator_ServiceInit();
    CHECK(start3+start4==starts && !active3 && !active4);
    Actuator_GetState(&state);
    CHECK(state.ready_mask==7 && !state.relay_on && !state.fan_level && !state.buzzer_on);
    printf("actuator shared-channel tests: %u assertions failed\n",failures);
    return failures ? 1:0;
}
