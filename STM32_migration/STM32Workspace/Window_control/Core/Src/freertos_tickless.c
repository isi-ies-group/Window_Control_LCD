#include "freertos_tickless.h"

#include "FreeRTOS.h"
#include "task.h"

static uint32_t suppress_ticks_blockers;

void DisableSuppressTicksAndSleep(uint32_t bitmask)
{
  taskENTER_CRITICAL();
  suppress_ticks_blockers |= bitmask;
  taskEXIT_CRITICAL();
}

void EnableSuppressTicksAndSleep(uint32_t bitmask)
{
  taskENTER_CRITICAL();
  suppress_ticks_blockers &= ~bitmask;
  taskEXIT_CRITICAL();
}
