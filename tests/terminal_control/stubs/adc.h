#ifndef TERMINAL_TEST_ADC_H
#define TERMINAL_TEST_ADC_H
#include "stm32f1xx_hal.h"
typedef struct { uint8_t placeholder; } ADC_HandleTypeDef;
extern ADC_HandleTypeDef hadc1;
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc,uint32_t *data,uint32_t count);
#endif
