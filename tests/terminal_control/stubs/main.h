#ifndef TERMINAL_TEST_MAIN_H
#define TERMINAL_TEST_MAIN_H
#include <stdint.h>

typedef enum { HAL_OK=0, HAL_ERROR=1 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET=0, GPIO_PIN_SET=1 } GPIO_PinState;
typedef struct { uint32_t output, input; } GPIO_TypeDef;
extern GPIO_TypeDef mock_gpiob, mock_gpioc;
#define GPIOB (&mock_gpiob)
#define GPIOC (&mock_gpioc)
#define RELAY_GPIO_Port GPIOC
#define RELAY_Pin (1U << 14U)
#define LED3_GPIO_Port GPIOB
#define LED3_Pin (1U << 15U)
#define SW1_GPIO_Port GPIOB
#define SW2_GPIO_Port GPIOB
#define SW1_Pin (1U << 12U)
#define SW2_Pin (1U << 13U)
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
#endif
