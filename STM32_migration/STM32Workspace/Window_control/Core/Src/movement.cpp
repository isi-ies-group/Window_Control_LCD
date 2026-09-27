#include "movement.h"

#include <stdint.h>
#include <stdlib.h>

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "global_structs.h"
#include "movement_alarm.h"
#include "movement_parameters.h"

#if defined(ZRI_Pin) && defined(ZRI_GPIO_Port)
#define MOVEMENT_HAS_ZRI_LIMIT 1
#else
#define MOVEMENT_HAS_ZRI_LIMIT 0
#endif

/* MXRI direction must come from CubeMX; fail loudly if the GPIO label is lost. */
#ifndef MXRI_Z_DIR_Pin
#error "MXRI_Z_DIR_Pin is missing. Check the MXRI_Z_DIR GPIO label in the .ioc file."
#endif

#ifndef MXRI_Z_DIR_GPIO_Port
#error "MXRI_Z_DIR_GPIO_Port is missing. Check the MXRI_Z_DIR GPIO label in the .ioc file."
#endif

/* STM32 pin aliases kept close to the original ESP32 movement numbering. */
#define STEP1_Pin        MXLI_X_STEP_Pin
#define STEP1_Port       MXLI_X_STEP_GPIO_Port
#define DIR1_Pin         MXLI_X_DIR_Pin
#define DIR1_Port        MXLI_X_DIR_GPIO_Port

#define STEP2_Pin        MXLE_Y_STEP_Pin
#define STEP2_Port       MXLE_Y_STEP_GPIO_Port
#define DIR2_Pin         MXLE_Y_DIR_Pin
#define DIR2_Port        MXLE_Y_DIR_GPIO_Port

#define STEP3_Pin        MXRI_Z_STEP_Pin
#define STEP3_Port       MXRI_Z_STEP_GPIO_Port
#define DIR3_Pin         MXRI_Z_DIR_Pin
#define DIR3_Port        MXRI_Z_DIR_GPIO_Port

#define STEP4_Pin        MXRE_A_STEP_Pin
#define STEP4_Port       MXRE_A_STEP_GPIO_Port
#define DIR4_Pin         MXRE_A_DIR_Pin
#define DIR4_Port        MXRE_A_DIR_GPIO_Port

#define STEP5_Pin        ZR_Z_STEP_Pin
#define STEP5_Port       ZR_Z_STEP_GPIO_Port
#define DIR5_Pin         ZR_Z_DIR_Pin
#define DIR5_Port        ZR_Z_DIR_GPIO_Port

#define STEP6_Pin        ZL_A_STEP_Pin
#define STEP6_Port       ZL_A_STEP_GPIO_Port
#define DIR6_Pin         ZL_A_DIR_Pin
#define DIR6_Port        ZL_A_DIR_GPIO_Port

#define ENABLE_X_Pin     Vertical_ENABLE_Pin
#define ENABLE_X_Port    Vertical_ENABLE_GPIO_Port
#define ENABLE_Z_Pin     Horizontal_ENABLE_Pin
#define ENABLE_Z_Port    Horizontal_ENABLE_GPIO_Port

/* CNC shield enables are active low: RESET enables, SET disables. */
#define ENABLE_ACTIVE     GPIO_PIN_RESET
#define ENABLE_INACTIVE   GPIO_PIN_SET

static const long DIR_CHANGE_DELAY_US = 10000;
static const int BACKOFF_STEPS = 30;
static const long VERTICAL_STEPS_PER_MM = 25;
static const long HORIZONTAL_STEPS_PER_MM = 20;
static const long FIRST_TOUCH_EXTRA_MM = 30;
static const long SECOND_TOUCH_EXTRA_MM = 70;
static const long MAX_VERTICAL_SECOND_TOUCH_STEPS =
  BACKOFF_STEPS + (VERTICAL_STEPS_PER_MM * SECOND_TOUCH_EXTRA_MM);
static const long MAX_HORIZONTAL_SECOND_TOUCH_STEPS =
  BACKOFF_STEPS + (HORIZONTAL_STEPS_PER_MM * SECOND_TOUCH_EXTRA_MM);

/* Compensated motor-step counters; logical position stays in g_x_val/g_z_val. */
static long CurrentStep1 = 0;
static long CurrentStep2 = 0;

typedef struct
{
  volatile uint8_t xli;
  volatile uint8_t xle;
  volatile uint8_t xri;
  volatile uint8_t xre;
  volatile uint8_t xlib;
  volatile uint8_t xleb;
  volatile uint8_t xrib;
  volatile uint8_t xreb;
  volatile uint8_t zl;
  volatile uint8_t zr;
  volatile uint8_t zli;
  volatile uint8_t zri;
} LimitSwitchState;

/*
 * What: EXTI callbacks keep the current endstop level for movement decisions.
 * How: rising edges store 1, falling edges store 0, and phase starts resync from GPIO.
 * Why: a released endstop must allow movement again without waiting for a polling read.
 */
