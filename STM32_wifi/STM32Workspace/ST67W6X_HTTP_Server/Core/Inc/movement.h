#ifndef MOVEMENT_H
#define MOVEMENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_motors(void);
bool move(float xmm, float zmm);
float movementClampVerticalTarget(float xmm);
float movementClampHorizontalTarget(float zmm);
void GoHomePair(float *posX, float *posZ);
uint8_t movementLimitSwitchUpdateFromExti(uint16_t gpio_pin);
void movementLimitSwitchRefreshAll(void);
uint8_t movementAnyLimitSwitchActive(void);

#ifdef __cplusplus
}
#endif

#endif /* MOVEMENT_H */
