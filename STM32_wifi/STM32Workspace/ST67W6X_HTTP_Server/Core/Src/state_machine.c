#include "state_machine.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "freertos_tickless.h"
#include "gps.h"
#include "hello.h"
#include "autoMode.h"
#include "eph_input_mode.h"
#include "global_structs.h"
#include "manual_mode.h"
#include "movement_alarm.h"
#include "movement_task.h"
#include "storage.h"

#define FSM_QUEUE_LENGTH      8U
#define FSM_TASK_STACK_SIZE   4096U
#define FSM_TASK_PRIORITY     23U
#define FSM_AUTO_PERIOD_MS    5000U
#define FSM_AUTO_HOME_PERIOD_TICKS (pdMS_TO_TICKS(60000U) * 120U)
#define STORAGE_TIME_SAVE_PERIOD_MS 10000U
#define FSM_STARTUP_GPS_TIMEOUT_MS  300000U
#define FSM_STARTUP_HOME_DELAY_MS   5000U
#define FSM_STARTUP_POLL_MS         100U
#define FSM_ALARM_POLL_MS           100U
#define FSM_SAFE_PARK_X_MM          10.0f
#define FSM_SAFE_PARK_Z_MM          10.0f

typedef enum
{
  STARTUP_PHASE_HOME,
  STARTUP_PHASE_GPS_SYNC,
} StartupPhase;

typedef enum
{
  ALARM_RECOVERY_PHASE_HOME,
  ALARM_RECOVERY_PHASE_RETRY_MOVE,
} AlarmRecoveryPhase;

typedef enum
{
  FAULT_LOCKOUT_PHASE_HOME,
  FAULT_LOCKOUT_PHASE_SAFE_MOVE,
  FAULT_LOCKOUT_PHASE_WAIT_RESET,
} FaultLockoutPhase;

/* Current and next FSM states. Start in standby until persistent memory exists. */
States thisSt = STDBY;
States nextSt = STDBY;

static QueueHandle_t fsmQueue = NULL;      /* Events queue */
static TaskHandle_t fsmTaskHandle = NULL;  /* Avoid task replication */
static States startupReturnState = STDBY;  /* Persisted state to enter after startup checks. */
static StartupPhase startupPhase = STARTUP_PHASE_HOME;
static bool startupHomeDelayStarted = false;
static bool startupHomeRequested = false;
static bool startupGpsRequested = false;
static TickType_t startupHomeDelayStartTick = 0U;
static TickType_t startupGpsStartTick = 0U;
static States alarmReturnState = STDBY;
static float alarmRecoveryTargetX = 0.0f;
static float alarmRecoveryTargetZ = 0.0f;
static float pendingAlarmTargetX = 0.0f;
static float pendingAlarmTargetZ = 0.0f;
static bool pendingAlarmTargetValid = false;
static AlarmRecoveryPhase alarmRecoveryPhase = ALARM_RECOVERY_PHASE_HOME;
static FaultLockoutPhase faultLockoutPhase = FAULT_LOCKOUT_PHASE_HOME;
static bool alarmCommandRequested = false;
static bool faultLockoutResetRequested = false;

static void fsmTask(void *argument);
static void fsmPrepareStartup(States return_state);
static bool fsmRunStartup(TickType_t now_tick);
static bool fsmRunAlarmRecovery(void);
static bool fsmRunFaultLockout(void);
static void fsmFinishStartup(void);
static void fsmHandleMovementAlarmEvent(void);
static bool stateIsPersistent(States state);
static bool stateIsTransient(States state);
static bool stateTransitionIsValid(States currentState, States newState);
static TickType_t ticksUntilPeriod(TickType_t now, TickType_t last, TickType_t period);
static TickType_t fsmWaitTicks(TickType_t now, TickType_t last_auto_tick, TickType_t last_time_save_tick);

