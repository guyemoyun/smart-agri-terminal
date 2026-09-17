#include "PWM.h"

void LED_PWM(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);         // 启动定时器1的PWM输出
    __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_1, 300); // 设置定时器1，CCR1为300，这时PWM占空比30%

}