static LimitSwitchState limitSwitchState = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

static void BackoffAll(int steps, long speed_us);
static void SecondTouchPair(long speed_us);
static uint8_t read_limit_pin(GPIO_TypeDef *port, uint16_t pin);
static bool xli_limit_active(void);
static bool xle_limit_active(void);
static bool xri_limit_active(void);
static bool xre_limit_active(void);
static bool xlib_limit_active(void);
static bool xleb_limit_active(void);
static bool xrib_limit_active(void);
static bool xreb_limit_active(void);
static bool zl_limit_active(void);
static bool zr_limit_active(void);
static bool zli_limit_active(void);
static bool zri_limit_active(void);
static bool vertical_top_left_limit_active(void);
static bool vertical_top_right_limit_active(void);
static bool vertical_bottom_left_limit_active(void);
static bool vertical_bottom_right_limit_active(void);
static bool vertical_limit_active(void);
static bool vertical_far_limit_active(void);
static bool horizontal_exterior_left_limit_active(void);
static bool horizontal_exterior_right_limit_active(void);
static bool horizontal_interior_left_limit_active(void);
static bool horizontal_interior_right_limit_active(void);
static bool horizontal_limit_active(void);
static bool horizontal_far_limit_active(void);
static bool all_vertical_limits_active(void);
static bool all_horizontal_limits_active(void);
static bool limit_should_stop_axis(bool moving_positive, bool released_once, bool home_limit_active, bool far_limit_active);

/* Busy-wait for short motor pulse delays using the DWT cycle counter. */
static void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = (HAL_RCC_GetHCLKFreq() / 1000000U) * us;

  while ((DWT->CYCCNT - start) < cycles)
  {
  }
}

/* Enable the DWT cycle counter used by delay_us(). */
static void dwt_delay_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Return the absolute value of a signed long without pulling in extra helpers. */
static long abs_long(long value)
{
  return (value < 0) ? -value : value;
}

static float abs_float(float value)
{
  return (value < 0.0f) ? -value : value;
}

static uint32_t movement_step_delay_us(void)
{
  return MovementParameters_ValidatedSpeedUs(g_movement_speed_us,
                                             MOVEMENT_DEFAULT_STEP_DELAY_US);
}

static uint32_t movement_home_step_delay_us(void)
{
  return MovementParameters_ValidatedSpeedUs(g_movement_home_speed_us,
                                             MOVEMENT_DEFAULT_HOME_STEP_DELAY_US);
}

static long movement_homing_max_steps(float configured_range_mm,
                                      float default_range_mm,
                                      long steps_per_mm,
                                      float gain,
                                      float offset_mm,
                                      float default_gain,
                                      float default_offset_mm)
{
  float range_mm = MovementParameters_ValidatedRangeMm(configured_range_mm,
                                                       default_range_mm);
  float safe_mm;

  if (steps_per_mm <= 0)
  {
    return 0;
  }

  gain = MovementParameters_ValidatedGain(gain, default_gain);
  offset_mm = MovementParameters_ValidatedOffsetMm(offset_mm, default_offset_mm);

  /*
   * What: calculate the first-touch homing runaway limit from the configured travel.
   * How: uses range*(1+gain), adds the absolute offset and the coarse-touch safety margin.
   * Why: X/Z range and offset are tunable now, so homing cannot depend on fixed old steps.
   */
  safe_mm = (range_mm * (1.0f + gain)) + abs_float(offset_mm) + (float)FIRST_TOUCH_EXTRA_MM;
  if (safe_mm <= 0.0f)
  {
    return 0;
  }

  return (long)((safe_mm * (float)steps_per_mm) + 0.5f);
}

static long vertical_homing_max_steps(void)
{
  return movement_homing_max_steps(g_movement_max_x_mm,
                                   MOVEMENT_DEFAULT_MAX_X_MM,
                                   VERTICAL_STEPS_PER_MM,
                                   g_vertical_movement_hysteresis_gain,
                                   g_vertical_movement_hysteresis_offset_mm,
                                   VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_GAIN,
                                   VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM);
}

static long horizontal_homing_max_steps(void)
{
  return movement_homing_max_steps(g_movement_max_z_mm,
                                   MOVEMENT_DEFAULT_MAX_Z_MM,
                                   HORIZONTAL_STEPS_PER_MM,
                                   g_movement_hysteresis_gain,
                                   g_movement_hysteresis_offset_mm,
                                   MOVEMENT_HYSTERESIS_DEFAULT_GAIN,
                                   MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM);
}

