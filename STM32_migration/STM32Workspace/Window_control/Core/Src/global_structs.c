#include "global_structs.h"

SPAInputs g_SPAInputs = {0};
AOIInputs g_AOIInputs = {0};
InterpolInputs g_InterpolInputs = {0};

char g_country[32] = "Spain";
bool auto_on = false;
bool manual_time = false;
volatile int auto_counter = 0;
volatile uint32_t g_user_button_irq_count = 0U;
volatile uint32_t g_user_button_last_tick_ms = 0U;
volatile uint32_t g_user_button_event_posted = 0U;
volatile uint32_t g_user_button_level = 0U;

float g_x_val = 0.0f;
float g_z_val = 0.0f;
float g_x_target = 0.0f;
float g_z_target = 0.0f;
float g_interp_x_val = 0.0f;
float g_interp_z_val = 0.0f;
float g_query_aoit = 0.0f;
float g_query_aoil = 0.0f;
time_t g_sunrise_epoch = 0;
time_t g_sunset_epoch = 0;

volatile uint32_t g_vertical_top_right_alarm = 0U;
volatile uint32_t g_vertical_top_left_alarm = 0U;
volatile uint32_t g_horizontal_interior_left_alarm = 0U;
volatile uint32_t g_horizontal_interior_right_alarm = 0U;
volatile uint32_t g_vertical_bottom_left_alarm = 0U;
volatile uint32_t g_vertical_bottom_right_alarm = 0U;
volatile uint32_t g_horizontal_exterior_left_alarm = 0U;
volatile uint32_t g_horizontal_exterior_right_alarm = 0U;
volatile bool g_any_movement_alarm = false;
