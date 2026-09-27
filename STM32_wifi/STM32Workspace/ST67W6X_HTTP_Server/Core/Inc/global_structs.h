#ifndef GLOBAL_STRUCTS_H
#define GLOBAL_STRUCTS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "global_def.h"

typedef struct
{
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  double longitude;
  double latitude;
} SPAInputs;

typedef struct
{
  double pan;
  double azimuth;
  double elevation;
  double tilt;
  bool tilt_correction;
} AOIInputs;

typedef struct
{
  float AOIt;
  float AOIl;
  const float (*matrix_X)[MATRIX_SIZE];
  const float (*matrix_Z)[MATRIX_SIZE];
} InterpolInputs;

extern SPAInputs g_SPAInputs;
extern AOIInputs g_AOIInputs;
extern InterpolInputs g_InterpolInputs;

extern char g_country[32];
extern bool auto_on;
extern bool manual_time;
extern volatile int auto_counter;

extern float g_x_val;
extern float g_z_val;
extern float g_x_target;
extern float g_z_target;
extern float g_interp_x_val;
extern float g_interp_z_val;
extern float g_query_aoit;
extern float g_query_aoil;
extern time_t g_sunrise_epoch;
extern time_t g_sunset_epoch;

extern volatile bool g_any_movement_alarm;
extern volatile uint32_t Vertical_top_right_alarm;
extern volatile uint32_t Vertical_top_left_alarm;
extern volatile uint32_t horizontal_interior_left_alarm;
extern volatile uint32_t horizontal_interior_right_alarm;
extern volatile uint32_t vertical_bottom_left_alarm;
extern volatile uint32_t vertical_bottom_right_alarm;
extern volatile uint32_t horizontal_exterior_left_alarm;
extern volatile uint32_t horizontal_exterior_right_alarm;

#endif /* GLOBAL_STRUCTS_H */
