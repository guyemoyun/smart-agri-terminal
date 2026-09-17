/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC
#define DHT22_Pin GPIO_PIN_15
#define DHT22_GPIO_Port GPIOC
#define CO2_UART2_TX_Pin GPIO_PIN_2
#define CO2_UART2_TX_GPIO_Port GPIOA
#define CO2_UART2_RX_Pin GPIO_PIN_3
#define CO2_UART2_RX_GPIO_Port GPIOA
#define SW1_Pin GPIO_PIN_12
#define SW1_GPIO_Port GPIOB
#define SW1_EXTI_IRQn EXTI15_10_IRQn
#define SW2_Pin GPIO_PIN_13
#define SW2_GPIO_Port GPIOB
#define SW2_EXTI_IRQn EXTI15_10_IRQn
#define LED3_Pin GPIO_PIN_15
#define LED3_GPIO_Port GPIOB
#define LED_PWM_Pin GPIO_PIN_8
#define LED_PWM_GPIO_Port GPIOA
#define Debug_UART1_TX_Pin GPIO_PIN_9
#define Debug_UART1_TX_GPIO_Port GPIOA
#define Debug_UART1_RX_Pin GPIO_PIN_10
#define Debug_UART1_RX_GPIO_Port GPIOA
#define OLED_I2C_SCK_Pin GPIO_PIN_6
#define OLED_I2C_SCK_GPIO_Port GPIOB
#define OLED_I2C_SDA_Pin GPIO_PIN_7
#define OLED_I2C_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