static long movement_compensated_target_steps(float logical_mm,
                                              long steps_per_mm,
                                              float gain,
                                              float offset_mm,
                                              float default_gain,
                                              float default_offset_mm)
{
  float compensated_mm;
  float compensated_steps;

  if (steps_per_mm <= 0)
  {
    return 0;
  }

  if (logical_mm != logical_mm)
  {
    logical_mm = 0.0f;
  }

  gain = MovementParameters_ValidatedGain(gain, default_gain);
  offset_mm = MovementParameters_ValidatedOffsetMm(offset_mm, default_offset_mm);

  /*
   * What: convert a logical target into the compensated motor coordinate.
   * How: applies compensation once to the absolute target: motor_mm = target*(1+gain) - offset.
   * Why: offset/gain must not be added to every small movement or automatic mode will accumulate error.
   */
  compensated_mm = (logical_mm * (1.0f + gain)) - offset_mm;
  if (compensated_mm <= 0.0f)
  {
    return 0;
  }

  compensated_steps = compensated_mm * (float)steps_per_mm;
  return (long)(compensated_steps + 0.5f);
}

static long horizontal_compensated_target_steps(float logical_mm)
{
  return movement_compensated_target_steps(logical_mm,
                                           HORIZONTAL_STEPS_PER_MM,
                                           g_movement_hysteresis_gain,
                                           g_movement_hysteresis_offset_mm,
                                           MOVEMENT_HYSTERESIS_DEFAULT_GAIN,
                                           MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM);
}

static long vertical_compensated_target_steps(float logical_mm)
{
  return movement_compensated_target_steps(logical_mm,
                                           VERTICAL_STEPS_PER_MM,
                                           g_vertical_movement_hysteresis_gain,
                                           g_vertical_movement_hysteresis_offset_mm,
                                           VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_GAIN,
                                           VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM);
}

float movementClampHorizontalTarget(float zmm)
{
  return MovementParameters_ClampZTarget(zmm);
}

float movementClampVerticalTarget(float xmm)
{
  return MovementParameters_ClampXTarget(xmm);
}

/* Small wrapper around HAL_GPIO_WritePin() to keep movement code compact. */
static void write_pin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  HAL_GPIO_WritePin(port, pin, state);
}

/* Small wrapper around HAL_GPIO_ReadPin() to keep limit reads consistent. */
static GPIO_PinState read_pin(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin);
}

/* Convert an active-high endstop GPIO level into a cached boolean value. */
static uint8_t read_limit_pin(GPIO_TypeDef *port, uint16_t pin)
{
  return (read_pin(port, pin) == GPIO_PIN_SET) ? 1U : 0U;
}

/*
 * What: update the endstop state touched by an EXTI edge.
 * How: samples the GPIO level, so rising marks active and falling marks released.
 * Why: the movement task can stop or re-allow pulses from the latest interrupt state.
 */
uint8_t movementLimitSwitchUpdateFromExti(uint16_t gpio_pin)
{
  switch (gpio_pin)
  {
    case XLI_Pin:
      limitSwitchState.xli = read_limit_pin(XLI_GPIO_Port, XLI_Pin);
      break;

    case XLE_Pin:
      limitSwitchState.xle = read_limit_pin(XLE_GPIO_Port, XLE_Pin);
      break;

    case XRI_Pin:
      limitSwitchState.xri = read_limit_pin(XRI_GPIO_Port, XRI_Pin);
      break;

    case XRE_Pin:
      limitSwitchState.xre = read_limit_pin(XRE_GPIO_Port, XRE_Pin);
      break;

    case XLIB_Pin:
      limitSwitchState.xlib = read_limit_pin(XLIB_GPIO_Port, XLIB_Pin);
      break;

    case XLEB_Pin:
      limitSwitchState.xleb = read_limit_pin(XLEB_GPIO_Port, XLEB_Pin);
      break;

    case XRIB_Pin:
      limitSwitchState.xrib = read_limit_pin(XRIB_GPIO_Port, XRIB_Pin);
      break;

    case XREB_Pin:
      limitSwitchState.xreb = read_limit_pin(XREB_GPIO_Port, XREB_Pin);
      break;

    case ZL_Pin:
      limitSwitchState.zl = read_limit_pin(ZL_GPIO_Port, ZL_Pin);
      break;

    case ZR_Pin:
      limitSwitchState.zr = read_limit_pin(ZR_GPIO_Port, ZR_Pin);
      break;

    case ZLI_Pin:
      limitSwitchState.zli = read_limit_pin(ZLI_GPIO_Port, ZLI_Pin);
      break;

#if MOVEMENT_HAS_ZRI_LIMIT
    case ZRI_Pin:
      limitSwitchState.zri = read_limit_pin(ZRI_GPIO_Port, ZRI_Pin);
      break;
#endif

    default:
      return 0U;
  }

  return 1U;
}

/*
 * What: seed the EXTI cache with the real electrical state of all endstops.
 * How: reads every GPIO directly before a movement phase starts.
 * Why: if a switch is already pressed before motion begins, no new rising edge will occur.
 */
