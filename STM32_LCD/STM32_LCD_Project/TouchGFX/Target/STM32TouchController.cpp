/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.cpp
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.23.1. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */

#include <STM32TouchController.hpp>
#include "main.h"

volatile bool newTouch = false;
extern "C" I2C_HandleTypeDef hi2c2;

void STM32TouchController::init()
{
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
    if (newTouch)
    {
        HAL_StatusTypeDef status;
        uint8_t rx_buf[6];

        newTouch = false;

        /* read x/y coordinates */
        status = HAL_I2C_Master_Receive(&hi2c2, 0x83, rx_buf, sizeof(rx_buf), 10);
        if (status == HAL_OK)
        {
            y = (rx_buf[3] << 8) | rx_buf[2];
            x = 320 - ((rx_buf[5] << 8) | rx_buf[4]);
            return true;
        }
    }
    return false;
}

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
