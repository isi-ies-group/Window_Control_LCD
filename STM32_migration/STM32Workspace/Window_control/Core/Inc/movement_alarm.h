#ifndef MOVEMENT_ALARM_H
#define MOVEMENT_ALARM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void MovementAlarm_ResetAll(void);
void MovementAlarm_RecordVerticalStop(bool top_left_active,
                                      bool top_right_active,
                                      bool bottom_left_active,
                                      bool bottom_right_active);
void MovementAlarm_RecordHorizontalStop(bool exterior_left_active,
                                        bool exterior_right_active,
                                        bool interior_left_active,
                                        bool interior_right_active);

#ifdef __cplusplus
}
#endif

#endif /* MOVEMENT_ALARM_H */
