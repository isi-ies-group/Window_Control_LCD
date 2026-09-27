#include "controled_shutdown.h"

#include "main.h"
#include "storage.h"
#include "stm32u5xx_hal.h"
#include "w6x_api.h"

#include "FreeRTOS.h"
#include "task.h"

extern RTC_HandleTypeDef hrtc;

#define CONTROLED_SHUTDOWN_MAX_SECONDS 131071U
#define CONTROLED_WIFI_OFF_TASK_STACK_WORDS 512U
#define CONTROLED_WIFI_OFF_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)

static TaskHandle_t wifiOffTaskHandle = NULL;

static void ControledShutdown_WifiOffTask(void *argument)
{
  /*
   * What: run the isolated ST67 WiFi-off test outside the HTTP request stack.
   * How: waits for the web response to leave, stops SoftAP, deinitializes the NCP, then holds CHIP_EN low.
   * Why: the U575 must stay awake so current measurement shows only the WiFi module change.
   */
  (void)argument;

  vTaskDelay(pdMS_TO_TICKS(750U));

  (void)W6X_WiFi_AP_Stop();
  vTaskDelay(pdMS_TO_TICKS(250U));

  W6X_DeInit();
  ControledShutdown_SetSafeOutputs();

  wifiOffTaskHandle = NULL;
  vTaskDelete(NULL);
}

static void ControledShutdown_BlockInterruptWakeups(void)
{
  /*
   * What: remove normal runtime interrupts before executing the final WFI.
   * How: suspends SysTick, clears pending core interrupts, and masks NVIC IRQ lines.
   * Why: a pending RTOS/SPI/EXTI interrupt can make the CPU return instead of latching Shutdown.
   */
  HAL_SuspendTick();

  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  for (uint32_t index = 0U; index < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); index++)
  {
    NVIC->ICER[index] = 0xFFFFFFFFU;
    NVIC->ICPR[index] = 0xFFFFFFFFU;
  }

  __disable_irq();
  __DSB();
  __ISB();
}

static void ControledShutdown_HoldWifiDisabledInShutdown(void)
{
  /*
   * What: keep ST67 CHIP_EN low while the STM32 is in Shutdown.
   * How: enables the U5 PWR-domain pull-down for PE11 and activates low-power PUPD retention.
   * Why: normal GPIO output state can be lost in Shutdown, letting CHIP_EN float high again.
   */
  (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_E, PWR_GPIO_BIT_11);
  (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_E, PWR_GPIO_BIT_11);
  HAL_PWREx_EnablePullUpPullDownConfig();
}

bool ControledShutdown_ArmRtcWakeup(uint32_t seconds)
{
  /*
   * What: prepare an RTC wakeup event before entering STM32 Shutdown.
   * How: clears old wake flags, reloads the RTC wake timer, and routes RTC to a PWR wake source.
   * Why: Shutdown loses the running firmware state, so wakeup must be armed before sleeping.
   */
  if ((seconds == 0U) || (seconds > CONTROLED_SHUTDOWN_MAX_SECONDS))
  {
    return false;
  }

  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN6);
  __HAL_PWR_CLEAR_FLAG(PWR_WAKEUP_ALL_FLAG);

  (void)HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);

  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc,
                                  seconds - 1U,
                                  RTC_WAKEUPCLOCK_CK_SPRE_17BITS,
                                  0U) != HAL_OK)
  {
    return false;
  }

  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN6_HIGH_3);

  return true;
}

void ControledShutdown_SetSafeOutputs(void)
{
  /*
   * What: leave external hardware in a safe state before cutting power/clock domains.
   * How: disables motor drivers and pulls the ST67 chip enable low.
   * Why: no output should keep drivers active while the MCU is entering deep sleep.
   */
  HAL_GPIO_WritePin(Horizontal_ENABLE_GPIO_Port, Horizontal_ENABLE_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(Vertical_ENABLE_GPIO_Port, Vertical_ENABLE_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CHIP_EN_GPIO_Port, CHIP_EN_Pin, GPIO_PIN_RESET);
}

ControledShutdownStatus_t ControledShutdown_DisconnectWifi(void)
{
  /*
   * What: shut down only the ST67 WiFi side.
   * How: stops the SoftAP, asks the ST driver to hibernate/deinit the NCP, then forces CHIP_EN low.
   * Why: this isolates the WiFi module consumption without putting the STM32U575 into deep sleep.
   */

  (void)W6X_WiFi_AP_Stop();
  HAL_Delay(250U);
  W6X_DeInit();
  ControledShutdown_SetSafeOutputs();

  return CONTROLED_SHUTDOWN_OK;
}

ControledShutdownStatus_t ControledShutdown_RequestWifiOffTest(void)
{
  /*
   * What: request the one-shot WiFi-off measurement task.
   * How: creates a short FreeRTOS task and rejects duplicate requests while it is running.
   * Why: WiFi deinit can kill the active HTTP socket, so it must happen after the response is sent.
   */
  if (wifiOffTaskHandle != NULL)
  {
    return CONTROLED_SHUTDOWN_BUSY;
  }

  if (xTaskCreate(ControledShutdown_WifiOffTask,
                  "WiFiOffTest",
                  CONTROLED_WIFI_OFF_TASK_STACK_WORDS,
                  NULL,
                  CONTROLED_WIFI_OFF_TASK_PRIORITY,
                  &wifiOffTaskHandle) != pdPASS)
  {
    wifiOffTaskHandle = NULL;
    return CONTROLED_SHUTDOWN_WIFI_ERROR;
  }

  return CONTROLED_SHUTDOWN_OK;
}

void ControledShutdown_EnterShutdown(void)
{
  /*
   * What: enter the deepest STM32 shutdown path used by this test module.
   * How: saves RTC-backed state, masks interrupts, and retries HAL_PWREx_EnterSHUTDOWNMode().
   * Why: pending SysTick/SPI/EXTI interrupts can cancel WFI before Shutdown is latched.
   */
  (void)saveRtcTime();

  ControledShutdown_SetSafeOutputs();
  ControledShutdown_HoldWifiDisabledInShutdown();
  ControledShutdown_BlockInterruptWakeups();

  while (1)
  {
    HAL_PWREx_EnterSHUTDOWNMode();
  }
}

ControledShutdownStatus_t ControledShutdown_Run(uint32_t seconds)
{
  /*
   * What: execute the complete controlled shutdown sequence for timed RTC wakeup.
   * How: arms RTC, disconnects WiFi, and finally enters STM32 Shutdown.
   * Why: this gives the HTTP/UI layer a single call once each step has been validated.
   */
  ControledShutdownStatus_t wifi_status;

  if (!ControledShutdown_ArmRtcWakeup(seconds))
  {
    return CONTROLED_SHUTDOWN_RTC_ERROR;
  }

  wifi_status = ControledShutdown_DisconnectWifi();
  (void)wifi_status;

  ControledShutdown_EnterShutdown();

  return CONTROLED_SHUTDOWN_OK;
}
