/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : TouchGFXHAL.hpp
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
#ifndef TouchGFXHAL_HPP
#define TouchGFXHAL_HPP

/* USER CODE BEGIN TouchGFXHAL.hpp */

#include <TouchGFXGeneratedHAL.hpp>
#include "stm32u5xx_hal.h"
#include <CortexMMCUInstrumentation.hpp>

#define ERROR_BUS_FAILURE -1

/* FMC registers */
#define FMC_BANK1_REG                     (0x60000000)
#define FMC_BANK1_MEM                     (0x60000000 | 0x0100000)

#define DISPLAY_SLEEP_OUT                 0x11U   /* Sleep out register */
#define DISPLAY_RAM_CTRL                  0xB0U   /* RAM Control */
#define DISPLAY_PIXEL_FORMAT              0x3AU   /* Pixel Format register */
#define DISPLAY_PV_GAMMA_CTRL             0xE0U   /* Positive Voltage Gamma Control */
#define DISPLAY_NV_GAMMA_CTRL             0xE1U   /* Negative Voltage Gamma Control */
#define DISPLAY_TEON                      0x35U   /* Tearing Effect Line ON */
#define DISPLAY_GRAM                      0x2CU   /* GRAM register */
#define DISPLAY_ON                        0x29U   /* Display on register */
#define DISPLAY_CASET                     0x2AU   /* Colomn address register */
#define DISPLAY_RASET                     0x2BU   /* Raw address register */
#define DISPLAY_RDID3                     0xDCU   /* Read ID 3 register */
#define DISPLAY_IFCTL                     0xF6U   /* Interface Control register */

/* Display Data */
#define DISPLAY_FORMAT_RBG565             0x55U   /* Pixel format RGB565 : 16 bpp */
#define DISPLAY_WIDTH                     320
#define DISPLAY_HEIGHT                    480


/**
 * @class TouchGFXHAL
 *
 * @brief HAL implementation for TouchGFX.
 *
 * @sa HAL
 */
class TouchGFXHAL : public TouchGFXGeneratedHAL
{
public:
    /**
     * @fn TouchGFXHAL::TouchGFXHAL(touchgfx::DMA_Interface& dma, touchgfx::LCD& display, touchgfx::TouchController& tc, uint16_t width, uint16_t height) : TouchGFXGeneratedHAL(dma, display, tc, width, height)
     *
     * @brief Constructor.
     *
     *        Constructor. Initializes members.
     *
     * @param [in,out] dma     Reference to DMA interface.
     * @param [in,out] display Reference to LCD interface.
     * @param [in,out] tc      Reference to Touch Controller driver.
     * @param width            Width of the display.
     * @param height           Height of the display.
     */
    TouchGFXHAL(touchgfx::DMA_Interface& dma, touchgfx::LCD& display, touchgfx::TouchController& tc, uint16_t width, uint16_t height) : TouchGFXGeneratedHAL(dma, display, tc, width, height)
    {
    }

    virtual void initialize();

    /**
     * @fn virtual void TouchGFXHAL::disableInterrupts();
     *
     * @brief Disables the DMA and LCD interrupts.
     *
     *        Disables the DMA and LCD interrupts.
     */
    virtual void disableInterrupts();

    /**
     * @fn virtual void TouchGFXHAL::enableInterrupts();
     *
     * @brief Enables the DMA and LCD interrupts.
     *
     *        Enables the DMA and LCD interrupts.
     */
    virtual void enableInterrupts();

    /**
     * @fn virtual void TouchGFXHAL::configureInterrupts();
     *
     * @brief Sets the DMA and LCD interrupt priorities.
     *
     *        Sets the DMA and LCD interrupt priorities.
     */
    virtual void configureInterrupts();

    /**
     * @fn virtual void TouchGFXHAL::enableLCDControllerInterrupt();
     *
     * @brief Configure the LCD controller to fire interrupts at VSYNC.
     *
     *        Configure the LCD controller to fire interrupts at VSYNC. Called automatically
     *        once TouchGFX initialization has completed.
     */
    virtual void enableLCDControllerInterrupt();

    virtual bool beginFrame();

    virtual void endFrame();

    /**
     * @fn virtual void TouchGFXHAL::flushFrameBuffer();
     *
     * @brief This function is called whenever the framework has performed a complete draw.
     *
     * This specialization is only in place to keep compilers happy. Base impl. will call the
     * Rect version.
     * @see HAL::flushFrameBuffer
     */
    virtual void flushFrameBuffer()
    {
        TouchGFXGeneratedHAL::flushFrameBuffer();
    }

    /**
     * @fn virtual void TouchGFXHAL::flushFrameBuffer(const Rect& rect);
     *
     * @brief This function is called whenever the framework has performed a partial draw.
     *
     *        This function is called whenever the framework has performed a partial draw.
     *        On the STM32F7, make sure to clean and invalidate the data cache. This is to
     *        ensure that LTDC sees correct data when transferring to the display.
     *
     * @param rect The area of the screen that has been drawn, expressed in absolute coordinates.
     *
     * @see flushFrameBuffer().
     */
    virtual void flushFrameBuffer(const touchgfx::Rect& rect);

    /**
     * @fn virtual bool TouchGFXHAL::blockCopy(void* RESTRICT dest, const void* RESTRICT src, uint32_t numBytes);
     *
     * @brief This function performs a platform-specific memcpy.
     *
     *        This function performs a platform-specific memcpy, if supported by the hardware.
     *
     * @param [out] dest Pointer to destination memory.
     * @param [in] src   Pointer to source memory.
     * @param numBytes   Number of bytes to copy.
     *
     * @return true if the copy succeeded, false if copy was not performed.
     */
    virtual bool blockCopy(void* RESTRICT dest, const void* RESTRICT src, uint32_t numBytes);

    virtual uint16_t getTFTCurrentLine();

protected:
    /**
     * @fn virtual uint16_t* TouchGFXHAL::getTFTFrameBuffer() const;
     *
     * @brief Gets the frame buffer address used by the TFT controller.
     *
     *        Gets the frame buffer address used by the TFT controller.
     *
     * @return The address of the frame buffer currently being displayed on the TFT.
     */
    virtual uint16_t* getTFTFrameBuffer() const;

    /**
     * @fn virtual void TouchGFXHAL::setTFTFrameBuffer(uint16_t* adr);
     *
     * @brief Sets the frame buffer address used by the TFT controller.
     *
     *        Sets the frame buffer address used by the TFT controller.
     *
     * @param [in,out] adr New frame buffer address.
     */
    virtual void setTFTFrameBuffer(uint16_t* adr);

private:
    touchgfx::CortexMMCUInstrumentation instrumentation;
};

/* USER CODE END TouchGFXHAL.hpp */

#endif // TouchGFXHAL_HPP

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
