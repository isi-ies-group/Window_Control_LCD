#ifndef FREERTOS_TICKLESS_H
#define FREERTOS_TICKLESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CFG_TICKLESS_SPIIF_ID    0
#define CFG_TICKLESS_LOG_ID      1
#define CFG_TICKLESS_MOVEMENT_ID 2
#define CFG_TICKLESS_AUTOMODE_ID 3

void DisableSuppressTicksAndSleep(uint32_t bitmask);
void EnableSuppressTicksAndSleep(uint32_t bitmask);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_TICKLESS_H */
