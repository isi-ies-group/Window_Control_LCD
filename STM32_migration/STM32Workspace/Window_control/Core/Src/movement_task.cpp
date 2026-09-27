#include "movement_task.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "freertos_tickless.h"

#include "global_structs.h"
#include "movement.h"
#include "state_machine.h"
#include "storage.h"

#define MOVEMENT_QUEUE_LENGTH      5U
#define MOVEMENT_TASK_STACK_SIZE   4096U
#define MOVEMENT_TASK_PRIORITY     22U

typedef enum
{
  CMD_NONE = 0,
  CMD_MOVE,
  CMD_HOME
} MoveCmd;

typedef struct
{
  MoveCmd cmd;
  float x_target;
  float z_target;
} MoveMessage;

static QueueHandle_t movementQueue = NULL;
static TaskHandle_t movementTaskHandle = NULL;
/* Motors are initialized only when the first movement command is requested. */
static bool movementInitialized = false;
/* True while the task is physically generating pulses or running homing. */
static volatile bool movementCommandActive = false;

static void movementTask(void *argument);
static void movementPostCommand(MoveCmd cmd, float x_target, float z_target);

void initMovementTask(void)
{
  /*
   * What: create the movement worker and its command queue on demand.
   * How: initializes motor GPIO/timing once, then creates a FreeRTOS queue and task.
   * Why: movement pulses are blocking, so they must not run inside the HTTP/FSM task.
   */
  if (!movementInitialized)
  {
    /* GPIO/DWT motor setup is delayed so WiFi startup does not touch movement hardware. */
    init_motors();
    movementInitialized = true;
  }

  if (movementQueue == NULL)
  {
    /* The queue serializes movement commands so only one runs at a time. */
    movementQueue = xQueueCreate(MOVEMENT_QUEUE_LENGTH, sizeof(MoveMessage));
  }

  if ((movementQueue != NULL) && (movementTaskHandle == NULL))
  {
    /* FreeRTOS expects stack size in words; the project constants are kept in bytes. */
    (void)xTaskCreate(movementTask,
                      "Movement_Task",
                      MOVEMENT_TASK_STACK_SIZE >> 2U,
                      NULL,
                      MOVEMENT_TASK_PRIORITY,
                      &movementTaskHandle);
  }
}

void requestMove(void)
{
  /*
   * What: request one absolute X/Z movement.
   * How: clamps the configured X/Z range, then captures g_x_target/g_z_target into the queue message.
   * Why: later target changes must not alter a movement command that was already accepted.
   */
  g_x_target = movementClampVerticalTarget(g_x_target);
  g_z_target = movementClampHorizontalTarget(g_z_target);
  movementPostCommand(CMD_MOVE, g_x_target, g_z_target);
}

void requestHome(void)
{
  /*
   * What: request homing from any web/FSM state.
   * How: posts CMD_HOME to the movement queue.
   * Why: homing can take time and must not block WiFi request processing.
   */
  movementPostCommand(CMD_HOME, 0.0f, 0.0f);
}

bool movementTaskIsBusy(void)
{
  /*
   * What: report whether movement or homing is active/pending.
   * How: checks the active execution flag and the movement queue depth.
   * Why: startup sequencing must wait for HOME to finish before requesting GPS sync.
   */
  return (movementCommandActive != false) ||
         ((movementQueue != NULL) && (uxQueueMessagesWaiting(movementQueue) != 0U));
}

static void movementTask(void *argument)
{
  /*
   * What: execute movement commands serially.
   * How: waits on the queue, runs move() or GoHomePair(), then saves the final position.
   * Why: only this task should own long motor operations and position persistence.
   */
  MoveMessage msg;

  (void)argument;

  for (;;)
  {
    /* Sleep here until the FSM/web layer posts a movement command. */
    if (xQueueReceive(movementQueue, &msg, portMAX_DELAY) == pdTRUE)
    {
      movementCommandActive = true;

      switch (msg.cmd)
      {
        case CMD_MOVE:
        {
          bool move_ok;

          /* What: protect motor movement from low-power entry.
           * How: block tickless before move(), then release it when the position has been stored.
           * Why: STEP pulses must keep deterministic timing while the motors are moving.
           */
          DisableSuppressTicksAndSleep(1UL << CFG_TICKLESS_MOVEMENT_ID);
          /* move() consumes the captured absolute X/Z target from this queue message. */
          move_ok = move(msg.x_target, msg.z_target);
          /*
           * What: accept the theoretical position only after pulse generation finished.
           * How: copy the command target to g_x_val/g_z_val after move() returns.
           * Why: web status/storage must represent completed movement, not pending intent.
           */
          if (move_ok)
          {
            g_x_val = msg.x_target;
            g_z_val = msg.z_target;
            (void)savePos();
          }
          else
          {
            (void)fsmPostMovementAlarm(msg.x_target, msg.z_target);
          }
          EnableSuppressTicksAndSleep(1UL << CFG_TICKLESS_MOVEMENT_ID);
          break;
        }

        case CMD_HOME:
          /* What: protect homing from low-power entry.
           * How: block tickless during GoHomePair() and release it after saving the home position.
           * Why: end-stop detection and STEP pulses must not be interrupted by Sleep/Stop.
           */
          DisableSuppressTicksAndSleep(1UL << CFG_TICKLESS_MOVEMENT_ID);
          /* Homing resets both software position globals to the mechanical zero. */
          GoHomePair(&g_x_val, &g_z_val);
          /* Keep pending targets aligned with the newly accepted home position. */
          g_x_target = g_x_val;
          g_z_target = g_z_val;
          (void)savePos();
          EnableSuppressTicksAndSleep(1UL << CFG_TICKLESS_MOVEMENT_ID);
          break;

        case CMD_NONE:
        default:
          break;
      }

      movementCommandActive = false;
    }
  }
}

static void movementPostCommand(MoveCmd cmd, float x_target, float z_target)
{
  /*
   * What: send a movement command to the worker task.
   * How: lazily creates the task, captures the target, and rejects stale move backlog.
   * Why: HTTP/FSM code can request movement safely without old commands executing later.
   */
  MoveMessage msg;

  if ((movementQueue == NULL) || (movementTaskHandle == NULL))
  {
    /* Lazy creation lets the WiFi app boot without creating movement resources. */
    initMovementTask();
  }

  if (movementQueue != NULL)
  {
    if (cmd == CMD_HOME)
    {
      /*
       * What: give homing priority over queued movement intents.
       * How: drops commands still waiting in the queue before queuing CMD_HOME.
       * Why: old move requests after a home cycle can send the mechanics to an unexpected target.
       */
      (void)xQueueReset(movementQueue);
    }
    else if ((movementCommandActive) || (uxQueueMessagesWaiting(movementQueue) != 0U))
    {
      /*
       * What: avoid movement backlog while another movement/homing command is active or pending.
       * How: rejects this CMD_MOVE; the next auto/manual/eph submit can request a fresh target.
       * Why: queued stale moves were able to execute after long homing or slow movements.
       */
      return;
    }

    if (cmd == CMD_MOVE)
    {
      x_target = movementClampVerticalTarget(x_target);
      z_target = movementClampHorizontalTarget(z_target);
    }

    msg.cmd = cmd;
    msg.x_target = x_target;
    msg.z_target = z_target;

    /* Non-blocking send: if the queue is full, this command is silently dropped. */
    (void)xQueueSend(movementQueue, &msg, 0U);
  }
}
