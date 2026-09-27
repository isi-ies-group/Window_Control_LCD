#include "movement_alarm.h"

#include <stddef.h>

#include "global_structs.h"

static uint8_t alarm_active_latch[MOVEMENT_ALARM_COUNT] = {0U};

static volatile uint32_t *MovementAlarm_CounterPtr(MovementAlarmId alarm_id)
{
  switch (alarm_id)
  {
    case MOVEMENT_ALARM_VERTICAL_TOP_RIGHT:
      return &Vertical_top_right_alarm;

    case MOVEMENT_ALARM_VERTICAL_TOP_LEFT:
      return &Vertical_top_left_alarm;

    case MOVEMENT_ALARM_HORIZONTAL_INTERIOR_LEFT:
      return &horizontal_interior_left_alarm;

    case MOVEMENT_ALARM_HORIZONTAL_INTERIOR_RIGHT:
      return &horizontal_interior_right_alarm;

    case MOVEMENT_ALARM_VERTICAL_BOTTOM_LEFT:
      return &vertical_bottom_left_alarm;

    case MOVEMENT_ALARM_VERTICAL_BOTTOM_RIGHT:
      return &vertical_bottom_right_alarm;

    case MOVEMENT_ALARM_HORIZONTAL_EXTERIOR_LEFT:
      return &horizontal_exterior_left_alarm;

    case MOVEMENT_ALARM_HORIZONTAL_EXTERIOR_RIGHT:
      return &horizontal_exterior_right_alarm;

    default:
      return NULL;
  }
}

void MovementAlarm_Update(MovementAlarmId alarm_id, uint8_t active)
{
  volatile uint32_t *counter;

  if ((uint32_t)alarm_id >= (uint32_t)MOVEMENT_ALARM_COUNT)
  {
    return;
  }

  counter = MovementAlarm_CounterPtr(alarm_id);
  if (counter == NULL)
  {
    return;
  }

  /*
   * Count only the inactive-to-active transition.
   * This keeps a held endstop from increasing the alarm on every software read.
   */
  if (active != 0U)
  {
    if (alarm_active_latch[alarm_id] == 0U)
    {
      (*counter)++;
      g_any_movement_alarm = true;
      alarm_active_latch[alarm_id] = 1U;
    }
  }
  else
  {
    alarm_active_latch[alarm_id] = 0U;
  }
}

void MovementAlarm_ResetAll(void)
{
  uint32_t i;

  Vertical_top_right_alarm = 0U;
  Vertical_top_left_alarm = 0U;
  horizontal_interior_left_alarm = 0U;
  horizontal_interior_right_alarm = 0U;
  vertical_bottom_left_alarm = 0U;
  vertical_bottom_right_alarm = 0U;
  horizontal_exterior_left_alarm = 0U;
  horizontal_exterior_right_alarm = 0U;
  g_any_movement_alarm = false;

  for (i = 0U; i < (uint32_t)MOVEMENT_ALARM_COUNT; i++)
  {
    alarm_active_latch[i] = 0U;
  }
}

void MovementAlarm_GetSnapshot(MovementAlarmSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }

  snapshot->vertical_top_right = Vertical_top_right_alarm;
  snapshot->vertical_top_left = Vertical_top_left_alarm;
  snapshot->horizontal_interior_left = horizontal_interior_left_alarm;
  snapshot->horizontal_interior_right = horizontal_interior_right_alarm;
  snapshot->vertical_bottom_left = vertical_bottom_left_alarm;
  snapshot->vertical_bottom_right = vertical_bottom_right_alarm;
  snapshot->horizontal_exterior_left = horizontal_exterior_left_alarm;
  snapshot->horizontal_exterior_right = horizontal_exterior_right_alarm;
}