void movementLimitSwitchRefreshAll(void)
{
  limitSwitchState.xli = read_limit_pin(XLI_GPIO_Port, XLI_Pin);
  limitSwitchState.xle = read_limit_pin(XLE_GPIO_Port, XLE_Pin);
  limitSwitchState.xri = read_limit_pin(XRI_GPIO_Port, XRI_Pin);
  limitSwitchState.xre = read_limit_pin(XRE_GPIO_Port, XRE_Pin);
  limitSwitchState.xlib = read_limit_pin(XLIB_GPIO_Port, XLIB_Pin);
  limitSwitchState.xleb = read_limit_pin(XLEB_GPIO_Port, XLEB_Pin);
  limitSwitchState.xrib = read_limit_pin(XRIB_GPIO_Port, XRIB_Pin);
  limitSwitchState.xreb = read_limit_pin(XREB_GPIO_Port, XREB_Pin);
  limitSwitchState.zl = read_limit_pin(ZL_GPIO_Port, ZL_Pin);
  limitSwitchState.zr = read_limit_pin(ZR_GPIO_Port, ZR_Pin);
  limitSwitchState.zli = read_limit_pin(ZLI_GPIO_Port, ZLI_Pin);
#if MOVEMENT_HAS_ZRI_LIMIT
  limitSwitchState.zri = read_limit_pin(ZRI_GPIO_Port, ZRI_Pin);
#else
  limitSwitchState.zri = 0U;
#endif
}

/* Report whether at least one cached endstop input is currently active. */
uint8_t movementAnyLimitSwitchActive(void)
{
  /*
   * What: report if any endstop is active for debug/LED feedback.
   * How: uses the same EXTI-fed state that movement uses.
   * Why: the LED must reflect the current interrupt state, including falling-edge releases.
   */
  return xli_limit_active() ||
         xle_limit_active() ||
         xri_limit_active() ||
         xre_limit_active() ||
         xlib_limit_active() ||
         xleb_limit_active() ||
         xrib_limit_active() ||
         xreb_limit_active() ||
         zl_limit_active() ||
         zr_limit_active() ||
         zli_limit_active() ||
         zri_limit_active();
}

/* Return the EXTI-fed state for the XLI vertical endstop. */
static bool xli_limit_active(void)
{
  return (limitSwitchState.xli != 0U);
}

/* Return the EXTI-fed state for the XLE vertical endstop. */
static bool xle_limit_active(void)
{
  return (limitSwitchState.xle != 0U);
}

/* Return the EXTI-fed state for the XRI vertical endstop. */
static bool xri_limit_active(void)
{
  return (limitSwitchState.xri != 0U);
}

/* Return the EXTI-fed state for the XRE vertical endstop. */
static bool xre_limit_active(void)
{
  return (limitSwitchState.xre != 0U);
}

/* Return the EXTI-fed state for the XLIB vertical far endstop. */
static bool xlib_limit_active(void)
{
  return (limitSwitchState.xlib != 0U);
}

/* Return the EXTI-fed state for the XLEB vertical far endstop. */
static bool xleb_limit_active(void)
{
  return (limitSwitchState.xleb != 0U);
}

/* Return the EXTI-fed state for the XRIB vertical far endstop. */
static bool xrib_limit_active(void)
{
  return (limitSwitchState.xrib != 0U);
}

/* Return the EXTI-fed state for the XREB vertical far endstop. */
static bool xreb_limit_active(void)
{
  return (limitSwitchState.xreb != 0U);
}

/* Return the EXTI-fed state for the left horizontal endstop. */
static bool zl_limit_active(void)
{
  return (limitSwitchState.zl != 0U);
}

/* Return the EXTI-fed state for the right horizontal endstop. */
static bool zr_limit_active(void)
{
  return (limitSwitchState.zr != 0U);
}

/* Return the EXTI-fed state for the left horizontal far endstop. */
static bool zli_limit_active(void)
{
  return (limitSwitchState.zli != 0U);
}

/* Return the EXTI-fed state for the right horizontal far endstop. */
static bool zri_limit_active(void)
{
  return (limitSwitchState.zri != 0U);
}

/* Report whether the left vertical top/reference side is active. */
static bool vertical_top_left_limit_active(void)
{
  return xli_limit_active() ||
         xle_limit_active();
}

/* Report whether the right vertical top/reference side is active. */
static bool vertical_top_right_limit_active(void)
{
  return xri_limit_active() ||
         xre_limit_active();
}

/* Report whether the left vertical bottom/far side is active. */
static bool vertical_bottom_left_limit_active(void)
{
  return xlib_limit_active() ||
         xleb_limit_active();
}

/* Report whether the right vertical bottom/far side is active. */
static bool vertical_bottom_right_limit_active(void)
{
  return xrib_limit_active() ||
         xreb_limit_active();
}

/* Report whether any vertical-axis homing-side endstop is active. */
static bool vertical_limit_active(void)
{
  return vertical_top_right_limit_active() ||
         vertical_top_left_limit_active();
}

/* Report whether any vertical far-side endstop is active. */
static bool vertical_far_limit_active(void)
{
  return vertical_bottom_left_limit_active() ||
         vertical_bottom_right_limit_active();
}

/* Report whether the horizontal exterior-left/reference endstop is active. */
static bool horizontal_exterior_left_limit_active(void)
{
  return zl_limit_active();
}

