#ifndef MOVEMENT_TASK_H
#define MOVEMENT_TASK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void initMovementTask(void);

void requestMove(void);
void requestHome(void);
bool movementTaskIsBusy(void);

#ifdef __cplusplus
}
#endif

#endif /* MOVEMENT_TASK_H */