void initFSM(void)
{
  /*
   * What: initialize the finite-state-machine task.
   * How: remembers the persisted state, then starts in a RAM-only STARTUP state.
   * Why: boot must home and request GPS sync without blocking WiFi/HTTP startup.
   */
  if (!stateIsPersistent(thisSt))
  {
    thisSt = STDBY;
  }

  fsmPrepareStartup(thisSt);

  thisSt = STARTUP;
  nextSt = STARTUP;
  auto_on = false;

  if (fsmQueue == NULL)
  {
    fsmQueue = xQueueCreate(FSM_QUEUE_LENGTH, sizeof(Events));
  }

  if ((fsmQueue != NULL) && (fsmTaskHandle == NULL))
  {
    /* ST defines stack sizes in bytes, but xTaskCreate expects stack words. */
    (void)xTaskCreate(fsmTask,
                      "FSM_Task",
                      FSM_TASK_STACK_SIZE >> 2U,
                      NULL,
                      FSM_TASK_PRIORITY,
                      &fsmTaskHandle);
  }
}

bool fsmPostEvent(Events event)
{
  /*
   * What: publish a web/application event to the FSM.
   * How: sends the event into the FreeRTOS queue without blocking.
   * Why: HTTP handlers should request transitions, not execute mode logic directly.
   */
  if (fsmQueue == NULL)
  {
    return false;
  }

  return (xQueueSend(fsmQueue, &event, 0U) == pdTRUE);
}

bool fsmPostMovementAlarm(float target_x, float target_z)
{
  /*
   * What: report that move() stopped on an unexpected endstop.
   * How: stores the failed target and posts a normal FSM event.
   * Why: recovery policy belongs to the FSM, while movement_task only reports facts.
   */
  pendingAlarmTargetX = target_x;
  pendingAlarmTargetZ = target_z;
  pendingAlarmTargetValid = true;

  return fsmPostEvent(movement_alarm_triggered);
}

States fsmGetState(void)
{
  /*
   * What: return the current state for status/UI code.
   * How: reads thisSt, which is updated only by the FSM logic.
   * Why: the web page needs state visibility to enable/disable controls correctly.
   */
  return thisSt;
}

States fsmGetPersistentState(void)
{
  /*
   * What: expose the state that is safe to store in flash.
   * How: maps transient STARTUP back to the state loaded from flash.
   * Why: a reboot must never persist STARTUP as the user's operating mode.
   */
  if (thisSt == STARTUP)
  {
    return startupReturnState;
  }

  if ((thisSt == ALARM_RECOVERY) || (thisSt == FAULT_LOCKOUT))
  {
    return alarmReturnState;
  }

  return thisSt;
}

const char *stateToText(States state)
{
  /*
   * What: convert an FSM enum into web-readable text.
   * How: maps each known state to a stable string.
   * Why: status JSON and debugging are easier with names instead of numeric values.
   */
  switch (state)
  {
    case STDBY:
      return "STDBY";

    case AUTO_MODE:
      return "AUTO_MODE";

    case MANUAL:
      return "MANUAL";

    case EPH_INPUT:
      return "EPH_INPUT";

    case STARTUP:
      return "STARTUP";

    case ALARM_RECOVERY:
      return "ALARM_RECOVERY";

    case FAULT_LOCKOUT:
      return "FAULT_LOCKOUT";

    default:
      return "UNKNOWN";
  }
}

States fsmProcess(Events event, bool auto_running)
{
  /*
   * What: translate one event into the requested next state.
   * How: maps begin/end/submit/toggle events and delegates validation to changeState().
   * Why: transition decisions stay centralized instead of scattered through HTTP code.
   */
  States requestedState = thisSt;

  /* Kept for compatibility with the first FSM version; thisSt is the source of truth now. */
  (void)auto_running;

  if (event == movement_alarm_triggered)
  {
    fsmHandleMovementAlarmEvent();
    return thisSt;
  }

  if (event == reset_alarm_request)
  {
    if (thisSt == FAULT_LOCKOUT)
    {
      faultLockoutResetRequested = true;
    }

    return thisSt;
  }

  if (stateIsTransient(thisSt))
  {
    return thisSt;
  }

  switch (event)
  {
    case end_eph_input:
    case end_manual:
      requestedState = STDBY;
      break;

    case begin_eph_input:
    case submit_eph_input:
      requestedState = EPH_INPUT;
      break;

    case begin_manual:
    case submit_manual_goto:
      requestedState = MANUAL;
      break;

    case toggle_auto_mode:
      requestedState = (thisSt == AUTO_MODE) ? STDBY : AUTO_MODE;
      break;

    default:
      requestedState = thisSt;
      break;
  }

  changeState(requestedState);

  return thisSt;
}

