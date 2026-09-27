#ifndef CONTROLED_SHUTDOWN_H
#define CONTROLED_SHUTDOWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  CONTROLED_SHUTDOWN_OK = 0,
  CONTROLED_SHUTDOWN_BAD_ARGUMENT,
  CONTROLED_SHUTDOWN_RTC_ERROR,
  CONTROLED_SHUTDOWN_WIFI_ERROR,
  CONTROLED_SHUTDOWN_BUSY
} ControledShutdownStatus_t;

bool ControledShutdown_ArmRtcWakeup(uint32_t seconds);
void ControledShutdown_SetSafeOutputs(void);
ControledShutdownStatus_t ControledShutdown_DisconnectWifi(void);
ControledShutdownStatus_t ControledShutdown_RequestWifiOffTest(void);
void ControledShutdown_EnterShutdown(void);
ControledShutdownStatus_t ControledShutdown_Run(uint32_t seconds);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLED_SHUTDOWN_H */
