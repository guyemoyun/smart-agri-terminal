#ifndef TERMINAL_TEST_HAL_H
#define TERMINAL_TEST_HAL_H
#include "main.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct { uint8_t placeholder; } SPI_HandleTypeDef;
extern GPIO_TypeDef mock_gpioa;
#define GPIOA (&mock_gpioa)
#define GPIO_PIN_0 1U
#define GPIO_PIN_1 2U
#define GPIO_PIN_4 16U
#define GPIO_PIN_10 1024U
#endif
