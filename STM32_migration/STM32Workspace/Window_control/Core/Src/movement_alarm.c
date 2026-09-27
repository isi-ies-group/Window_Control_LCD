#include "movement_alarm.h"

#include "global_structs.h"

static void MovementAlarm_CountIfActive(bool active, volatile uint32_t *counter)
{
  if (active)
  {
    (*counter)++;
    g_any_movement_alarm = true;
  }
}

void MovementAlarm_ResetAll(void)
{
  g_vertical_top_right_alarm = 0U;
  g_vertical_top_left_alarm = 0U;
  g_horizontal_interior_left_alarm = 0U;
  g_horizontal_interior_right_alarm = 0U;
  g_vertical_bottom_left_alarm = 0U;
  g_vertical_bottom_right_alarm = 0U;
  g_horizontal_exterior_left_alarm = 0U;
  g_horizontal_exterior_right_alarm = 0U;
  g_any_movement_alarm = false;
}

void MovementAlarm_RecordVerticalStop(bool top_left_active,
                                      bool top_right_active,
                                      bool bottom_left_active,
                                      bool bottom_right_active)
{
  /*
   * What: count vertical limit hits caused by normal movement, not homing.
   * How: movement.cpp calls this only after move() decides an endstop blocked the command.
   * Why: alarm counters show real protection events without treating homing as a fault.
   */
  MovementAlarm_CountIfActive(top_left_active, &g_vertical_top_left_alarm);
  MovementAlarm_CountIfActive(top_right_active, &g_vertical_top_right_alarm);
  MovementAlarm_CountIfActive(bottom_left_active, &g_vertical_bottom_left_alarm);
  MovementAlarm_CountIfActive(bottom_right_active, &g_vertical_bottom_right_alarm);
}

void MovementAlarm_RecordHorizontalStop(bool exterior_left_active,
                                        bool exterior_right_active,
                                        bool interior_left_active,
                                        bool interior_right_active)
{
  /*
   * What: count horizontal limit hits caused by normal movement, not homing.
   * How: exterior limits are the homing side and interior limits are the far/window side.
   * Why: the test can identify which patin stopped the movement.
   */
  MovementAlarm_CountIfActive(exterior_left_active, &g_horizontal_exterior_left_alarm);
  MovementAlarm_CountIfActive(exterior_right_active, &g_horizontal_exterior_right_alarm);
  MovementAlarm_CountIfActive(interior_left_active, &g_horizontal_interior_left_alarm);
  MovementAlarm_CountIfActive(interior_right_active, &g_horizontal_interior_right_alarm);
}