/* Report whether the horizontal exterior-right/reference endstop is active. */
static bool horizontal_exterior_right_limit_active(void)
{
  return zr_limit_active();
}

/* Report whether the horizontal interior-left/far endstop is active. */
static bool horizontal_interior_left_limit_active(void)
{
  return zli_limit_active();
}

/* Report whether the horizontal interior-right/far endstop is active. */
static bool horizontal_interior_right_limit_active(void)
{
  return zri_limit_active();
}

/* Report whether any horizontal-axis homing-side endstop is active. */
static bool horizontal_limit_active(void)
{
  return horizontal_exterior_left_limit_active() ||
         horizontal_exterior_right_limit_active();
}

/* Report whether any horizontal far-side endstop is active. */
static bool horizontal_far_limit_active(void)
{
  return horizontal_interior_left_limit_active() ||
         horizontal_interior_right_limit_active();
}

/* Report whether every vertical motor has reached its own homing endstop. */
static bool all_vertical_limits_active(void)
{
  return xli_limit_active() &&
         xle_limit_active() &&
         xri_limit_active() &&
         xre_limit_active();
}

/* Report whether both horizontal skates have reached their own homing endstop. */
static bool all_horizontal_limits_active(void)
{
  return zl_limit_active() &&
         zr_limit_active();
}

/* Decide if a movement must stop because a same-direction limit is active. */
static bool limit_should_stop_axis(bool moving_positive,
                                   bool released_once,
                                   bool home_limit_active,
                                   bool far_limit_active)
{
  bool home_should_stop = home_limit_active && ((!moving_positive) || released_once);
  bool far_should_stop = far_limit_active && (moving_positive || released_once);

  return home_should_stop || far_should_stop;
}

/* Enable or disable all vertical motors through the active-low enable pin. */
static void enable_vertical(bool enabled)
{
  write_pin(ENABLE_X_Port, ENABLE_X_Pin, enabled ? ENABLE_ACTIVE : ENABLE_INACTIVE);
}

/* Enable or disable all horizontal motors through the active-low enable pin. */
static void enable_horizontal(bool enabled)
{
  write_pin(ENABLE_Z_Port, ENABLE_Z_Pin, enabled ? ENABLE_ACTIVE : ENABLE_INACTIVE);
}

/* Set vertical motor directions for movement away from the homing side. */
static void set_vertical_dir_positive(void)
{
  write_pin(DIR1_Port, DIR1_Pin, GPIO_PIN_RESET); /* MXLI */
  write_pin(DIR2_Port, DIR2_Pin, GPIO_PIN_RESET); /* MXLE */
  write_pin(DIR3_Port, DIR3_Pin, GPIO_PIN_RESET); /* MXRI */
  write_pin(DIR4_Port, DIR4_Pin, GPIO_PIN_RESET); /* MXRE */
}

/* Set vertical motor directions for movement toward the homing side. */
static void set_vertical_dir_negative(void)
{
  write_pin(DIR1_Port, DIR1_Pin, GPIO_PIN_SET); /* MXLI */
  write_pin(DIR2_Port, DIR2_Pin, GPIO_PIN_SET); /* MXLE */
  write_pin(DIR3_Port, DIR3_Pin, GPIO_PIN_SET); /* MXRI */
  write_pin(DIR4_Port, DIR4_Pin, GPIO_PIN_SET); /* MXRE */
}

/* Set horizontal motor directions for movement away from the homing side. */
static void set_horizontal_dir_positive(void)
{
  write_pin(DIR5_Port, DIR5_Pin, GPIO_PIN_RESET); /* ZR */
  write_pin(DIR6_Port, DIR6_Pin, GPIO_PIN_SET);   /* ZL */
}

/* Set horizontal motor directions for movement toward the homing side. */
static void set_horizontal_dir_negative(void)
{
  write_pin(DIR5_Port, DIR5_Pin, GPIO_PIN_SET);   /* ZR */
  write_pin(DIR6_Port, DIR6_Pin, GPIO_PIN_RESET); /* ZL */
}

/* Drive all vertical step pins to the same level. */
static void set_vertical_step(GPIO_PinState state)
{
  write_pin(STEP1_Port, STEP1_Pin, state);
  write_pin(STEP2_Port, STEP2_Pin, state);
  write_pin(STEP3_Port, STEP3_Pin, state);
  write_pin(STEP4_Port, STEP4_Pin, state);
}

/* Drive both horizontal step pins to the same level. */
static void set_horizontal_step(GPIO_PinState state)
{
  write_pin(STEP5_Port, STEP5_Pin, state);
  write_pin(STEP6_Port, STEP6_Pin, state);
}

/* Prepare DWT timing, disable drivers and reset software position counters. */
void init_motors(void)
{
  dwt_delay_init();
  movementLimitSwitchRefreshAll();

  enable_horizontal(false);
  enable_vertical(false);

  set_vertical_dir_negative();
  set_horizontal_dir_negative();

  set_vertical_step(GPIO_PIN_RESET);
  set_horizontal_step(GPIO_PIN_RESET);

  CurrentStep1 = 0;
  CurrentStep2 = 0;
}