void changeState(States newState)
{
  /*
   * What: apply a validated FSM state transition.
   * How: updates thisSt/nextSt, mirrors AUTO_MODE into auto_on, and persists changes.
   * Why: legacy code still reads auto_on while the FSM remains the source of truth.
   */
  if (stateTransitionIsValid(thisSt, newState))
  {
    States previousState = thisSt;

    nextSt = newState;
    thisSt = nextSt;
    auto_on = (thisSt == AUTO_MODE);

    if ((previousState != thisSt) && (previousState != STARTUP) && stateIsPersistent(thisSt))
    {
      /* Persist state transitions so the next boot resumes from the same mode. */
      (void)saveState();
    }
  }
}

static void fsmTask(void *argument)
{
  /*
   * What: run the FSM loop as a FreeRTOS task.
   * How: consumes queued events, launches subroutines, and ticks automode every 5 seconds.
   * Why: modes must progress without putting blocking logic in main while(1) or HTTP.
  */
  Events event;
  TickType_t last_auto_tick = xTaskGetTickCount();
  TickType_t last_auto_home_tick = xTaskGetTickCount();
  TickType_t last_time_save_tick = xTaskGetTickCount();

  /* FreeRTOS task signature requires this argument, even if we do not use it. */
  (void)argument;

  for (;;)
  {
    TickType_t now_tick = xTaskGetTickCount();
    TickType_t queue_wait_ticks = fsmWaitTicks(now_tick, last_auto_tick, last_time_save_tick);

    /*
     * What: sleep the FSM task until the next real deadline or web event.
     * How: STANDBY/MANUAL/EPH wait mostly for events/storage, AUTO waits for the next 5 s cycle.
     * Why: periodic 100 ms wakeups were preventing useful tickless sleep while the app was idle.
     */
    if (xQueueReceive(fsmQueue, &event, queue_wait_ticks) == pdTRUE)
    {
      States previousState = thisSt;
      States currentState = fsmProcess(event, auto_on);

      if ((!stateIsTransient(previousState)) && (event == submit_manual_goto))
      {
        /* HTTP has already loaded g_x_target/g_z_target; movement_task runs the blocking pulses. */
        manualModeGoto(g_x_target, g_z_target);
        requestMove();
      }
      else if ((!stateIsTransient(previousState)) && (event == submit_home))
      {
        /* Homing is requested by the web layer and executed by the movement task. */
        requestHome();
      }
      else if ((!stateIsTransient(previousState)) && (event == submit_eph_input))
      {
        /* Ephemeris mode calculates AOI and updates g_x_target/g_z_target. */
        if (ephInputMode())
        {
          /* movement_task owns the blocking step pulses, so the FSM stays responsive. */
          requestMove();
        }
      }

      if ((previousState != AUTO_MODE) && (currentState == AUTO_MODE))
      {
        last_auto_tick = xTaskGetTickCount();
        last_auto_home_tick = last_auto_tick;
      }
    }

    now_tick = xTaskGetTickCount();

    if (thisSt == STARTUP)
    {
      if (fsmRunStartup(now_tick))
      {
        now_tick = xTaskGetTickCount();
        last_auto_tick = now_tick;
        last_auto_home_tick = now_tick;
      }

      last_time_save_tick = xTaskGetTickCount();
    }
    else if (thisSt == ALARM_RECOVERY)
    {
      if (fsmRunAlarmRecovery())
      {
        now_tick = xTaskGetTickCount();
        last_auto_tick = now_tick;
        last_auto_home_tick = now_tick;
      }

      last_time_save_tick = xTaskGetTickCount();
    }
    else if (thisSt == FAULT_LOCKOUT)
    {
      (void)fsmRunFaultLockout();
      last_time_save_tick = xTaskGetTickCount();
    }

    now_tick = xTaskGetTickCount();

    if ((!stateIsTransient(thisSt)) &&
        ((now_tick - last_time_save_tick) >= pdMS_TO_TICKS(STORAGE_TIME_SAVE_PERIOD_MS)))
    {
      /*
       * What: periodically persist the local RTC shown in the web status.
       * How: storage reads RTC and rewrites the single flash-backed app record.
       * Why: after reset/power loss the clock should not jump back to the default time.
       */
      (void)saveRtcTime();
      last_time_save_tick = now_tick;
    }

    if (thisSt == AUTO_MODE)
    {
      /* Automatic mode runs periodically while the FSM remains in AUTO_MODE. */
      if ((now_tick - last_auto_tick) >= pdMS_TO_TICKS(FSM_AUTO_PERIOD_MS))
      {
        bool movement_busy = movementTaskIsBusy();

        if ((now_tick - last_auto_home_tick) >= FSM_AUTO_HOME_PERIOD_TICKS)
        {
          if (!movement_busy)
          {
            /*
             * What: periodically re-reference the mechanics while AUTO is active.
             * How: queues HOME instead of a normal auto target every two hours.
             * Why: small automatic corrections can accumulate mechanical error over time.
             */
            requestHome();
            last_auto_home_tick = now_tick;
          }
        }
        else if (!movement_busy)
        {
          /*
           * What: protect one automatic cycle from low-power entry.
           * How: block tickless while autoMode() reads time, calculates target angles and queues movement.
           * Why: an automode cycle should not be split by Sleep/Stop halfway through its calculation.
           */
          DisableSuppressTicksAndSleep(1UL << CFG_TICKLESS_AUTOMODE_ID);
          autoMode();
          EnableSuppressTicksAndSleep(1UL << CFG_TICKLESS_AUTOMODE_ID);
          auto_counter++;
        }

        last_auto_tick = now_tick;
      }
    }
    else
    {
      last_auto_tick = now_tick;
      last_auto_home_tick = now_tick;
    }
  }
}

