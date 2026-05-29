/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : TouchGFXHAL.cpp
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.22.0. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#include <TouchGFXHAL.hpp>

/* USER CODE BEGIN TouchGFXHAL.cpp */
#include <touchgfx/hal/GPIO.hpp>
#include <touchgfx/hal/OSWrappers.hpp>
#include <KeySampler.hpp>
#include "main.h"
#include "FreeRTOS.h"

using namespace touchgfx;

extern "C" DMA_HandleTypeDef handle_GPDMA1_Channel13;
static DMA_HandleTypeDef* const DISPLAY_DMA = &handle_GPDMA1_Channel13;

KeySampler btnctrl;

static const int MAX_BLOCK_HEIGHT = 20; // Specify the maximum number of display lines transfered per block. This must respect the DMA maximum transfer size

static volatile int nextSendToDisplayLine = 0;
static volatile int maxSendToDisplayLine = 0;
static volatile bool refreshRequested = false;
static volatile int refreshMinLine = 0;
static volatile int refreshMaxLine = 0;
static volatile int maxDrawLine = DISPLAY_HEIGHT;
static uint16_t* framebuffer = 0;

void initLCD(void);
static void DMA_TxCpltCallback(DMA_HandleTypeDef* hdma);
static void DMA_TxErrorCallback(DMA_HandleTypeDef* hdma);
void setDisplayWindow(uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height);
static int32_t LCD_IO_SendDataDMA(uint8_t* pData, uint32_t Length);
void sendNextFrameBufferBlockToDisplay();

void TouchGFXHAL::initialize()
{
    initLCD();
    setButtonController(&btnctrl);

    HAL_DMA_RegisterCallback(DISPLAY_DMA, HAL_DMA_XFER_CPLT_CB_ID, DMA_TxCpltCallback);
    HAL_DMA_RegisterCallback(DISPLAY_DMA, HAL_DMA_XFER_ERROR_CB_ID, DMA_TxErrorCallback);

    instrumentation.init();
    setMCUInstrumentation(&instrumentation);
    enableMCULoadCalculation(true);

    registerTaskDelayFunction(&OSWrappers::taskDelay);
    setFrameRefreshStrategy(REFRESH_STRATEGY_OPTIM_SINGLE_BUFFER_TFT_CTRL);

    TouchGFXGeneratedHAL::initialize();

    framebuffer = frameBuffer0; // make the framebuffer pointer available to functions that is not part of TouchGFXHAL
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == LCD_TE_Pin)
    {
        // VSync has occurred, increment TouchGFX engine vsync counter
        HAL::getInstance()->vSync();
        // VSync has occurred, signal TouchGFX engine
        OSWrappers::signalVSync();
        GPIO::set(GPIO::VSYNC_FREQ);

        if (refreshRequested)
        {
            // Initiate a new transfer of the part of the framebuffer that has been updated
            // This assumes that all the blocks of the previous transfer has been completed,
            // since the FMC+DMA is fast enough to transfer the entire frame buffer each frame
            // with the default settings in this TBS
            refreshRequested = false;
            nextSendToDisplayLine = refreshMinLine;
            maxSendToDisplayLine = refreshMaxLine;
            sendNextFrameBufferBlockToDisplay();
        }
    }
}

//This function starts transfer of the requested framebuffer area (block by block) and updates the associated variables
void sendNextFrameBufferBlockToDisplay()
{
    maxDrawLine = nextSendToDisplayLine; // Allow TouchGFX framework render in frame buffer until the last line of the previous transfer before setting up next transfer

    const int32_t remainingLines = maxSendToDisplayLine - nextSendToDisplayLine;
    if (remainingLines > 0)
    {
        // The display transfer is not done. Start transfer of the next block
        const uint32_t sendBlockHeight = MIN(MAX_BLOCK_HEIGHT, remainingLines);
        setDisplayWindow(0, nextSendToDisplayLine, DISPLAY_WIDTH, sendBlockHeight);
        uint16_t* dataPtr = framebuffer + nextSendToDisplayLine * DISPLAY_WIDTH;
        uint32_t dataSize = DISPLAY_WIDTH * sendBlockHeight * 2;
        nextSendToDisplayLine += sendBlockHeight;
        LCD_IO_SendDataDMA((uint8_t*)dataPtr, dataSize);
    }
    else
    {
        // The display transfer is done. Allow drawing to the entire framebuffer
        maxDrawLine = DISPLAY_HEIGHT;
    }
}

