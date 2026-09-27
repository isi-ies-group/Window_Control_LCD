#ifndef MOVEMENT_PARAMETERS_H
#define MOVEMENT_PARAMETERS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Movement parameters shared by WiFi, movement and future non-WiFi contexts.
 * Horizontal parameters are applied to the Z axis and vertical parameters to the X axis.
 */
#define MOVEMENT_HYSTERESIS_DEFAULT_GAIN              (4.0f / 75.0f)
#define MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM         0.0f
#define VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_GAIN     0.0f
#define VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM 0.0f

#define MOVEMENT_DEFAULT_MAX_X_MM                     55.0f
#define MOVEMENT_DEFAULT_MAX_Z_MM                     72.0f
#define MOVEMENT_DEFAULT_STEP_DELAY_US                600U
#define MOVEMENT_DEFAULT_HOME_STEP_DELAY_US           1000U
#define MOVEMENT_SECOND_TOUCH_STEP_DELAY_US           1000U

extern float g_movement_hysteresis_gain;
extern float g_movement_hysteresis_offset_mm;
extern float g_vertical_movement_hysteresis_gain;
extern float g_vertical_movement_hysteresis_offset_mm;
extern float g_movement_max_x_mm;
extern float g_movement_max_z_mm;
extern uint32_t g_movement_speed_us;
extern uint32_t g_movement_home_speed_us;

void MovementParameters_ResetDefaults(void);

bool MovementParameters_IsGainValid(float value);
bool MovementParameters_IsOffsetMmValid(float value);
bool MovementParameters_IsRangeMmValid(float value);
bool MovementParameters_IsSpeedUsValid(uint32_t value);

float MovementParameters_ValidatedGain(float value, float default_value);
float MovementParameters_ValidatedOffsetMm(float value, float default_value);
float MovementParameters_ValidatedRangeMm(float value, float default_value);
uint32_t MovementParameters_ValidatedSpeedUs(uint32_t value, uint32_t default_value);

bool MovementParameters_SetHorizontalGain(float gain);
bool MovementParameters_SetHorizontalOffsetMm(float offset_mm);
bool MovementParameters_SetVerticalGain(float gain);
bool MovementParameters_SetVerticalOffsetMm(float offset_mm);
bool MovementParameters_SetMaxXMM(float max_x_mm);
bool MovementParameters_SetMaxZMM(float max_z_mm);
bool MovementParameters_SetMovementSpeedUs(uint32_t speed_us);
bool MovementParameters_SetHomeSpeedUs(uint32_t speed_us);

float MovementParameters_ClampXTarget(float xmm);
float MovementParameters_ClampZTarget(float zmm);

#ifdef __cplusplus
}
#endif

#endif /* MOVEMENT_PARAMETERS_H */
