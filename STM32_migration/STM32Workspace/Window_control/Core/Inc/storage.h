#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load every persistent variable into the runtime globals at application start. */
bool Storage_LoadAll(void);

/* Save configuration, state, position, or time mode into the flash-backed record. */
bool saveData(void);
bool saveState(void);
bool savePos(void);
bool saveMovementConfig(void);
bool saveManualTime(int year, int month, int day, int hour, int minute, int second);
bool saveTimeMode(bool is_manual_time);
bool saveRtcTime(void);

/* Reload one group from flash when a caller wants ESP32-like explicit load steps. */
bool loadData(void);
bool loadState(void);
bool loadPos(void);
bool loadManualTime(void);

/* Report whether flash already contained a valid persistent record at boot. */
bool Storage_HasValidRecord(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