/* Move to absolute X/Z targets while stopping the command on active endstops. */
bool move(float xmm, float zmm)
{
  bool alarm_stopped = false;
  uint32_t step_delay_us = movement_step_delay_us();

  movementLimitSwitchRefreshAll();

  /* Manual X/Z inputs are logical absolute targets; motor counters use compensated targets. */
  xmm = movementClampVerticalTarget(xmm);
  zmm = movementClampHorizontalTarget(zmm);
  long targetStepsX = vertical_compensated_target_steps(xmm);
  long diffX = targetStepsX - CurrentStep1;

  if (diffX != 0)
  {
    long steps = abs_long(diffX);
    long moved_steps = 0;
    bool moving_positive = (diffX > 0);
    bool released_once = (!vertical_limit_active()) && (!vertical_far_limit_active());

    enable_vertical(true);

    if (moving_positive)
    {
      set_vertical_dir_positive();
    }
    else
    {
      set_vertical_dir_negative();
    }

    for (long i = 0; i < steps; i++)
    {
      movementLimitSwitchRefreshAll();
      if (limit_should_stop_axis(moving_positive,
                                 released_once,
                                 vertical_limit_active(),
                                 vertical_far_limit_active()))
      {
        MovementAlarm_RecordVerticalStop(vertical_top_left_limit_active(),
                                         vertical_top_right_limit_active(),
                                         vertical_bottom_left_limit_active(),
                                         vertical_bottom_right_limit_active());
        alarm_stopped = true;
        break;
      }

      /*
       * What: generate one manual-move STEP pulse for the four vertical drivers.
       * How: force STEP1-STEP4 low, wait, then force STEP1-STEP4 high in the same cycle.
       * Why: explicit per-pin writes make the manual pulse train easier to verify on hardware.
       */
      write_pin(STEP1_Port, STEP1_Pin, GPIO_PIN_RESET);
      write_pin(STEP2_Port, STEP2_Pin, GPIO_PIN_RESET);
      write_pin(STEP3_Port, STEP3_Pin, GPIO_PIN_RESET);
      write_pin(STEP4_Port, STEP4_Pin, GPIO_PIN_RESET);
      delay_us(step_delay_us);

      write_pin(STEP1_Port, STEP1_Pin, GPIO_PIN_SET);
      write_pin(STEP2_Port, STEP2_Pin, GPIO_PIN_SET);
      write_pin(STEP3_Port, STEP3_Pin, GPIO_PIN_SET);
      write_pin(STEP4_Port, STEP4_Pin, GPIO_PIN_SET);
      delay_us(step_delay_us);
      moved_steps++;

      movementLimitSwitchRefreshAll();
      if ((!vertical_limit_active()) && (!vertical_far_limit_active()))
      {
        released_once = true;
      }

      if (limit_should_stop_axis(moving_positive,
                                 released_once,
                                 vertical_limit_active(),
                                 vertical_far_limit_active()))
      {
        MovementAlarm_RecordVerticalStop(vertical_top_left_limit_active(),
                                         vertical_top_right_limit_active(),
                                         vertical_bottom_left_limit_active(),
                                         vertical_bottom_right_limit_active());
        alarm_stopped = true;
        break;
      }

      if ((i % 100L) == 0L)
      {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    write_pin(STEP1_Port, STEP1_Pin, GPIO_PIN_RESET);
    write_pin(STEP2_Port, STEP2_Pin, GPIO_PIN_RESET);
    write_pin(STEP3_Port, STEP3_Pin, GPIO_PIN_RESET);
    write_pin(STEP4_Port, STEP4_Pin, GPIO_PIN_RESET);
    CurrentStep1 += moving_positive ? moved_steps : -moved_steps;
    if (moved_steps == steps)
    {
      CurrentStep1 = targetStepsX;
    }
    enable_vertical(false);
  }

  if (alarm_stopped)
  {
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(1));

  /* The horizontal axis uses the same absolute-target model with its own counter. */
  long targetStepsZ = horizontal_compensated_target_steps(zmm);
  long diffZ = targetStepsZ - CurrentStep2;

  if (diffZ != 0)
  {
    long steps = abs_long(diffZ);
    long moved_steps = 0;
    bool moving_positive = (diffZ > 0);
    bool released_once = (!horizontal_limit_active()) && (!horizontal_far_limit_active());

    enable_horizontal(true);

    if (moving_positive)
    {
      set_horizontal_dir_positive();
    }
    else
    {
      set_horizontal_dir_negative();
    }

    for (long i = 0; i < steps; i++)
    {
      movementLimitSwitchRefreshAll();
      if (limit_should_stop_axis(moving_positive,
                                 released_once,
                                 horizontal_limit_active(),
                                 horizontal_far_limit_active()))
      {
        MovementAlarm_RecordHorizontalStop(horizontal_exterior_left_limit_active(),
                                           horizontal_exterior_right_limit_active(),
                                           horizontal_interior_left_limit_active(),
                                           horizontal_interior_right_limit_active());
        alarm_stopped = true;
        break;
      }

      /*
       * What: generate one manual-move STEP pulse for the two horizontal drivers.
       * How: force STEP5-STEP6 low, wait, then force STEP5-STEP6 high in the same cycle.
       * Why: keeps the manual X/Z pulse style identical and easy to probe.
       */
      write_pin(STEP5_Port, STEP5_Pin, GPIO_PIN_RESET);
      write_pin(STEP6_Port, STEP6_Pin, GPIO_PIN_RESET);
      delay_us(step_delay_us);

      write_pin(STEP5_Port, STEP5_Pin, GPIO_PIN_SET);
      write_pin(STEP6_Port, STEP6_Pin, GPIO_PIN_SET);
      delay_us(step_delay_us);
      moved_steps++;

      movementLimitSwitchRefreshAll();
      if ((!horizontal_limit_active()) && (!horizontal_far_limit_active()))
      {
        released_once = true;
      }

      if (limit_should_stop_axis(moving_positive,
                                 released_once,
                                 horizontal_limit_active(),
                                 horizontal_far_limit_active()))
      {
        MovementAlarm_RecordHorizontalStop(horizontal_exterior_left_limit_active(),
                                           horizontal_exterior_right_limit_active(),
                                           horizontal_interior_left_limit_active(),
                                           horizontal_interior_right_limit_active());
        alarm_stopped = true;
        break;
      }

      if ((i % 100L) == 0L)
      {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    write_pin(STEP5_Port, STEP5_Pin, GPIO_PIN_RESET);
    write_pin(STEP6_Port, STEP6_Pin, GPIO_PIN_RESET);
    CurrentStep2 += moving_positive ? moved_steps : -moved_steps;
    if (moved_steps == steps)
    {
      CurrentStep2 = targetStepsZ;
    }
    enable_horizontal(false);
  }

  return !alarm_stopped;
}

/*
 * What: run a complete homing cycle and reset software position to zero.
 * How: first touch stops each whole axis when any endstop on that axis triggers,
 *      backs off, then second touch homes each motor/patin independently by EXTI state.
 * Why: the first touch finds the reference area safely, and the second touch avoids
 *      pushing a switch that has already been reached.
 */
void GoHomePair(float *posX, float *posZ)
{
  bool xHomingReached = false;
  bool zHomingReached = false;
  long safeSteps = 0;
  uint32_t home_step_delay_us = movement_home_step_delay_us();
  long max_x_homing_steps = vertical_homing_max_steps();
  long max_z_homing_steps = horizontal_homing_max_steps();

  movementLimitSwitchRefreshAll();

  if (all_vertical_limits_active() && all_horizontal_limits_active())
  {
    CurrentStep1 = 0;
    CurrentStep2 = 0;

    if (posX != NULL)
    {
      *posX = 0.0f;
    }

    if (posZ != NULL)
    {
      *posZ = 0.0f;
    }

    return;
  }

  xHomingReached = vertical_limit_active();
  zHomingReached = horizontal_limit_active();

  enable_vertical(true);
  enable_horizontal(true);

  set_vertical_dir_negative();
  set_horizontal_dir_negative();

  safeSteps = 0;
  while (!xHomingReached && (safeSteps < max_x_homing_steps))
  {
    /*
     * What: first vertical touch.
     * How: all vertical motors pulse together until any vertical endstop interrupt is active.
     * Why: this coarse pass only finds the home area before the precise second touch.
     */
    if (vertical_limit_active())
    {
      xHomingReached = true;
      break;
    }

    set_vertical_step(GPIO_PIN_RESET);
    delay_us(home_step_delay_us);

    set_vertical_step(GPIO_PIN_SET);

    delay_us(home_step_delay_us);

    safeSteps++;

    if (vertical_limit_active())
    {
      xHomingReached = true;
    }

    if ((safeSteps % 100L) == 0L)
    {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  set_vertical_step(GPIO_PIN_RESET);
  if (xHomingReached)
  {
    enable_vertical(false);
  }

  safeSteps = 0;
  while (!zHomingReached && (safeSteps < max_z_homing_steps))
  {
    /*
     * What: first horizontal touch.
     * How: both horizontal patins pulse together until either horizontal endstop is active.
     * Why: this coarse pass matches the ESP32 behavior before the precise second touch.
     */
    if (horizontal_limit_active())
    {
      zHomingReached = true;
      break;
    }

    set_horizontal_step(GPIO_PIN_RESET);
    delay_us(home_step_delay_us);

    set_horizontal_step(GPIO_PIN_SET);

    delay_us(home_step_delay_us);

    safeSteps++;

    if (horizontal_limit_active())
    {
      zHomingReached = true;
    }

    if ((safeSteps % 100L) == 0L)
    {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  set_horizontal_step(GPIO_PIN_RESET);
  if (zHomingReached)
  {
    enable_horizontal(false);
  }

  if (xHomingReached && zHomingReached)
  {
    BackoffAll(BACKOFF_STEPS, (long)home_step_delay_us);
    SecondTouchPair((long)MOVEMENT_SECOND_TOUCH_STEP_DELAY_US);
  }

  enable_vertical(false);
  enable_horizontal(false);

  CurrentStep1 = 0;
  CurrentStep2 = 0;

  if (posX != NULL)
  {
    *posX = 0.0f;
  }

  if (posZ != NULL)
  {
    *posZ = 0.0f;
  }
}

/*
 * What: perform the slow second touch after backing off the switches.
 * How: each loop reads every endstop before each STEP pulse and only pulses motors
 *      whose own endstop is still inactive.
 * Why: the final reference must be gentle and independent so an active switch cannot
 *      be pushed again while another motor is still searching.
 */
static void SecondTouchPair(long speed_us)
{
  bool verticalDone = false;
  long safeSteps = 0;

  movementLimitSwitchRefreshAll();

  set_vertical_dir_negative();
  set_horizontal_dir_negative();

  delay_us((uint32_t)DIR_CHANGE_DELAY_US);

  enable_vertical(true);

  while (!verticalDone && (safeSteps < MAX_VERTICAL_SECOND_TOUCH_STEPS))
  {
    bool moveMXLI;
    bool moveMXLE;
    bool moveMXRI;
    bool moveMXRE;

    movementLimitSwitchRefreshAll();

    if (all_vertical_limits_active())
    {
      verticalDone = true;
      break;
    }

    moveMXLI = !xli_limit_active();
    moveMXLE = !xle_limit_active();
    moveMXRI = !xri_limit_active();
    moveMXRE = !xre_limit_active();

    set_vertical_step(GPIO_PIN_RESET);
    delay_us((uint32_t)speed_us);

    if (moveMXLI) write_pin(STEP1_Port, STEP1_Pin, GPIO_PIN_SET);
    if (moveMXLE) write_pin(STEP2_Port, STEP2_Pin, GPIO_PIN_SET);
    if (moveMXRI) write_pin(STEP3_Port, STEP3_Pin, GPIO_PIN_SET);
    if (moveMXRE) write_pin(STEP4_Port, STEP4_Pin, GPIO_PIN_SET);

    delay_us((uint32_t)speed_us);
    safeSteps++;
  }

  set_vertical_step(GPIO_PIN_RESET);
  enable_vertical(false);

  enable_horizontal(true);
  safeSteps = 0;
  while (safeSteps < MAX_HORIZONTAL_SECOND_TOUCH_STEPS)
  {
    bool moveZL;
    bool moveZR;

    movementLimitSwitchRefreshAll();

    if (all_horizontal_limits_active())
    {
      break;
    }

    /*
     * What: second horizontal touch with falling-edge recovery.
     * How: each patin is pulsed only while its current EXTI state says "not pressed".
     * Why: if a switch releases or bounces low, the next pulse is allowed again.
     */
    moveZL = !zl_limit_active();
    moveZR = !zr_limit_active();

    if (!moveZL && !moveZR)
    {
      break;
    }

    set_horizontal_step(GPIO_PIN_RESET);
    delay_us((uint32_t)speed_us);

    if (moveZR) write_pin(STEP5_Port, STEP5_Pin, GPIO_PIN_SET);
    if (moveZL) write_pin(STEP6_Port, STEP6_Pin, GPIO_PIN_SET);

    delay_us((uint32_t)speed_us);
    safeSteps++;
  }

  set_horizontal_step(GPIO_PIN_RESET);
  set_vertical_step(GPIO_PIN_RESET);

  enable_horizontal(false);
}

/*
 * What: move all axes away from the endstops before the second touch.
 * How: all motors in the same axis receive simultaneous STEP pulses while directions
 *      are set away from home.
 * Why: simultaneous backoff keeps left/right and ZL/ZR aligned instead of advancing
 *      one motor more than the others during the release movement.
 */
static void BackoffAll(int steps, long speed_us)
{
  enable_vertical(true);
  enable_horizontal(true);

  set_vertical_dir_positive();
  set_horizontal_dir_positive();

  for (int i = 0; i < steps; i++)
  {
    set_vertical_step(GPIO_PIN_RESET);
    delay_us((uint32_t)speed_us);

    set_vertical_step(GPIO_PIN_SET);
    delay_us((uint32_t)speed_us);
  }

  set_vertical_step(GPIO_PIN_RESET);

  for (int i = 0; i < steps; i++)
  {
    set_horizontal_step(GPIO_PIN_RESET);
    delay_us((uint32_t)speed_us);

    set_horizontal_step(GPIO_PIN_SET);
    delay_us((uint32_t)speed_us);
  }

  set_horizontal_step(GPIO_PIN_RESET);

  enable_vertical(false);
  enable_horizontal(false);
  movementLimitSwitchRefreshAll();
}
