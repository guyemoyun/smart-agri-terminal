#include "motor_control.h"
#include "tim.h"

#define MOTOR_PWM_CHANNEL TIM_CHANNEL_3

/* 按用户确认的使用工况设置 0%、30%、50%、80%、100% 五个档位。 */
static const uint16_t motor_level_duty_permille[MOTOR_LEVEL_COUNT] =
{
    0U,
    300U,
    500U,
    800U,
    MOTOR_PWM_MAX_DUTY_PERMILLE
};

static uint16_t motor_duty_permille;
static uint8_t motor_level;
static uint8_t motor_started;

static uint32_t Motor_DutyToCompare(uint16_t duty_permille)
{
    const uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim4);
    /* 当前 ARR=3599；100% 时 CCR=3600，PWM1 模式整个周期保持有效。 */
    return ((period + 1U) * duty_permille) / 1000U;
}

static uint8_t Motor_FindLevel(uint16_t duty_permille)
{
    uint8_t level;

    for (level = 0U; level < MOTOR_LEVEL_COUNT; ++level)
    {
        if (motor_level_duty_permille[level] == duty_permille)
        {
            return level;
        }
    }

    return MOTOR_LEVEL_CUSTOM;
}

static void Motor_ClearCompare(void)
{
    const uint32_t preload = htim4.Instance->CCMR2 & TIM_CCMR2_OC3PE;

    /*
     * 只临时关闭 CH3 比较寄存器的预装载，让停止指令直接写入有效 CCR。
     * 不产生全局更新事件，不改 ARR/PSC，也不影响 CH4 的比较值和配置。
     */
    __HAL_TIM_DISABLE_OCxPRELOAD(&htim4, MOTOR_PWM_CHANNEL);
    __HAL_TIM_SET_COMPARE(&htim4, MOTOR_PWM_CHANNEL, 0U);
    if (preload != 0U)
    {
        __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, MOTOR_PWM_CHANNEL);
    }
}

MotorResult_t Motor_Init(void)
{
    /* 未生成或未执行 TIM4 初始化时，不访问无效的定时器地址。 */
    if (htim4.Instance != TIM4)
    {
        motor_started = 0U;
        motor_duty_permille = 0U;
        motor_level = 0U;
        return MOTOR_RESULT_NOT_READY;
    }

    /* 先将有效 CCR 归零，再允许输出；不会恢复先前的非零指令。 */
    Motor_Stop();
    if (motor_started != 0U)
    {
        return MOTOR_RESULT_OK;
    }

    if (HAL_TIM_PWM_Start(&htim4, MOTOR_PWM_CHANNEL) != HAL_OK)
    {
        /* 清理 CH3 的使能和 HAL 通道状态，保留已经运行的 CH4。 */
        (void)HAL_TIM_PWM_Stop(&htim4, MOTOR_PWM_CHANNEL);
        Motor_Stop();
        return MOTOR_RESULT_PWM_ERROR;
    }

    motor_started = 1U;
    return MOTOR_RESULT_OK;
}

void Motor_Stop(void)
{
    if (htim4.Instance == TIM4)
    {
        Motor_ClearCompare();
    }

    motor_duty_permille = 0U;
    motor_level = 0U;
}

MotorResult_t Motor_SetDutyPermille(uint16_t duty_permille)
{
    if (duty_permille > MOTOR_PWM_MAX_DUTY_PERMILLE)
    {
        return MOTOR_RESULT_INVALID_ARGUMENT;
    }

    if (motor_started == 0U)
    {
        return MOTOR_RESULT_NOT_READY;
    }

    if (duty_permille == 0U)
    {
        Motor_Stop();
        return MOTOR_RESULT_OK;
    }

    __HAL_TIM_SET_COMPARE(&htim4, MOTOR_PWM_CHANNEL,
                         Motor_DutyToCompare(duty_permille));

    /* 仅在已启动通道并写入指令后更新状态，未就绪时不缓存启动请求。 */
    motor_duty_permille = duty_permille;
    motor_level = Motor_FindLevel(duty_permille);
    return MOTOR_RESULT_OK;
}

MotorResult_t Motor_SetLevel(uint8_t level)
{
    if (level >= MOTOR_LEVEL_COUNT)
    {
        return MOTOR_RESULT_INVALID_ARGUMENT;
    }

    return Motor_SetDutyPermille(motor_level_duty_permille[level]);
}

uint8_t Motor_IsReady(void)
{
    return motor_started;
}

uint16_t Motor_GetDutyPermille(void)
{
    return motor_duty_permille;
}

uint8_t Motor_GetLevel(void)
{
    return motor_level;
}