static void fsmPrepareStartup(States return_state)
{
  /*
   * What: prepare the transient STARTUP sequence.
   * How: stores the state to restore, resets startup phases and clears movement alarms.
   * Why: reset-alarm and reboot paths must re-enter the system through one clean point.
   */
  if (!stateIsPersistent(return_state))
  {
    return_state = STDBY;
  }

  startupReturnState = return_state;
  startupPhase = STARTUP_PHASE_HOME;
  startupHomeDelayStarted = false;
  startupHomeRequested = false;
  startupGpsRequested = false;
  startupHomeDelayStartTick = 0U;
  startupGpsStartTick = 0U;
  pendingAlarmTargetValid = false;
  faultLockoutResetRequested = false;

  MovementAlarm_ResetAll();
}

static bool fsmRunStartup(TickType_t now_tick)
{
  /*
   * What: run the boot-only startup sequence.
   * How: waits for power rails, queues HOME, waits until movement is idle, requests GPS sync, then waits for sync or timeout.
   * Why: mechanics and RTC setup belong to the FSM task, not to blocking code in main_app().
   */
  if (thisSt != STARTUP)
  {
    return false;
  }

  switch (startupPhase)
  {
    case STARTUP_PHASE_HOME:
      if (!startupHomeDelayStarted)
      {
        startupHomeDelayStarted = true;
        startupHomeDelayStartTick = now_tick;
        break;
      }

      if ((now_tick - startupHomeDelayStartTick) < pdMS_TO_TICKS(FSM_STARTUP_HOME_DELAY_MS))
      {
        break;
      }

      if (!startupHomeRequested)
      {
        requestHome();
        startupHomeRequested = true;
      }
      else if (!movementTaskIsBusy())
      {
        GPS_Task_RequestTimeSync();
        startupGpsRequested = true;
        startupGpsStartTick = now_tick;
        startupPhase = STARTUP_PHASE_GPS_SYNC;
      }
      break;

    case STARTUP_PHASE_GPS_SYNC:
      if (!startupGpsRequested)
      {
        GPS_Task_RequestTimeSync();
        startupGpsRequested = true;
        startupGpsStartTick = now_tick;
      }

      if ((g_gps_rtc_synced != 0U) ||
          ((now_tick - startupGpsStartTick) >= pdMS_TO_TICKS(FSM_STARTUP_GPS_TIMEOUT_MS)))
      {
        fsmFinishStartup();
        return true;
      }
      break;

    default:
      fsmFinishStartup();
      return true;
  }

  return false;
}

