#ifndef MOVEMENT_ALARM_H
#define MOVEMENT_ALARM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  MOVEMENT_ALARM_VERTICAL_TOP_RIGHT = 0,
  MOVEMENT_ALARM_VERTICAL_TOP_LEFT,
  MOVEMENT_ALARM_HORIZONTAL_INTERIOR_LEFT,
  MOVEMENT_ALARM_HORIZONTAL_INTERIOR_RIGHT,
  MOVEMENT_ALARM_VERTICAL_BOTTOM_LEFT,
  MOVEMENT_ALARM_VERTICAL_BOTTOM_RIGHT,
  MOVEMENT_ALARM_HORIZONTAL_EXTERIOR_LEFT,
  MOVEMENT_ALARM_HORIZONTAL_EXTERIOR_RIGHT,
  MOVEMENT_ALARM_COUNT
} MovementAlarmId;

typedef struct
{
  uint32_t vertical_top_right;
  uint32_t vertical_top_left;
  uint32_t horizontal_interior_left;
  uint32_t horizontal_interior_right;
  uint32_t vertical_bottom_left;
  uint32_t vertical_bottom_right;
  uint32_t horizontal_exterior_left;
  uint32_t horizontal_exterior_right;
} MovementAlarmSnapshot;

void MovementAlarm_Update(MovementAlarmId alarm_id, uint8_t active);
void MovementAlarm_ResetAll(void);
void MovementAlarm_GetSnapshot(MovementAlarmSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* MOVEMENT_ALARM_H */
