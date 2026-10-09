#include "motor_control.h"
#include "main.h"
#include "tim.h"

#define MOTOR_PWM_CHANNEL TIM_CHANNEL_3

/* 初始开环档位。当前数值为保守的临时值，
 * 后续需要结合实际电机、负载和供电情况进行校准。 */
static const uint16_t motor_level_duty_permille[MOTOR_LEVEL_COUNT] =
{
    0U,
    300U,
    400U,
    500U,
    MOTOR_PWM_MAX_DUTY_PERMILLE
};

static uint16_t motor_duty_permille;
static uint8_t motor_level;
static uint8_t motor_started;

static uint32_t Motor_DutyToCompare(uint16_t duty_permille)
{
    const uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim4);
    return ((period + 1U) * duty_permille) / 1000U;
}

void Motor_Init(void)
{
    motor_duty_permille = 0U;
    motor_level = 0U;

    /* 以 CCR=0 启动 PWM 通道，确保 PB8 初始不输出有效驱动。 */
    if (HAL_TIM_PWM_Start(&htim4, MOTOR_PWM_CHANNEL) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_TIM_SET_COMPARE(&htim4, MOTOR_PWM_CHANNEL, 0U);
    motor_started = 1U;
}

void Motor_Stop(void)
{
    motor_duty_permille = 0U;
    motor_level = 0U;

    if (motor_started != 0U)
    {
        __HAL_TIM_SET_COMPARE(&htim4, MOTOR_PWM_CHANNEL, 0U);
    }
    else
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    }
}

void Motor_SetDutyPermille(uint16_t duty_permille)
{
    if (duty_permille > MOTOR_PWM_MAX_DUTY_PERMILLE)
    {
        duty_permille = MOTOR_PWM_MAX_DUTY_PERMILLE;
    }

    motor_duty_permille = duty_permille;
    /* 直接设置占空比不属于预设档位，用 0xFF 表示当前不是标准档位。 */
    motor_level = 0xFFU;

    if (motor_started == 0U)
    {
        return;
    }

    __HAL_TIM_SET_COMPARE(&htim4, MOTOR_PWM_CHANNEL,
                         Motor_DutyToCompare(motor_duty_permille));
}

void Motor_SetLevel(uint8_t level)
{
    if (level >= MOTOR_LEVEL_COUNT)
    {
        level = (uint8_t)(MOTOR_LEVEL_COUNT - 1U);
    }

    motor_level = level;
    motor_duty_permille = motor_level_duty_permille[level];

    if (motor_started != 0U)
    {
        __HAL_TIM_SET_COMPARE(&htim4, MOTOR_PWM_CHANNEL,
                             Motor_DutyToCompare(motor_duty_permille));
    }
}

uint16_t Motor_GetDutyPermille(void)
{
    return motor_duty_permille;
}

uint8_t Motor_GetLevel(void)
{
    return motor_level;
}