bool TouchGFXHAL::beginFrame()
{
    // Initialize the min/max line variables used to accumulate the range that is updated this frame
    // The variables are set such that any potential valid rect will expand the range
    refreshMinLine = DISPLAY_HEIGHT;
    refreshMaxLine = 0;
    return TouchGFXGeneratedHAL::beginFrame();
}

/**
 * This function is called whenever the framework has performed a partial draw.
 *
 * @param rect The area of the screen that has been drawn, expressed in absolute coordinates.
 *
 * @see flushFrameBuffer().
 */
void TouchGFXHAL::flushFrameBuffer(const touchgfx::Rect& rect)
{
    // Calling parent implementation of flushFrameBuffer(const touchgfx::Rect& rect).
    TouchGFXGeneratedHAL::flushFrameBuffer(rect);

    // Determine the boundaries of the current rect, acounting for display orientation
    int rectMin, rectMax;
    if (HAL::getInstance()->DISPLAY_ROTATION == rotate0)
    {
        rectMin = rect.y;
        rectMax = rect.bottom();
    }
    else
    {
        rectMin = DISPLAY_HEIGHT - rect.right();
        rectMax = DISPLAY_HEIGHT - rect.x;
    }

    // Make sure that all rects for the frame are contained within the transmitted range
    refreshMinLine = MIN(rectMin, refreshMinLine);
    refreshMaxLine = MAX(rectMax, refreshMaxLine);

    refreshRequested = true; // Signal that the framebuffer has been updated this tick
}

/**
  * @brief  DMA transfer complete callback
  * @note   This function is executed when the transfer complete interrupt
  *         is generated
  * @retval None
  */
static void DMA_TxCpltCallback(DMA_HandleTypeDef* hdma)
{
    if (hdma == DISPLAY_DMA)
    {
        sendNextFrameBufferBlockToDisplay();
    }
}

// The TouchGFX framework calls this function get the maximum line number that is safe to draw in the framebuffer
uint16_t TouchGFXHAL::getTFTCurrentLine()
{
    return maxDrawLine;
}

/**
 * Gets the frame buffer address used by the TFT controller.
 *
 * @return The address of the frame buffer currently being displayed on the TFT.
 */
uint16_t* TouchGFXHAL::getTFTFrameBuffer() const
{
    // Calling parent implementation of getTFTFrameBuffer().
    //
    // To overwrite the generated implementation, omit call to parent function
    // and implemented needed functionality here.

    return TouchGFXGeneratedHAL::getTFTFrameBuffer();
}

/**
 * Sets the frame buffer address used by the TFT controller.
 *
 * @param [in] address New frame buffer address.
 */
void TouchGFXHAL::setTFTFrameBuffer(uint16_t* address)
{
    // Calling parent implementation of setTFTFrameBuffer(uint16_t* address).
    //
    // To overwrite the generated implementation, omit call to parent function
    // and implemented needed functionality here.

    TouchGFXGeneratedHAL::setTFTFrameBuffer(address);
}

bool TouchGFXHAL::blockCopy(void* RESTRICT dest, const void* RESTRICT src, uint32_t numBytes)
{
    return TouchGFXGeneratedHAL::blockCopy(dest, src, numBytes);
}

/**
 * Configures the interrupts relevant for TouchGFX. This primarily entails setting
 * the interrupt priorities for the DMA and LCD interrupts.
 */
void TouchGFXHAL::configureInterrupts()
{
    // Calling parent implementation of configureInterrupts().
    //
    // To overwrite the generated implementation, omit call to parent function
    // and implemented needed functionality here.

    TouchGFXGeneratedHAL::configureInterrupts();
}

