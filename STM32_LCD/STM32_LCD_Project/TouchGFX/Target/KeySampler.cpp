#include "KeySampler.hpp"
#include "main.h"

using namespace touchgfx;

void KeySampler::init()
{
}

bool KeySampler::sample(uint8_t& key)
{
    bool newPress = false;
    static bool userButtonPressedLastFrame = false;
    const bool userButtonPressedThisFrame = HAL_GPIO_ReadPin(BUTTON_USER_GPIO_Port, BUTTON_USER_Pin) == GPIO_PIN_SET;

    if (userButtonPressedThisFrame && !userButtonPressedLastFrame)
    {
        newPress = true;
        key = 1; // This is the "hardware button key" referred in the TouchGFX Designer Interactions
    }

    userButtonPressedLastFrame = userButtonPressedThisFrame;

    return newPress;
}
