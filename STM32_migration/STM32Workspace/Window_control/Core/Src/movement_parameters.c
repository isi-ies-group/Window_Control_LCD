#include "movement_parameters.h"

float g_movement_hysteresis_gain = MOVEMENT_HYSTERESIS_DEFAULT_GAIN;
float g_movement_hysteresis_offset_mm = MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM;
float g_vertical_movement_hysteresis_gain = VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_GAIN;
float g_vertical_movement_hysteresis_offset_mm = VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM;
float g_movement_max_x_mm = MOVEMENT_DEFAULT_MAX_X_MM;
float g_movement_max_z_mm = MOVEMENT_DEFAULT_MAX_Z_MM;
uint32_t g_movement_speed_us = MOVEMENT_DEFAULT_STEP_DELAY_US;
uint32_t g_movement_home_speed_us = MOVEMENT_DEFAULT_HOME_STEP_DELAY_US;

void MovementParameters_ResetDefaults(void)
{
  /*
   * What: restore the firmware movement calibration to known-safe defaults.
   * How: writes all runtime movement parameters in one place.
   * Why: storage migration and invalid flash fallback must not duplicate default values.
   */
  g_movement_hysteresis_gain = MOVEMENT_HYSTERESIS_DEFAULT_GAIN;
  g_movement_hysteresis_offset_mm = MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM;
  g_vertical_movement_hysteresis_gain = VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_GAIN;
  g_vertical_movement_hysteresis_offset_mm = VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM;
  g_movement_max_x_mm = MOVEMENT_DEFAULT_MAX_X_MM;
  g_movement_max_z_mm = MOVEMENT_DEFAULT_MAX_Z_MM;
  g_movement_speed_us = MOVEMENT_DEFAULT_STEP_DELAY_US;
  g_movement_home_speed_us = MOVEMENT_DEFAULT_HOME_STEP_DELAY_US;
}

bool MovementParameters_IsGainValid(float value)
{
  return (value == value) && (value >= -1.0f) && (value <= 1.0f);
}

bool MovementParameters_IsOffsetMmValid(float value)
{
  return (value == value) && (value >= -50.0f) && (value <= 50.0f);
}

bool MovementParameters_IsRangeMmValid(float value)
{
  return (value == value) && (value >= 0.0f) && (value <= 200.0f);
}

bool MovementParameters_IsSpeedUsValid(uint32_t value)
{
  return (value >= 100U) && (value <= 10000U);
}

float MovementParameters_ValidatedGain(float value, float default_value)
{
  return MovementParameters_IsGainValid(value) ? value : default_value;
}

float MovementParameters_ValidatedOffsetMm(float value, float default_value)
{
  return MovementParameters_IsOffsetMmValid(value) ? value : default_value;
}

float MovementParameters_ValidatedRangeMm(float value, float default_value)
{
  return MovementParameters_IsRangeMmValid(value) ? value : default_value;
}

uint32_t MovementParameters_ValidatedSpeedUs(uint32_t value, uint32_t default_value)
{
  return MovementParameters_IsSpeedUsValid(value) ? value : default_value;
}

bool MovementParameters_SetHorizontalGain(float gain)
{
  /*
   * What: update the horizontal/Z proportional correction.
   * How: accepts only the same validated range used by flash restore.
   * Why: non-WiFi inputs can reuse this path without duplicating safety checks.
   */
  if (!MovementParameters_IsGainValid(gain))
  {
    return false;
  }

  g_movement_hysteresis_gain = gain;
  return true;
}

bool MovementParameters_SetHorizontalOffsetMm(float offset_mm)
{
  if (!MovementParameters_IsOffsetMmValid(offset_mm))
  {
    return false;
  }

  g_movement_hysteresis_offset_mm = offset_mm;
  return true;
}

bool MovementParameters_SetVerticalGain(float gain)
{
  if (!MovementParameters_IsGainValid(gain))
  {
    return false;
  }

  g_vertical_movement_hysteresis_gain = gain;
  return true;
}

bool MovementParameters_SetVerticalOffsetMm(float offset_mm)
{
  if (!MovementParameters_IsOffsetMmValid(offset_mm))
  {
    return false;
  }

  g_vertical_movement_hysteresis_offset_mm = offset_mm;
  return true;
}

bool MovementParameters_SetMaxXMM(float max_x_mm)
{
  if (!MovementParameters_IsRangeMmValid(max_x_mm))
  {
    return false;
  }

  g_movement_max_x_mm = max_x_mm;
  return true;
}

bool MovementParameters_SetMaxZMM(float max_z_mm)
{
  if (!MovementParameters_IsRangeMmValid(max_z_mm))
  {
    return false;
  }

  g_movement_max_z_mm = max_z_mm;
  return true;
}

bool MovementParameters_SetMovementSpeedUs(uint32_t speed_us)
{
  if (!MovementParameters_IsSpeedUsValid(speed_us))
  {
    return false;
  }

  g_movement_speed_us = speed_us;
  return true;
}

bool MovementParameters_SetHomeSpeedUs(uint32_t speed_us)
{
  if (!MovementParameters_IsSpeedUsValid(speed_us))
  {
    return false;
  }

  g_movement_home_speed_us = speed_us;
  return true;
}

float MovementParameters_ClampXTarget(float xmm)
{
  float max_x = MovementParameters_ValidatedRangeMm(g_movement_max_x_mm,
                                                    MOVEMENT_DEFAULT_MAX_X_MM);

  if ((xmm != xmm) || (xmm < 0.0f))
  {
    return 0.0f;
  }

  if (xmm > max_x)
  {
    return max_x;
  }

  return xmm;
}

float MovementParameters_ClampZTarget(float zmm)
{
  float max_z = MovementParameters_ValidatedRangeMm(g_movement_max_z_mm,
                                                    MOVEMENT_DEFAULT_MAX_Z_MM);

  if ((zmm != zmm) || (zmm < 0.0f))
  {
    return 0.0f;
  }

  if (zmm > max_z)
  {
    return max_z;
  }

  return zmm;
}