static bool fsmRunAlarmRecovery(void)
{
  /*
   * What: recover from the first unexpected endstop hit.
   * How: homes, retries the failed target once, then returns to the original mode on success.
   * Why: one alarm can be a mechanical slip; a repeated alarm means the process must lock out.
   */
  if (thisSt != ALARM_RECOVERY)
  {
    return false;
  }

  switch (alarmRecoveryPhase)
  {
    case ALARM_RECOVERY_PHASE_HOME:
      if (!alarmCommandRequested)
      {
        if (!movementTaskIsBusy())
        {
          requestHome();
          alarmCommandRequested = true;
        }
      }
      else if (!movementTaskIsBusy())
      {
        alarmCommandRequested = false;
        alarmRecoveryPhase = ALARM_RECOVERY_PHASE_RETRY_MOVE;
      }
      break;

    case ALARM_RECOVERY_PHASE_RETRY_MOVE:
      if (!alarmCommandRequested)
      {
        if (!movementTaskIsBusy())
        {
          g_any_movement_alarm = false;
          g_x_target = alarmRecoveryTargetX;
          g_z_target = alarmRecoveryTargetZ;
          requestMove();
          alarmCommandRequested = true;
        }
      }
      else if (!movementTaskIsBusy())
      {
        if (g_any_movement_alarm)
        {
          fsmHandleMovementAlarmEvent();
          return true;
        }

        alarmCommandRequested = false;
        changeState(alarmReturnState);
        return true;
      }
      break;

    default:
      alarmRecoveryPhase = ALARM_RECOVERY_PHASE_HOME;
      alarmCommandRequested = false;
      break;
  }

  return false;
}

static bool fsmRunFaultLockout(void)
{
  /*
   * What: keep the system in a recoverable blocking fault state.
   * How: homes, moves to the safe logical position and then waits for reset_alarm_request.
   * Why: after two alarms, an operator must authorize a clean STARTUP before normal modes resume.
   */
  if (thisSt != FAULT_LOCKOUT)
  {
    return false;
  }

  g_any_movement_alarm = true;

  switch (faultLockoutPhase)
  {
    case FAULT_LOCKOUT_PHASE_HOME:
      if (!alarmCommandRequested)
      {
        if (!movementTaskIsBusy())
        {
          requestHome();
          alarmCommandRequested = true;
        }
      }
      else if (!movementTaskIsBusy())
      {
        alarmCommandRequested = false;
        faultLockoutPhase = FAULT_LOCKOUT_PHASE_SAFE_MOVE;
      }
      break;

    case FAULT_LOCKOUT_PHASE_SAFE_MOVE:
      if (!alarmCommandRequested)
      {
        if (!movementTaskIsBusy())
        {
          g_x_target = FSM_SAFE_PARK_X_MM;
          g_z_target = FSM_SAFE_PARK_Z_MM;
          requestMove();
          alarmCommandRequested = true;
        }
      }
      else if (!movementTaskIsBusy())
      {
        alarmCommandRequested = false;
        faultLockoutPhase = FAULT_LOCKOUT_PHASE_WAIT_RESET;
      }
      break;

    case FAULT_LOCKOUT_PHASE_WAIT_RESET:
      if ((faultLockoutResetRequested) && (!movementTaskIsBusy()))
      {
        fsmPrepareStartup(alarmReturnState);
        changeState(STARTUP);
        return true;
      }
      break;

    default:
      break;
  }

  return false;
}

static void fsmFinishStartup(void)
{
  /*
   * What: leave the transient startup state.
   * How: restores the validated state that was loaded from flash before STARTUP.
   * Why: after homing/GPS the controller should behave exactly as configured before reboot.
   */
  if (!stateIsPersistent(startupReturnState))
  {
    startupReturnState = STDBY;
  }

  thisSt = startupReturnState;
  nextSt = startupReturnState;
  auto_on = (thisSt == AUTO_MODE);
}