/**
 * Used for enabling interrupts set in configureInterrupts()
 */
void TouchGFXHAL::enableInterrupts()
{
    // Calling parent implementation of enableInterrupts().
    //
    // To overwrite the generated implementation, omit call to parent function
    // and implemented needed functionality here.

    TouchGFXGeneratedHAL::enableInterrupts();
}

/**
 * Used for disabling interrupts set in configureInterrupts()
 */
void TouchGFXHAL::disableInterrupts()
{
    // Calling parent implementation of disableInterrupts().
    //
    // To overwrite the generated implementation, omit call to parent function
    // and implemented needed functionality here.

    TouchGFXGeneratedHAL::disableInterrupts();
}

/**
 * Configure the LCD controller to fire interrupts at VSYNC. Called automatically
 * once TouchGFX initialization has completed.
 */
void TouchGFXHAL::enableLCDControllerInterrupt()
{
    // Calling parent implementation of enableLCDControllerInterrupt().
    //
    // To overwrite the generated implementation, omit call to parent function
    // and implemented needed functionality here.

    TouchGFXGeneratedHAL::enableLCDControllerInterrupt();
}

void TouchGFXHAL::endFrame()
{
    TouchGFXGeneratedHAL::endFrame();
}

static __attribute__((noinline)) void FMC_BANK1_WriteReg(volatile uint8_t Reg)
{
    /* Write 8-bits Index, then write register */
    *(uint8_t*)(FMC_BANK1_REG) = Reg;
    __DSB();
}

static __attribute__((noinline)) void FMC_BANK1_WriteData(volatile uint8_t Data)
{
    /* Write 8-bits Reg */
    *(uint8_t*)(FMC_BANK1_MEM) = Data;
    __DSB();
}

/**
  * @brief  DMA transfer error callback
  * @note   This function is executed when the transfer error interrupt
  *         is generated during DMA transfer
  * @retval None
  */
static void DMA_TxErrorCallback(DMA_HandleTypeDef* hdma)
{
    __disable_irq();
    while (1)
    {
    }
}

static int32_t LCD_IO_SendDataDMA(uint8_t* pData, uint32_t Length)
{
    if (HAL_DMA_Start_IT(DISPLAY_DMA, (uint32_t)pData, (uint32_t)FMC_BANK1_MEM, Length) != HAL_OK)
    {
        /* Transfer Error */
        return ERROR_BUS_FAILURE;
    }

    return 0;
}

