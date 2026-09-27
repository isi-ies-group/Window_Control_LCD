#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  STDBY,
  AUTO_MODE,
  MANUAL,
  EPH_INPUT,
  STARTUP,
  ALARM_RECOVERY,
  FAULT_LOCKOUT,
} States;

typedef enum
{
  end_eph_input,
  begin_eph_input,
  submit_eph_input,
  begin_manual,
  submit_manual_goto,
  submit_home,
  end_manual,
  toggle_auto_mode,
  movement_alarm_triggered,
  reset_alarm_request,
} Events;

extern States thisSt;
extern States nextSt;

void initFSM(void);
const char *stateToText(States state);
States fsmProcess(Events event, bool auto_running);
void changeState(States newState);
bool fsmPostEvent(Events event);
bool fsmPostMovementAlarm(float target_x, float target_z);
States fsmGetState(void);
States fsmGetPersistentState(void);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_H */