static void fsmHandleMovementAlarmEvent(void)
{
  /*
   * What: convert a movement alarm notification into FSM recovery policy.
   * How: first alarm stores source state/target and enters ALARM_RECOVERY; second alarm enters FAULT_LOCKOUT.
   * Why: movement and WiFi remain generic producers of events while the FSM owns behavior.
   */
  g_any_movement_alarm = true;

  if (thisSt == ALARM_RECOVERY)
  {
    pendingAlarmTargetValid = false;
    alarmCommandRequested = false;
    faultLockoutResetRequested = false;
    faultLockoutPhase = FAULT_LOCKOUT_PHASE_HOME;
    changeState(FAULT_LOCKOUT);
    return;
  }

  if ((thisSt == FAULT_LOCKOUT) || (thisSt == STARTUP))
  {
    pendingAlarmTargetValid = false;
    return;
  }

  alarmReturnState = stateIsPersistent(thisSt) ? thisSt : STDBY;

  if (pendingAlarmTargetValid)
  {
    alarmRecoveryTargetX = pendingAlarmTargetX;
    alarmRecoveryTargetZ = pendingAlarmTargetZ;
  }
  else
  {
    alarmRecoveryTargetX = g_x_target;
    alarmRecoveryTargetZ = g_z_target;
  }

  pendingAlarmTargetValid = false;
  alarmRecoveryPhase = ALARM_RECOVERY_PHASE_HOME;
  alarmCommandRequested = false;
  changeState(ALARM_RECOVERY);
}

static TickType_t ticksUntilPeriod(TickType_t now, TickType_t last, TickType_t period)
{
  /*
   * What: compute how long a periodic action can still sleep.
   * How: returns zero when the period is already due, otherwise the remaining ticks.
   * Why: tickless needs long blocking windows, but deadlines must still fire on time.
   */
  TickType_t elapsed = now - last;

  if (elapsed >= period)
  {
    return 0U;
  }

  return period - elapsed;
}

static TickType_t fsmWaitTicks(TickType_t now, TickType_t last_auto_tick, TickType_t last_time_save_tick)
{
  /*
   * What: choose the next timeout for the FSM event queue.
   * How: always wakes for RTC persistence; AUTO also wakes for its 5 s iteration.
   * Why: STANDBY, MANUAL and EPH_INPUT should sleep while no submit/movement is pending.
   */
  TickType_t wait_ticks;

  if (stateIsTransient(thisSt))
  {
    return pdMS_TO_TICKS((thisSt == STARTUP) ? FSM_STARTUP_POLL_MS : FSM_ALARM_POLL_MS);
  }

  wait_ticks = ticksUntilPeriod(now,
                                last_time_save_tick,
                                pdMS_TO_TICKS(STORAGE_TIME_SAVE_PERIOD_MS));

  if (thisSt == AUTO_MODE)
  {
    TickType_t auto_wait_ticks = ticksUntilPeriod(now,
                                                  last_auto_tick,
                                                  pdMS_TO_TICKS(FSM_AUTO_PERIOD_MS));

    if (auto_wait_ticks < wait_ticks)
    {
      wait_ticks = auto_wait_ticks;
    }
  }

  return wait_ticks;
}

static bool stateIsPersistent(States state)
{
  /*
   * What: identify states that are valid user operating modes.
   * How: excludes STARTUP, which is only a transient boot phase.
   * Why: flash must keep the resume target, not the internal startup marker.
   */
  return (state == STDBY) ||
         (state == AUTO_MODE) ||
         (state == MANUAL) ||
         (state == EPH_INPUT);
}

static bool stateIsTransient(States state)
{
  /*
   * What: identify internal states that must not accept user movement submits or flash persistence.
   * How: groups startup and alarm handling states.
   * Why: external inputs should only request events; recovery sequencing is owned here.
   */
  return (state == STARTUP) ||
         (state == ALARM_RECOVERY) ||
         (state == FAULT_LOCKOUT);
}

static bool stateTransitionIsValid(States currentState, States newState)
{
  /*
   * What: decide if a requested state is allowed.
   * How: accepts only the declared application states for this first integration.
   * Why: gives us one future point to enforce stricter transition rules.
   */
  (void)currentState;

  return stateIsPersistent(newState) || stateIsTransient(newState);
}