void initLCD(void)
{
    /* Reset DISPLAY */
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(10); /* wait at least 10us according DISPLAY datasheet */
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(120); /* wait maximum 120ms according DISPLAY datasheet */

    /* set p-gamma */
    FMC_BANK1_WriteReg(0xE0);
    FMC_BANK1_WriteData(0x00);
    FMC_BANK1_WriteData(0x10);
    FMC_BANK1_WriteData(0x14);
    FMC_BANK1_WriteData(0x01);
    FMC_BANK1_WriteData(0x0E);
    FMC_BANK1_WriteData(0x04);
    FMC_BANK1_WriteData(0x33);
    FMC_BANK1_WriteData(0x56);
    FMC_BANK1_WriteData(0x48);
    FMC_BANK1_WriteData(0x03);
    FMC_BANK1_WriteData(0x0C);
    FMC_BANK1_WriteData(0x0B);
    FMC_BANK1_WriteData(0x2B);
    FMC_BANK1_WriteData(0x34);
    FMC_BANK1_WriteData(0x0F);

    /* set n-gamma */
    FMC_BANK1_WriteReg(0XE1);
    FMC_BANK1_WriteData(0x00);
    FMC_BANK1_WriteData(0x12);
    FMC_BANK1_WriteData(0x18);
    FMC_BANK1_WriteData(0x05);
    FMC_BANK1_WriteData(0x12);
    FMC_BANK1_WriteData(0x06);
    FMC_BANK1_WriteData(0x40);
    FMC_BANK1_WriteData(0x34);
    FMC_BANK1_WriteData(0x57);
    FMC_BANK1_WriteData(0x06);
    FMC_BANK1_WriteData(0x10);
    FMC_BANK1_WriteData(0x0C);
    FMC_BANK1_WriteData(0x3B);
    FMC_BANK1_WriteData(0x3F);
    FMC_BANK1_WriteData(0x0F);

    /* set power control 1 */
    FMC_BANK1_WriteReg(0XC0);
    FMC_BANK1_WriteData(0x0F);
    FMC_BANK1_WriteData(0x0C);

    /* set power control 2 */
    FMC_BANK1_WriteReg(0xC1);
    FMC_BANK1_WriteData(0x41);

    /* set VCOM control */
    FMC_BANK1_WriteReg(0xC5);
    FMC_BANK1_WriteData(0x00);
    FMC_BANK1_WriteData(0x25);
    FMC_BANK1_WriteData(0x80);

    /* memory access */
    FMC_BANK1_WriteReg(0x36);
    FMC_BANK1_WriteData(0x48);

    /* pixel format */
    FMC_BANK1_WriteReg(0x3A);
    FMC_BANK1_WriteData(0x55);

    /* interface mode control */
    FMC_BANK1_WriteReg(0xB0);
    FMC_BANK1_WriteData(0x00);

    /* frame rate control */
    FMC_BANK1_WriteReg(0xB1);
    FMC_BANK1_WriteData(0xA0);

    /* display function control */
    FMC_BANK1_WriteReg(0xB6);
    FMC_BANK1_WriteData(0x02);
    FMC_BANK1_WriteData(0x02);

    /* display inversion on */
    FMC_BANK1_WriteReg(0x21);

    /* sleep out */
    FMC_BANK1_WriteReg(0x11);
    HAL_Delay(120);

    /* tearing effect line on */
    FMC_BANK1_WriteReg(0x35);
    FMC_BANK1_WriteData(0x00);

    /* Set Tearing Effect */
    FMC_BANK1_WriteReg(DISPLAY_TEON);
    FMC_BANK1_WriteData(0x00);  // TE Mode 1

    /* Turn On Display */
    FMC_BANK1_WriteReg(DISPLAY_ON);
}

void setDisplayWindow(uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height)
{
    // Set pixel x position
    FMC_BANK1_WriteReg(DISPLAY_CASET);
    FMC_BANK1_WriteData((uint8_t)(Xpos >> 8U));                    /* XS[15:8] */
    FMC_BANK1_WriteData((uint8_t)(Xpos & 0xFFU));                  /* XS[7:0] */
    FMC_BANK1_WriteData((uint8_t)((Xpos + Width - 1U) >> 8U));     /* XE[15:8] */
    FMC_BANK1_WriteData((uint8_t)((Xpos + Width - 1U) & 0xFFU));   /* XE[7:0] */

    // Set pixel y position
    FMC_BANK1_WriteReg(DISPLAY_RASET);
    FMC_BANK1_WriteData((uint8_t)(Ypos >> 8U));                    /* YS[15:8] */
    FMC_BANK1_WriteData((uint8_t)(Ypos & 0xFFU));                  /* YS[7:0] */
    FMC_BANK1_WriteData((uint8_t)((Ypos + Height - 1U) >> 8U));    /* YE[15:8] */
    FMC_BANK1_WriteData((uint8_t)((Ypos + Height - 1U) & 0xFFU));  /* YE[7:0] */

    FMC_BANK1_WriteReg(DISPLAY_GRAM);
}

extern bool newTouch;
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == LCD_TE_Pin)
    {
        GPIO::clear(GPIO::VSYNC_FREQ);
    }
    else if (GPIO_Pin == TOUCH_IRQ_Pin)
    {
        newTouch = true;
    }
}

extern "C"
{
    portBASE_TYPE IdleTaskHook(void* p)
    {
        if ((int)p) //idle task sched out
        {
            touchgfx::HAL::getInstance()->setMCUActive(true);
        }
        else //idle task sched in
        {
            touchgfx::HAL::getInstance()->setMCUActive(false);
        }
        return pdTRUE;
    }
}

/* USER CODE END TouchGFXHAL.cpp */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
