#ifndef MOTOR_TEST_TIM_H
#define MOTOR_TEST_TIM_H

#include <stdint.h>

/* 仅用于主机单元测试的 TIM4/HAL 模型，不访问真实寄存器或硬件。 */
typedef struct
{
    uint32_t ARR;
    uint32_t CCR3;
    uint32_t CCR4;
    uint32_t CCMR2;
    uint32_t CCER;
    uint32_t CR1;
} TIM_TypeDef;

typedef struct
{
    TIM_TypeDef *Instance;
} TIM_HandleTypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1
} HAL_StatusTypeDef;

extern TIM_TypeDef mock_tim4;
extern TIM_HandleTypeDef htim4;
#define TIM4 (&mock_tim4)
#define TIM_CHANNEL_3 0x00000008U
#define TIM_CHANNEL_4 0x0000000CU
#define TIM_CCMR2_OC3PE (1U << 3U)
#define TIM_CCMR2_OC4PE (1U << 11U)
#define TIM_CCER_CC3E (1U << 8U)
#define TIM_CCER_CC4E (1U << 12U)
#define TIM_CR1_CEN 1U

void Mock_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel);

#define __HAL_TIM_GET_AUTORELOAD(timer) ((timer)->Instance->ARR)
#define __HAL_TIM_SET_COMPARE(timer, channel, value) \
    Mock_SetCompare((timer), (channel), (value))
#define __HAL_TIM_DISABLE_OCxPRELOAD(timer, channel) \
    ((timer)->Instance->CCMR2 &= ~TIM_CCMR2_OC3PE)
#define __HAL_TIM_ENABLE_OCxPRELOAD(timer, channel) \
    ((timer)->Instance->CCMR2 |= TIM_CCMR2_OC3PE)

#endif /* MOTOR_TEST_TIM_H */
