#ifndef AUTOMODE_H
#define AUTOMODE_H

#include "global_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

void startDaySimulation(time_t sunrise_epoch);
void updateSPAInputsFromTime(struct tm *time_info, SPAInputs *spa);
void autoMode();

#ifdef __cplusplus
}
#endif

#endif /* AUTOMODE_H */
