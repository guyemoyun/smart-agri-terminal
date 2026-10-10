#include "actuator_service.h"
#include "motor_control.h"
#include "main.h"
#include "tim.h"
#include <string.h>

static ActuatorState_t actual;

static void BuzzerOff(void)
{
    const uint32_t preload = htim4.Instance->CCMR2 & TIM_CCMR2_OC4PE;
    __HAL_TIM_DISABLE_OCxPRELOAD(&htim4, TIM_CHANNEL_4);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0U);
    if (preload != 0U) __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, TIM_CHANNEL_4);
}

void Actuator_AllSafeOff(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);
    Motor_Stop();
    if (htim4.Instance == TIM4) BuzzerOff();
    actual.relay_on = 0U;
    actual.fan_level = 0U;
    actual.fan_duty_permille = 0U;
    actual.buzzer_on = 0U;
}

void Actuator_ServiceInit(void)
{
    if (actual.ready_mask == ACTUATOR_READY_ALL)
    {
        Actuator_AllSafeOff();
        return;
    }
    memset(&actual, 0, sizeof(actual));
    Actuator_AllSafeOff();
    actual.ready_mask = ACTUATOR_READY_RELAY;
    if (Motor_Init() == MOTOR_RESULT_OK) actual.ready_mask |= ACTUATOR_READY_MOTOR;
    if (htim4.Instance == TIM4 &&
        HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4) == HAL_OK)
        actual.ready_mask |= ACTUATOR_READY_BUZZER;
    else if (htim4.Instance == TIM4)
        (void)HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_4);
    Actuator_AllSafeOff();
}

uint8_t Actuator_ServiceApply(uint8_t relay_on, uint8_t fan_level, uint8_t buzzer_on)
{
    if (relay_on > 1U || fan_level > 4U || buzzer_on > 1U)
    {
        Actuator_AllSafeOff();
        return 0U;
    }
    if (actual.ready_mask != ACTUATOR_READY_ALL)
    {
        Actuator_AllSafeOff();
        return 0U;
    }
    /* 电机命令先校验成功，不能在风扇控制失败后继续启动水泵。 */
    if (Motor_SetLevel(fan_level) != MOTOR_RESULT_OK)
    {
        actual.ready_mask &= (uint8_t)~ACTUATOR_READY_MOTOR;
        Actuator_AllSafeOff();
        return 0U;
    }
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, relay_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (buzzer_on != 0U)
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, __HAL_TIM_GET_AUTORELOAD(&htim4) + 1U);
    else BuzzerOff();
    actual.relay_on = relay_on;
    actual.fan_level = Motor_GetLevel();
    actual.fan_duty_permille = Motor_GetDutyPermille();
    actual.buzzer_on = buzzer_on;
    return 1U;
}

void Actuator_GetState(ActuatorState_t *state)
{
    if (state != 0) *state = actual;
}
