/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.h
  * @author  ST67 Application Team
  * @brief   Header for main.c file.
  *          This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025-2026 STMicroelectronics.
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
#include "stm32u5xx_hal.h"

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
#define XLIB_Pin GPIO_PIN_6
#define XLIB_GPIO_Port GPIOE
#define XLIB_EXTI_IRQn EXTI6_IRQn
#define Horizontal_ENABLE_Pin GPIO_PIN_3
#define Horizontal_ENABLE_GPIO_Port GPIOA
#define SPI_CLK_Pin GPIO_PIN_5
#define SPI_CLK_GPIO_Port GPIOA
#define SPI_MISO_Pin GPIO_PIN_6
#define SPI_MISO_GPIO_Port GPIOA
#define SPI_MOSI_Pin GPIO_PIN_7
#define SPI_MOSI_GPIO_Port GPIOA
#define XLI_Pin GPIO_PIN_1
#define XLI_GPIO_Port GPIOB
#define XLI_EXTI_IRQn EXTI1_IRQn
#define XLE_Pin GPIO_PIN_2
#define XLE_GPIO_Port GPIOB
#define XLE_EXTI_IRQn EXTI2_IRQn
#define Vertical_ENABLE_Pin GPIO_PIN_11
#define Vertical_ENABLE_GPIO_Port GPIOF
#define XLEB_Pin GPIO_PIN_0
#define XLEB_GPIO_Port GPIOG
#define XLEB_EXTI_IRQn EXTI0_IRQn
#define ZLI_Pin GPIO_PIN_7
#define ZLI_GPIO_Port GPIOE
#define ZLI_EXTI_IRQn EXTI7_IRQn
#define BOOT_Pin GPIO_PIN_9
#define BOOT_GPIO_Port GPIOE
#define ZL_A_DIR_Pin GPIO_PIN_10
#define ZL_A_DIR_GPIO_Port GPIOE
#define CHIP_EN_Pin GPIO_PIN_11
#define CHIP_EN_GPIO_Port GPIOE
#define SPI_RDY_Pin GPIO_PIN_13
#define SPI_RDY_GPIO_Port GPIOE
#define SPI_RDY_EXTI_IRQn EXTI13_IRQn
#define ZL_Pin GPIO_PIN_14
#define ZL_GPIO_Port GPIOB
#define ZL_EXTI_IRQn EXTI14_IRQn
#define ZR_Pin GPIO_PIN_15
#define ZR_GPIO_Port GPIOB
#define ZR_EXTI_IRQn EXTI15_IRQn
#define MXLE_Y_DIR_Pin GPIO_PIN_10
#define MXLE_Y_DIR_GPIO_Port GPIOD
#define ZL_A_STEP_Pin GPIO_PIN_11
#define ZL_A_STEP_GPIO_Port GPIOD
#define ZR_Z_DIR_Pin GPIO_PIN_12
#define ZR_Z_DIR_GPIO_Port GPIOD
#define ZR_Z_STEP_Pin GPIO_PIN_13
#define ZR_Z_STEP_GPIO_Port GPIOD
#define SPI_CS_Pin GPIO_PIN_14
#define SPI_CS_GPIO_Port GPIOD
#define LED_RED_Pin GPIO_PIN_2
#define LED_RED_GPIO_Port GPIOG
#define MXRE_A_DIR_Pin GPIO_PIN_4
#define MXRE_A_DIR_GPIO_Port GPIOG
#define MXRI_Z_STEP_Pin GPIO_PIN_5
#define MXRI_Z_STEP_GPIO_Port GPIOG
#define MXRE_A_STEP_Pin GPIO_PIN_6
#define MXRE_A_STEP_GPIO_Port GPIOG
#define MXRI_Z_DIR_Pin GPIO_PIN_7
#define MXRI_Z_DIR_GPIO_Port GPIOG
#define MXLE_Y_STEP_Pin GPIO_PIN_8
#define MXLE_Y_STEP_GPIO_Port GPIOG
#define LED_GREEN_Pin GPIO_PIN_7
#define LED_GREEN_GPIO_Port GPIOC
#define USART1_TX_Pin GPIO_PIN_9
#define USART1_TX_GPIO_Port GPIOA
#define USART1_RX_Pin GPIO_PIN_10
#define USART1_RX_GPIO_Port GPIOA
#define XREB_Pin GPIO_PIN_9
#define XREB_GPIO_Port GPIOG
#define XREB_EXTI_IRQn EXTI9_IRQn
#define XRIB_Pin GPIO_PIN_10
#define XRIB_GPIO_Port GPIOG
#define XRIB_EXTI_IRQn EXTI10_IRQn
#define GPS_enable_Pin GPIO_PIN_12
#define GPS_enable_GPIO_Port GPIOG
#define MXLI_X_DIR_Pin GPIO_PIN_14
#define MXLI_X_DIR_GPIO_Port GPIOG
#define XRE_Pin GPIO_PIN_4
#define XRE_GPIO_Port GPIOB
#define XRE_EXTI_IRQn EXTI4_IRQn
#define XRI_Pin GPIO_PIN_5
#define XRI_GPIO_Port GPIOB
#define XRI_EXTI_IRQn EXTI5_IRQn
#define LED_BLUE_Pin GPIO_PIN_7
#define LED_BLUE_GPIO_Port GPIOB
#define ZRI_Pin GPIO_PIN_8
#define ZRI_GPIO_Port GPIOB
#define ZRI_EXTI_IRQn EXTI8_IRQn
#define MXLI_X_STEP_Pin GPIO_PIN_0
#define MXLI_X_STEP_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#ifndef SPI_RDY_Pin
#define SPI_RDY_Pin GPIO_PIN_13
#define SPI_RDY_GPIO_Port GPIOE
#endif

#ifndef SPI_RDY_EXTI_IRQn
#define SPI_RDY_EXTI_IRQn EXTI13_IRQn
#endif

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
