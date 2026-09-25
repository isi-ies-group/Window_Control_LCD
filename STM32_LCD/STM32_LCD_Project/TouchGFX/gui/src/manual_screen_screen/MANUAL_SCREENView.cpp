#include <gui/manual_screen_screen/MANUAL_SCREENView.hpp>

#ifndef SIMULATOR
    #include "stm32u5xx_hal.h"
#endif

MANUAL_SCREENView::MANUAL_SCREENView()
{

}

void MANUAL_SCREENView::setupScreen()
{
    MANUAL_SCREENViewBase::setupScreen();
}

void MANUAL_SCREENView::tearDownScreen()
{
    MANUAL_SCREENViewBase::tearDownScreen();
}

//Funciones propias
void MANUAL_SCREENView::LED_ON()
{
#ifndef SIMULATOR
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
#endif
}
void MANUAL_SCREENView::LED_OFF()
{
#ifndef SIMULATOR
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
#endif
}
