/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    httpserver.c
  * @author  ST67 Application Team
  * @brief   Http server application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025-2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "httpserver.h"
#include "main.h"

#include "w6x_api.h"
#include "common_parser.h" /* Common Parser functions */

/* Please read the following Wiki on how to create the html_pages.h binary:
https://wiki.st.com/stm32mcu/wiki/Connectivity:Wi-Fi_ST67W6X_HTTP_Server_Application */
#include "html_pages.h"

#include "FreeRTOS.h"
#include "event_groups.h"

/* USER CODE BEGIN Includes */
#include "task.h"

#include "controled_shutdown.h"
#include "global_structs.h"
#include "gps.h"
#include "movement_alarm.h"
#include "movement_parameters.h"
#include "state_machine.h"
#include "storage.h"

/* USER CODE END Includes */

/* Global variables ----------------------------------------------------------*/
/* USER CODE BEGIN GV */

/* USER CODE END GV */

/* Private typedef -----------------------------------------------------------*/
/**
  * @brief  HTTP server response type
  */
typedef enum
{
  FAVICON_SVG,
  ST_LOGO_SVG,
  INDEX_HTML,
  LED_GREEN_STATE,
  LED_RED_STATE,
  BUTTON_STATE,
  ERROR_404_HTML,
  UNKNOWN_RESPONSE
} HttpServer_response_e;

/**
  * @brief  Structure to handle GPIO pins for HTTP server application
  */
typedef struct
{
  GPIO_PinState btn_state;        /*!< user button state */
  GPIO_PinState led_green_state;  /*!< green led state */
  GPIO_PinState led_red_state;    /*!< red led state */
} PinInfos_t;

/**
  * @brief  HTTP server response structure
  */
typedef struct
{
  HttpServer_response_e response_type;  /*!< Type of response */
  const char *request;                  /*!< Request string */
  const char *response;                 /*!< Response string */
  size_t resp_len;                      /*!< Response string length */
} HttpServer_response_t;

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private defines -----------------------------------------------------------*/
/** Priority of the user pins polling task */
#define PIN_POLLING_THREAD_PRIO        30

/** Stack size of the user pins polling task */
#define PIN_POLLING_TASK_STACK_SIZE    512U

/** Priority of the web server child task */
#define WEBSERVER_CHILD_THREAD_PRIO    29

/** Stack size of the web server child task */
#define HTTP_CHILD_TASK_STACK_SIZE     2048U

/** HTTP server port */
#define HTTP_PORT                      80

/** Socket timeout in ms */
#define SOCKET_TIMEOUT_MS              1000

/** Event flag for pin status update */
#define EVENT_FLAG_PIN                 (1UL << 1U)

/** Timeout for the pin status update in ms */
#define PIN_TIMEOUT_MS                 9000

/** Buffer size for the child task */
#define HTTP_CHILD_TASK_BUFFER_SIZE    1024U

/** Maximum bytes to send in one step */
#define MAX_BYTES_TO_SEND              4096U

/* USER CODE BEGIN PD */
/** Maximum simultaneous HTTP client tasks. Keep below the ST67 network connection limit. */
#define HTTP_MAX_ACTIVE_CLIENTS        3U

/* USER CODE END PD */

/* Private macros ------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/** Button and Leds states */
static volatile PinInfos_t pins_info =
{
  .btn_state = GPIO_PIN_RESET,
  .led_green_state = GPIO_PIN_RESET,
  .led_red_state = GPIO_PIN_RESET
};

/** Event group handle for pin status update */
EventGroupHandle_t pin_handle;

/** Flag to indicate if the button state has changed */
extern uint8_t button_changed;

/** Response with OK content */
static const char response_ok_html[] =
{
  "HTTP/1.1 200 OK\r\n"
  "Server: U5\r\n"
  "Access-Control-Allow-Origin: * \r\n"
  "Cache-Control: no-cache\r\n"
  "Keep-Alive: timeout=2, max=2\r\n"
  "Connection: close\r\n"
  "Content-Type: text/html; charset=utf-8\r\n"
  "Content-Length: 2\r\n\r\n"
  "OK"
};

/** Response content depending on the request */
static const HttpServer_response_t http_server_responses[] =
{
  {INDEX_HTML,      "GET / ",                                     response_index_html,  sizeof(response_index_html)},
  {FAVICON_SVG,     "GET /favicon.ico",                           response_favicon_svg, sizeof(response_favicon_svg)},
  {ST_LOGO_SVG,     "GET /IES_logo.svg",                          response_st_logo_svg, sizeof(response_st_logo_svg)},
  {ST_LOGO_SVG,     "GET /ST_logo_2020_white_no_tagline_rgb.svg", response_st_logo_svg, sizeof(response_st_logo_svg)},
  {LED_GREEN_STATE, "GET /LedGreen",                              response_ok_html,     sizeof(response_ok_html)},
  {LED_RED_STATE,   "GET /LedRed",                                response_ok_html,     sizeof(response_ok_html)},
  {BUTTON_STATE,    "GET /pins_status",                           NULL,                 0U},
};

/* USER CODE BEGIN PV */
extern RTC_HandleTypeDef hrtc;
static volatile uint32_t http_active_clients = 0U;
static volatile uint32_t http_tracked_clients_mask = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/**
  * @brief  Web server child task
  * @param  arg: pointer on argument(not used here)
  */
static void http_server_serve_task(void *arg);

/**
  * @brief  Pins polling task
  * @param  arg: pointer on argument(not used here)
  */
static void pin_verification_task(void *arg);

/**
  * @brief  Write HTML data to client
  * @param  client: client information
  * @param  buffer: HTML data to send
  * @param  buffer_size: size of the data to send
  * @retval 0 if success, 1 otherwise
  */
static int32_t http_server_write(int32_t client, const char *buffer, size_t buffer_size);

/**
  * @brief  Close client connection
  * @param  client: client information
  * @retval 0
  */
static int32_t close_client(int32_t client);

/**
  * @brief  Process the HTTP response
  * @param  client: client information
  * @param  recv_buffer: received request data
  */
static void http_process_response(int32_t client, char *recv_buffer);

/* USER CODE BEGIN PFP */
static void http_get_rtc_datetime(char *date_str, size_t date_size,
                                  char *time_str, size_t time_size);
static int32_t http_hex_to_nibble(char c);
static void http_url_decode(char *dst, size_t dst_size, const char *src, size_t src_len);
static bool http_get_query_param(const char *request, const char *name,
                                 char *value, size_t value_size);
static bool http_get_query_double(const char *request, const char *name, double *value);
static bool http_get_query_uint32(const char *request, const char *name, uint32_t *value);
static bool http_apply_config_submit(const char *request);
static bool http_apply_movement_gain_submit(const char *request);
static bool http_apply_movement_offset_submit(const char *request);
static bool http_apply_vertical_movement_gain_submit(const char *request);
static bool http_apply_vertical_movement_offset_submit(const char *request);
static bool http_apply_movement_x_range_submit(const char *request);
static bool http_apply_movement_z_range_submit(const char *request);
static bool http_apply_movement_speed_submit(const char *request);
static bool http_apply_movement_home_speed_submit(const char *request);
static bool http_apply_manual_datetime(const char *request);
static bool http_apply_eph_submit(const char *request);
static bool http_apply_manual_goto(const char *request);
static void http_send_html_message(int32_t client, const char *title,
                                   const char *message, bool is_error);
static bool http_accept_client_or_reject(int32_t client);
static void http_configure_client_socket(int32_t client);
static void http_release_client_slot(int32_t client);
static void http_reject_busy_client(int32_t client);

/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void http_server_socket(void *arg)
{
  (void)arg;
  const int32_t domain = AF_INET;
  int32_t sock = -1;
  struct sockaddr_in s_addr_in_t = { 0 };
  int32_t fct_start = 9;
  uint16_t port = HTTP_PORT;
  uint8_t ip_address[4] = {0};
  uint8_t netmask_addr[4] = {0};
  int32_t timeout = (int32_t)pdMS_TO_TICKS(SOCKET_TIMEOUT_MS);

  /* USER CODE BEGIN http_server_socket_1 */

  /* USER CODE END http_server_socket_1 */

  /* Get the soft-AP current IP address */
  if (W6X_Net_AP_GetIPAddress(ip_address, netmask_addr) != W6X_STATUS_OK)
  {
    LogError("Get soft-AP IP failed\n");
    goto _err;
  }

  LogInfo("Soft-AP IP address : " IPSTR "\n", IP2STR(ip_address));
  s_addr_in_t.sin_addr.s_addr = ATON(ip_address);
  s_addr_in_t.sin_port = PP_HTONS(port);
  s_addr_in_t.sin_family = AF_INET;

  /* Create a new TCP server socket with port HTTP_PORT */
  sock = W6X_Net_Socket(domain, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0)
  {
    LogInfo("Could not create the socket.\n");
    goto _err;
  }

  /* Set the socket Receive timeout option */
  if (W6X_Net_Setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout)) != 0)
  {
    LogError("Could not set the socket options.\n");
    goto _err;
  }

  /* Set the socket Send timeout option */
  if (W6X_Net_Setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (void *)&timeout, sizeof(timeout)) != 0)
  {
    LogError("Could not set the socket options.\n");
    goto _err;
  }

  /* Set the socket Send timeout option */
  if (W6X_Net_Setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&timeout, sizeof(timeout)) != 0)
  {
    LogError("Could not set the socket options.\n");
    goto _err;
  }

  /* Bind the socket to the server address */
  if (W6X_Net_Bind(sock, (struct sockaddr *)&s_addr_in_t, sizeof(s_addr_in_t)) != 0)
  {
    LogInfo("\n LwIP Bind Fail\n");
    goto _err;
  }

  LogInfo("\n LwIP Bind Pass\n");

  /* Listen for incoming connections (TCP listen backlog = 5). */
  if (W6X_Net_Listen(sock, 5) != 0)
  {
    LogInfo("\n LwIP Listen Fail\n");
    goto _err;
  }

  LogInfo("\n LwIP Listen Pass\n");

  /* USER CODE BEGIN http_server_socket_2 */

  /* USER CODE END http_server_socket_2 */

  /* Creation of a thread to check if the pin value of the button or the LEDs has change */
  pin_handle = xEventGroupCreate();
  if (pdPASS != xTaskCreate((TaskFunction_t)pin_verification_task, "UserPinsPolling",
                            PIN_POLLING_TASK_STACK_SIZE >> 2U,
                            &fct_start, PIN_POLLING_THREAD_PRIO, NULL))
  {
    LogInfo("User pins task creation failed\n");
    goto _err;
  }

  while (true)
  {
    struct sockaddr remotehost_t;
    uint32_t remotehost_size = sizeof(remotehost_t);

    /* Wait for an incoming client connection */
    int32_t newconn = W6X_Net_Accept(sock, (struct sockaddr *)&remotehost_t, (socklen_t *)&remotehost_size);
    if (newconn < 0)
    {
      vTaskDelay(pdMS_TO_TICKS(200));
      LogInfo("\n Failed to accept new client requests.\n");
    }
    else
    {
      /* Create a temporary thread to process the incoming HTTP request */
      char thread_name[14];
      const size_t thread_name_len = sizeof(thread_name);
      (void)snprintf(thread_name, thread_name_len, "HTTP_%08" PRIX32, newconn);
      thread_name[thread_name_len - 1U] = '\0';

      LogDebug("\n Creation of temporary thread to process an incoming HTTP request : %" PRIi32 "\n", newconn);
      if (pdPASS != xTaskCreate((TaskFunction_t)http_server_serve_task, thread_name,
                                HTTP_CHILD_TASK_STACK_SIZE >> 2U,
                                &newconn, WEBSERVER_CHILD_THREAD_PRIO, NULL))
      {
        LogInfo("%s task creation failed\n", thread_name);
      }
      /* Delay added to avoid that too many requests are processed in parallel */
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }

  /* USER CODE BEGIN http_server_socket_last */

  /* USER CODE END http_server_socket_last */

_err:
  /* Error case */
  if (sock >= 0)
  {
    if (W6X_Net_Shutdown(sock, 1) != 0)
    {
      LogError("Failed to close server socket\n");
    }
  }
  return;
}

/* USER CODE BEGIN FD */
static int32_t http_hex_to_nibble(char c)
{
  if ((c >= '0') && (c <= '9'))
  {
    return (int32_t)(c - '0');
  }
  if ((c >= 'a') && (c <= 'f'))
  {
    return (int32_t)(c - 'a' + 10);
  }
  if ((c >= 'A') && (c <= 'F'))
  {
    return (int32_t)(c - 'A' + 10);
  }

  return -1;
}

static void http_url_decode(char *dst, size_t dst_size, const char *src, size_t src_len)
{
  size_t dst_index = 0;

  if ((dst == NULL) || (dst_size == 0U))
  {
    return;
  }

  if (src == NULL)
  {
    dst[0] = '\0';
    return;
  }

  for (size_t src_index = 0; (src_index < src_len) && (dst_index < (dst_size - 1U)); src_index++)
  {
    if (src[src_index] == '+')
    {
      dst[dst_index++] = ' ';
    }
    else if ((src[src_index] == '%') && ((src_index + 2U) < src_len))
    {
      int32_t high = http_hex_to_nibble(src[src_index + 1U]);
      int32_t low = http_hex_to_nibble(src[src_index + 2U]);

      if ((high >= 0) && (low >= 0))
      {
        dst[dst_index++] = (char)((high << 4) | low);
        src_index += 2U;
      }
      else
      {
        dst[dst_index++] = src[src_index];
      }
    }
    else
    {
      dst[dst_index++] = src[src_index];
    }
  }

  dst[dst_index] = '\0';
}

static bool http_get_query_param(const char *request, const char *name,
                                 char *value, size_t value_size)
{
  /*
   * What: extract one form/query parameter from an HTTP request.
   * How: reads after '?' for GET requests or after the blank header line for POST bodies.
   * Why: config and manual date/time can share the same URL-decoding parser.
   */
  const char *query;
  const char *query_end;
  const char *cursor;
  size_t name_len;

  if ((request == NULL) || (name == NULL) || (value == NULL) || (value_size == 0U))
  {
    return false;
  }

  value[0] = '\0';
  query = strchr(request, '?');
  if (query != NULL)
  {
    query++;
    query_end = strchr(query, ' ');
    if (query_end == NULL)
    {
      query_end = query + strlen(query);
    }
  }
  else
  {
    query = strstr(request, "\r\n\r\n");
    if (query == NULL)
    {
      return false;
    }

    query += 4U;
    query_end = query + strlen(query);
  }

  name_len = strlen(name);
  cursor = query;

  while (cursor < query_end)
  {
    const char *param_end = memchr(cursor, '&', (size_t)(query_end - cursor));
    const char *separator;

    if (param_end == NULL)
    {
      param_end = query_end;
    }

    separator = memchr(cursor, '=', (size_t)(param_end - cursor));
    if ((separator != NULL) &&
        ((size_t)(separator - cursor) == name_len) &&
        (strncmp(cursor, name, name_len) == 0))
    {
      http_url_decode(value, value_size, separator + 1, (size_t)(param_end - separator - 1));
      return true;
    }

    cursor = param_end + 1;
  }

  return false;
}

static bool http_get_query_double(const char *request, const char *name, double *value)
{
  char text[64];
  char *endptr = NULL;
  double parsed;

  if ((value == NULL) || (!http_get_query_param(request, name, text, sizeof(text))))
  {
    return false;
  }

  parsed = strtod(text, &endptr);
  if ((endptr == text) || (endptr == NULL) || (*endptr != '\0') ||
      (parsed != parsed))
  {
    return false;
  }

  *value = parsed;
  return true;
}

static bool http_get_query_uint32(const char *request, const char *name, uint32_t *value)
{
  double parsed = 0.0;
  uint32_t converted;

  if ((value == NULL) || (!http_get_query_double(request, name, &parsed)) ||
      (parsed < 0.0) || (parsed > 4294967295.0))
  {
    return false;
  }

  converted = (uint32_t)parsed;
  if ((double)converted != parsed)
  {
    return false;
  }

  *value = converted;
  return true;
}

static bool http_apply_config_submit(const char *request)
{
  /*
   * What: apply configuration values submitted from the web form.
   * How: parses latitude/longitude/panel/country fields, validates ranges and saves flash.
   * Why: this mirrors ESP32 config_submit so the HTML becomes the source of config changes.
   */
  char value[64];
  bool valid_request = true;
  double latitude = 0.0;
  double longitude = 0.0;
  double pan = 0.0;
  double tilt = 0.0;
  char country[sizeof(g_country)];

  if (http_get_query_param(request, "latitude", value, sizeof(value)))
  {
    latitude = strtod(value, NULL);
  }
  else
  {
    valid_request = false;
  }

  if (http_get_query_param(request, "longitude", value, sizeof(value)))
  {
    longitude = strtod(value, NULL);
  }
  else
  {
    valid_request = false;
  }

  if (http_get_query_param(request, "pan", value, sizeof(value)))
  {
    pan = strtod(value, NULL);
  }
  else
  {
    valid_request = false;
  }

  if (http_get_query_param(request, "tilt", value, sizeof(value)))
  {
    tilt = strtod(value, NULL);
  }
  else
  {
    valid_request = false;
  }

  if (http_get_query_param(request, "country", country, sizeof(country)) == false)
  {
    valid_request = false;
  }

  if ((valid_request == false) ||
      (latitude < -90.0) || (latitude > 90.0) ||
      (longitude < -180.0) || (longitude > 180.0))
  {
    return false;
  }

  g_SPAInputs.latitude = latitude;
  g_SPAInputs.longitude = longitude;
  g_AOIInputs.pan = pan;
  g_AOIInputs.tilt = tilt;
  g_AOIInputs.tilt_correction = http_get_query_param(request, "tilt_correction", value, sizeof(value));
  (void)strncpy(g_country, country, sizeof(g_country) - 1U);
  g_country[sizeof(g_country) - 1U] = '\0';

  (void)saveData();

  return true;
}

static bool http_apply_movement_gain_submit(const char *request)
{
  /*
   * What: apply the horizontal hysteresis gain submitted from the web page.
   * How: validates a finite numeric value, publishes it to the movement global and persists flash.
   * Why: mechanical calibration must be tunable without rebuilding firmware.
   */
  double gain = 0.0;

  if ((!http_get_query_double(request, "gain", &gain)) ||
      (!MovementParameters_IsGainValid((float)gain)))
  {
    return false;
  }

  if (!MovementParameters_SetHorizontalGain((float)gain))
  {
    return false;
  }

  return saveMovementConfig();
}

static bool http_apply_movement_offset_submit(const char *request)
{
  /*
   * What: apply the horizontal hysteresis offset submitted from the web page.
   * How: validates a finite millimetre value, publishes it to the movement global and persists flash.
   * Why: linear calibration may need a small intercept adjustment after regression.
   */
  double offset_mm = 0.0;

  if ((!http_get_query_double(request, "offset", &offset_mm)) ||
      (!MovementParameters_IsOffsetMmValid((float)offset_mm)))
  {
    return false;
  }

  if (!MovementParameters_SetHorizontalOffsetMm((float)offset_mm))
  {
    return false;
  }

  return saveMovementConfig();
}

static bool http_apply_vertical_movement_gain_submit(const char *request)
{
  /*
   * What: apply the vertical hysteresis gain submitted from the web page.
   * How: validates a finite numeric value, publishes it to the movement global and persists flash.
   * Why: vertical mechanical calibration must be tunable without rebuilding firmware.
   */
  double gain = 0.0;

  if ((!http_get_query_double(request, "gain", &gain)) ||
      (!MovementParameters_IsGainValid((float)gain)))
  {
    return false;
  }

  if (!MovementParameters_SetVerticalGain((float)gain))
  {
    return false;
  }

  return saveMovementConfig();
}

static bool http_apply_vertical_movement_offset_submit(const char *request)
{
  /*
   * What: apply the vertical hysteresis offset submitted from the web page.
   * How: validates a finite millimetre value, publishes it to the movement global and persists flash.
   * Why: vertical linear calibration may need a small intercept adjustment after testing.
   */
  double offset_mm = 0.0;

  if ((!http_get_query_double(request, "offset", &offset_mm)) ||
      (!MovementParameters_IsOffsetMmValid((float)offset_mm)))
  {
    return false;
  }

  if (!MovementParameters_SetVerticalOffsetMm((float)offset_mm))
  {
    return false;
  }

  return saveMovementConfig();
}

static bool http_apply_movement_x_range_submit(const char *request)
{
  /*
   * What: apply the maximum logical X displacement from the web page.
   * How: validates the millimetre range and clamps any pending X target to it.
   * Why: the same safety limit must be shared by manual, EPH and automatic movement.
   */
  double range_mm = 0.0;

  if ((!http_get_query_double(request, "x_range", &range_mm)) ||
      (!MovementParameters_IsRangeMmValid((float)range_mm)))
  {
    return false;
  }

  if (!MovementParameters_SetMaxXMM((float)range_mm))
  {
    return false;
  }

  g_x_target = MovementParameters_ClampXTarget(g_x_target);
  return saveMovementConfig();
}

static bool http_apply_movement_z_range_submit(const char *request)
{
  /*
   * What: apply the maximum logical Z displacement from the web page.
   * How: validates the millimetre range and clamps any pending Z target to it.
   * Why: this keeps the physical stop protection configurable without rebuilding firmware.
   */
  double range_mm = 0.0;

  if ((!http_get_query_double(request, "z_range", &range_mm)) ||
      (!MovementParameters_IsRangeMmValid((float)range_mm)))
  {
    return false;
  }

  if (!MovementParameters_SetMaxZMM((float)range_mm))
  {
    return false;
  }

  g_z_target = MovementParameters_ClampZTarget(g_z_target);
  return saveMovementConfig();
}

static bool http_apply_movement_speed_submit(const char *request)
{
  /*
   * What: apply the normal movement STEP delay submitted from the web page.
   * How: validates an integer microsecond delay and saves it as a movement parameter.
   * Why: movement speed tuning should not require editing movement.cpp.
   */
  uint32_t speed_us = 0U;

  if ((!http_get_query_uint32(request, "movement_speed", &speed_us)) ||
      (!MovementParameters_IsSpeedUsValid(speed_us)))
  {
    return false;
  }

  if (!MovementParameters_SetMovementSpeedUs(speed_us))
  {
    return false;
  }

  return saveMovementConfig();
}

static bool http_apply_movement_home_speed_submit(const char *request)
{
  /*
   * What: apply the first-touch homing STEP delay submitted from the web page.
   * How: validates an integer microsecond delay and leaves second-touch speed fixed.
   * Why: coarse homing speed is useful to tune, but final switch contact must stay gentle.
   */
  uint32_t speed_us = 0U;

  if ((!http_get_query_uint32(request, "home_speed", &speed_us)) ||
      (!MovementParameters_IsSpeedUsValid(speed_us)))
  {
    return false;
  }

  if (!MovementParameters_SetHomeSpeedUs(speed_us))
  {
    return false;
  }

  return saveMovementConfig();
}

static bool http_apply_manual_datetime(const char *request)
{
  /*
   * What: apply a manually entered local date/time from the web form.
   * How: parses YYYY-MM-DD and HH:MM:SS, loads RTC with setManualTime(), then persists it.
   * Why: manual time must survive reset until the user presses Sync Time and GPS takes over.
   */
  char date_value[16];
  char time_value[16];
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;

  /* Date and time are submitted as form fields in local time. */
  if ((!http_get_query_param(request, "date", date_value, sizeof(date_value))) ||
      (!http_get_query_param(request, "time", time_value, sizeof(time_value))))
  {
    return false;
  }

  if ((sscanf(date_value, "%d-%d-%d", &year, &month, &day) != 3) ||
      (sscanf(time_value, "%d:%d:%d", &hour, &minute, &second) != 3))
  {
    return false;
  }

  if ((year < 2000) || (year > 3000) ||
      (month < 1) || (month > 12) ||
      (day < 1) || (day > 31) ||
      (hour < 0) || (hour > 23) ||
      (minute < 0) || (minute > 59) ||
      (second < 0) || (second > 59))
  {
    return false;
  }

  setManualTime(year, month, day, hour, minute, second);

  return saveManualTime(year, month, day, hour, minute, second);
}

static void http_get_rtc_datetime(char *date_str, size_t date_size,
                                  char *time_str, size_t time_size)
{
  /*
   * What: read the current STM32 RTC value for status JSON.
   * How: gets time before date, as required by STM32 HAL, then formats ISO-like strings.
   * Why: the web page needs to show whether manual/GPS sync actually changed the clock.
   */
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  if ((HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) ||
      (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK))
  {
    (void)snprintf(date_str, date_size, "0000-00-00");
    (void)snprintf(time_str, time_size, "00:00:00");
    return;
  }

  (void)snprintf(date_str, date_size, "20%02" PRIu32 "-%02" PRIu32 "-%02" PRIu32,
                 (uint32_t)rtc_date.Year,
                 (uint32_t)rtc_date.Month,
                 (uint32_t)rtc_date.Date);
  (void)snprintf(time_str, time_size, "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
                 (uint32_t)rtc_time.Hours,
                 (uint32_t)rtc_time.Minutes,
                 (uint32_t)rtc_time.Seconds);
}

static bool http_apply_eph_submit(const char *request)
{
  char value[64];
  double azimuth = 0.0;
  double elevation = 0.0;

  if (http_get_query_param(request, "azimuth", value, sizeof(value)))
  {
    azimuth = strtod(value, NULL);
  }
  else
  {
    return false;
  }

  if (http_get_query_param(request, "elevation", value, sizeof(value)))
  {
    elevation = strtod(value, NULL);
  }
  else
  {
    return false;
  }

  g_AOIInputs.azimuth = azimuth;
  g_AOIInputs.elevation = elevation;

  return fsmPostEvent(submit_eph_input);
}

static bool http_apply_manual_goto(const char *request)
{
  char value[64];
  double x = 0.0;
  double z = 0.0;

  /* Both manual targets are required; an empty query field is currently parsed as zero. */
  if (http_get_query_param(request, "x", value, sizeof(value)))
  {
    x = strtod(value, NULL);
  }
  else
  {
    return false;
  }

  if (http_get_query_param(request, "z", value, sizeof(value)))
  {
    z = strtod(value, NULL);
  }
  else
  {
    return false;
  }

  /*
   * What: store the manual destination requested by the web form.
   * How: writes target globals; movement_task copies them to g_x_val/g_z_val only after moving.
   * Why: status/storage should show the accepted position, not a movement still pending in the queue.
   */
  g_x_target = MovementParameters_ClampXTarget((float)x);
  g_z_target = MovementParameters_ClampZTarget((float)z);

  return fsmPostEvent(submit_manual_goto);
}

static void http_send_html_message(int32_t client, const char *title,
                                   const char *message, bool is_error)
{
  char header[250];
  char body[768];
  const char *color = is_error ? "#c00000" : "#008000";
  const char *status = is_error ? "HTTP/1.1 403 Forbidden" : "HTTP/1.1 200 OK";
  int32_t body_len;
  int32_t header_len;

  body_len = snprintf(body, sizeof(body),
                      "<!DOCTYPE html>"
                      "<html><head>"
                      "<title>Window Control</title>"
                      "<meta charset=\"UTF-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                      "</head>"
                      "<body style=\"font-family:Arial,sans-serif;color:#03234b;\">"
                      "<h2 style=\"color:%s;\">%s</h2>"
                      "<p>%s</p>"
                      "<p><a href=\"/\">Back to control page</a></p>"
                      "</body></html>",
                      color,
                      title,
                      message);

  if (body_len < 0)
  {
    return;
  }

  header_len = snprintf(header, sizeof(header),
                        "%s\r\n"
                        "Server: U5\r\n"
                        "Access-Control-Allow-Origin: * \r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: %" PRIu32 "\r\n\r\n",
                        status,
                        (uint32_t)strlen(body));

  if (header_len < 0)
  {
    return;
  }

  (void)http_server_write(client, header, (size_t)header_len);
  (void)http_server_write(client, body, strlen(body));
}

static bool http_accept_client_or_reject(int32_t client)
{
  bool accepted = false;
  uint32_t client_bit;

  if ((client < 0) || (client >= 32))
  {
    return false;
  }

  client_bit = (1UL << (uint32_t)client);

  http_configure_client_socket(client);

  taskENTER_CRITICAL();
  if (http_active_clients < HTTP_MAX_ACTIVE_CLIENTS)
  {
    http_active_clients++;
    http_tracked_clients_mask |= client_bit;
    accepted = true;
  }
  taskEXIT_CRITICAL();

  if (!accepted)
  {
    http_reject_busy_client(client);
  }

  return accepted;
}

static void http_configure_client_socket(int32_t client)
{
  int32_t timeout = (int32_t)pdMS_TO_TICKS(SOCKET_TIMEOUT_MS);

  (void)W6X_Net_Setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout));
  (void)W6X_Net_Setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (void *)&timeout, sizeof(timeout));
}

static void http_release_client_slot(int32_t client)
{
  uint32_t client_bit;

  if ((client < 0) || (client >= 32))
  {
    return;
  }

  client_bit = (1UL << (uint32_t)client);

  taskENTER_CRITICAL();
  if ((http_tracked_clients_mask & client_bit) != 0U)
  {
    http_tracked_clients_mask &= ~client_bit;
    if (http_active_clients > 0U)
    {
      http_active_clients--;
    }
  }
  taskEXIT_CRITICAL();
}

static void http_reject_busy_client(int32_t client)
{
  static const char busy_response[] =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Server: U5\r\n"
    "Access-Control-Allow-Origin: * \r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n"
    "Retry-After: 2\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: 4\r\n\r\n"
    "BUSY";

  (void)http_server_write(client, busy_response, sizeof(busy_response) - 1U);
}

/* USER CODE END FD */

/* Private Functions Definition ----------------------------------------------*/
static void http_server_serve_task(void *arg)
{
  int32_t client = *((int32_t *)arg);
  int32_t bytes_received;
  int32_t recv_total_len = 0;
  bool request_complete = false;
  /* Allocate considering the biggest buffer the client can send */
  ssize_t recv_buffer_len = HTTP_CHILD_TASK_BUFFER_SIZE;
  char *recv_buffer = (char *)pvPortMalloc(recv_buffer_len + 1);

  if (recv_buffer == NULL)
  {
    LogError("Unable to allocate recv buffer\n");
    goto _err;
  }

  /* USER CODE BEGIN http_server_serve_task_1 */
  if (!http_accept_client_or_reject(client))
  {
    goto _close;
  }

  /* USER CODE END http_server_serve_task_1 */

  /* Read in the request */
  recv_buffer[0] = '\0';
  do
  {
    bytes_received = W6X_Net_Recv(client, (uint8_t *)&recv_buffer[recv_total_len], recv_buffer_len, 0);
    if (bytes_received <= 0) /* No data received or error */
    {
      break;
    }

    recv_total_len += bytes_received;

    /* Verify if we have receive the whole request or if we need to do another receive */
    if ((recv_total_len >= 4) &&
        (strncmp(&recv_buffer[recv_total_len - 4], "\r\n\r\n", 4) == 0))
    {
      /* The full request has been received leaving the reading loop */
      request_complete = true;
      break;
    }

    recv_buffer_len -= bytes_received;
  } while (recv_buffer_len > 0);

  if (!request_complete)
  {
    if (recv_total_len == 0)
    {
      LogError("No data read on the socket, closing the socket\n");
    }
    else
    {
      LogError("Data received does not contain the whole request, receive buffer is too small, closing the socket\n");
    }
    goto _close;
  }

  LogDebug("\n %" PRIi32 " >>> %" PRIi32 " <<<\n", client, recv_total_len);

  /* Count can be negative. */
  recv_buffer[recv_total_len] = '\0';
  LogDebug("\n %" PRIi32 " >>> %s <<<\n", client, recv_buffer);

  /* USER CODE BEGIN http_server_serve_task_2 */

  /* USER CODE END http_server_serve_task_2 */

  /* Process the response */
  http_process_response(client, recv_buffer);

  /* USER CODE BEGIN http_server_serve_task_last */

  /* USER CODE END http_server_serve_task_last */

_close:
  vPortFree(recv_buffer);
_err:
  (void)close_client(client);
  vTaskDelete(NULL);
}

static void pin_verification_task(void *arg)
{
  UNUSED(arg);
  GPIO_PinState Btn;
  GPIO_PinState LedGreen;
  GPIO_PinState LedRed;

  /* USER CODE BEGIN pin_verification_task_1 */

  /* USER CODE END pin_verification_task_1 */

  while (true)
  {
    while (true) /* Wait for the button or the LEDs to change */
    {
      vTaskDelay(pdMS_TO_TICKS(50));
      Btn = (GPIO_PinState)button_changed;
      LedGreen = HAL_GPIO_ReadPin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
      LedRed = HAL_GPIO_ReadPin(LED_RED_GPIO_Port, LED_RED_Pin);

      if (Btn != pins_info.btn_state)
      {
        break;
      }
      if (LedGreen != pins_info.led_green_state)
      {
        break;
      }
      if (LedRed != pins_info.led_red_state)
      {
        break;
      }
    }

    /* Update the button and the LEDs states */
    pins_info.btn_state = Btn;
    pins_info.led_green_state = LedGreen;
    pins_info.led_red_state = LedRed;

    /* Notify the HTTP server task */
    (void)xEventGroupSetBits(pin_handle, EVENT_FLAG_PIN);
  }
}

static int32_t http_server_write(int32_t client, const char *buffer, size_t buffer_size)
{
  int32_t bytes_sent = 0;

  /* USER CODE BEGIN http_server_write_1 */

  /* USER CODE END http_server_write_1 */

  LogDebug("[%" PRIi32 "] *****> %s<*****\n", client, buffer);

  do /* Send the data. Can be done in multiple steps. */
  {
    bytes_sent = W6X_Net_Send(client, (void *)buffer, buffer_size, 0);
    if (bytes_sent <= 0)
    {
      LogError("[%" PRIi32 "] *****> SEND ERROR <*****\n", client);
      return 1;
    }
    buffer_size -= bytes_sent;
    buffer += bytes_sent;
  } while (buffer_size > 0);

  /* USER CODE BEGIN http_server_write_last */

  /* USER CODE END http_server_write_last */

  return 0;
}

static int32_t close_client(int32_t client)
{
  /* USER CODE BEGIN close_client_1 */
  http_release_client_slot(client);

  /* USER CODE END close_client_1 */

  /* Close the connection */
  (void)W6X_Net_Close(client);
  LogDebug("!!! Closed connection <%" PRIi32 "> !!!\n", client);

  /* USER CODE BEGIN close_client_last */

  /* USER CODE END close_client_last */

  return 0;
}

static void http_process_response(int32_t client, char *recv_buffer)
{
  HttpServer_response_e response = UNKNOWN_RESPONSE;
  const char *response_data = response_error_404_html; /* Request not recognized, return 404 error */
  size_t resp_len = sizeof(response_error_404_html);
  char response_template[250] =
  {
    "HTTP/1.1 200 OK\r\n"
    "Server: U5\r\n"
    "Access-Control-Allow-Origin: * \r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
  };

  /* USER CODE BEGIN http_process_response_1 */

  /* USER CODE END http_process_response_1 */

  /* Check the request to determine the response */
  for (uint32_t i = 0; i < (sizeof(http_server_responses) / sizeof(http_server_responses[0])); i++)
  {
    if (strncmp(recv_buffer, http_server_responses[i].request, strlen(http_server_responses[i].request)) == 0)
    {
      response = http_server_responses[i].response_type;
      response_data = (char *)http_server_responses[i].response;
      resp_len = http_server_responses[i].resp_len;
      break;
    }
  }

  /* USER CODE BEGIN http_process_response_2 */
  if (strncmp(recv_buffer, "GET /reset_alarm", strlen("GET /reset_alarm")) == 0)
  {
    (void)fsmPostEvent(reset_alarm_request);
    (void)http_server_write(client, response_ok_html, sizeof(response_ok_html));
    return;
  }

  if (strncmp(recv_buffer, "GET /status", strlen("GET /status")) == 0)
  {
    char *data = (char *)pvPortMalloc(4096U);
    char date_str[11];
    char time_str[9];
    MovementAlarmSnapshot alarm_snapshot;

    if (data == NULL)
    {
      const char error_data[] = "{\"error\":\"Unable to allocate status buffer\"}";

      resp_len = strlen(response_template);
      resp_len += snprintf(&response_template[strlen(response_template)],
                           sizeof(response_template) - strlen(response_template),
                           "Content-Length: %" PRIu32 "\r\n\r\n%s", (uint32_t)strlen(error_data), error_data);

      (void)http_server_write(client, response_template, resp_len);
      return;
    }

    http_get_rtc_datetime(date_str, sizeof(date_str), time_str, sizeof(time_str));
    MovementAlarm_GetSnapshot(&alarm_snapshot);

    (void)snprintf(data, 4096U,
                   "{"
                   "\"fsm_state\":\"%s\","
                   "\"auto_on\":%s,\"auto_counter\":%d,"
                   "\"rtc_date\":\"%s\",\"rtc_time\":\"%s\","
                   "\"manual_time\":%s,"
                   "\"country\":\"%s\","
                   "\"latitude\":%.2f,\"longitude\":%.2f,\"pan\":%.2f,\"tilt\":%.2f,"
                   "\"tilt_correction\":%s,"
                   "\"movement_gain\":%.6f,\"movement_offset\":%.6f,"
                   "\"vertical_movement_gain\":%.6f,\"vertical_movement_offset\":%.6f,"
                   "\"movement_max_x\":%.3f,\"movement_max_z\":%.3f,"
                   "\"movement_speed\":%" PRIu32 ",\"movement_home_speed\":%" PRIu32 ","
                   "\"any_movement_alarm\":%s,"
                   "\"vertical_top_right_alarm\":%" PRIu32 ","
                   "\"vertical_top_left_alarm\":%" PRIu32 ","
                   "\"horizontal_interior_left_alarm\":%" PRIu32 ","
                   "\"horizontal_interior_right_alarm\":%" PRIu32 ","
                   "\"vertical_bottom_left_alarm\":%" PRIu32 ","
                   "\"vertical_bottom_right_alarm\":%" PRIu32 ","
                   "\"horizontal_exterior_left_alarm\":%" PRIu32 ","
                   "\"horizontal_exterior_right_alarm\":%" PRIu32 ","
                   "\"azimuth\":%.6f,\"elevation\":%.6f,"
                   "\"aoil\":%.6f,\"aoit\":%.6f,"
                   "\"query_aoil\":%.6f,\"query_aoit\":%.6f,"
                   "\"interp_x\":%.6f,\"interp_z\":%.6f,"
                   "\"x_target\":%.6f,\"z_target\":%.6f,"
                   "\"x\":%.6f,\"z\":%.6f,"
                   "\"gps_task_loop_count\":%" PRIu32 ","
                   "\"gps_power_enabled\":%" PRIu32 ","
                   "\"gps_line_count\":%" PRIu32 ","
                   "\"gps_rmc_count\":%" PRIu32 ","
                   "\"gps_overflow_count\":%" PRIu32 ","
                   "\"gps_line_ready\":%" PRIu32 ","
                   "\"gps_utc_ready\":%" PRIu32 ","
                   "\"gps_fix_valid\":%" PRIu32 ","
                   "\"gps_local_ready\":%" PRIu32 ","
                   "\"gps_timezone_offset_hours\":%" PRIi32 ","
                   "\"gps_time_sync_requested\":%" PRIu32 ","
                   "\"gps_rtc_synced\":%" PRIu32 ","
                   "\"gps_rtc_sync_count\":%" PRIu32 ","
                   "\"gps_rtc_sync_error_count\":%" PRIu32 ","
                   "\"gps_utc\":\"%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32 " %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "\","
                   "\"gps_local\":\"%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32 " %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "\","
                   "\"gps_last_line\":\"%s\","
                   "\"gps_last_rmc\":\"%s\","
                   "\"gps_nmea_available\":%s"
                   "}",
                   stateToText(fsmGetState()),
                   auto_on ? "true" : "false",
                   auto_counter,
                   date_str,
                   time_str,
                   manual_time ? "true" : "false",
                   g_country,
                   g_SPAInputs.latitude,
                   g_SPAInputs.longitude,
                   g_AOIInputs.pan,
                   g_AOIInputs.tilt,
                   g_AOIInputs.tilt_correction ? "true" : "false",
                   (double)g_movement_hysteresis_gain,
                   (double)g_movement_hysteresis_offset_mm,
                   (double)g_vertical_movement_hysteresis_gain,
                   (double)g_vertical_movement_hysteresis_offset_mm,
                   (double)g_movement_max_x_mm,
                   (double)g_movement_max_z_mm,
                   (uint32_t)g_movement_speed_us,
                   (uint32_t)g_movement_home_speed_us,
                   g_any_movement_alarm ? "true" : "false",
                   (uint32_t)alarm_snapshot.vertical_top_right,
                   (uint32_t)alarm_snapshot.vertical_top_left,
                   (uint32_t)alarm_snapshot.horizontal_interior_left,
                   (uint32_t)alarm_snapshot.horizontal_interior_right,
                   (uint32_t)alarm_snapshot.vertical_bottom_left,
                   (uint32_t)alarm_snapshot.vertical_bottom_right,
                   (uint32_t)alarm_snapshot.horizontal_exterior_left,
                   (uint32_t)alarm_snapshot.horizontal_exterior_right,
                   g_AOIInputs.azimuth,
                   g_AOIInputs.elevation,
                   (double)g_InterpolInputs.AOIl,
                   (double)g_InterpolInputs.AOIt,
                   (double)g_query_aoil,
                   (double)g_query_aoit,
                   (double)g_interp_x_val,
                   (double)g_interp_z_val,
                   (double)g_x_target,
                   (double)g_z_target,
                   (double)g_x_val,
                   (double)g_z_val,
                   (uint32_t)g_gps_task_loop_count,
                   (uint32_t)g_gps_power_enabled,
                   (uint32_t)g_gps_line_count,
                   (uint32_t)g_gps_rmc_count,
                   (uint32_t)g_gps_overflow_count,
                   (uint32_t)g_gps_line_ready,
                   (uint32_t)g_gps_utc_ready,
                   (uint32_t)g_gps_fix_valid,
                   (uint32_t)g_gps_local_ready,
                   (int32_t)g_gps_timezone_offset_hours,
                   (uint32_t)g_gps_time_sync_requested,
                   (uint32_t)g_gps_rtc_synced,
                   (uint32_t)g_gps_rtc_sync_count,
                   (uint32_t)g_gps_rtc_sync_error_count,
                   (uint32_t)g_gps_utc_year,
                   (uint32_t)g_gps_utc_month,
                   (uint32_t)g_gps_utc_day,
                   (uint32_t)g_gps_utc_hour,
                   (uint32_t)g_gps_utc_minute,
                   (uint32_t)g_gps_utc_second,
                   (uint32_t)g_gps_local_year,
                   (uint32_t)g_gps_local_month,
                   (uint32_t)g_gps_local_day,
                   (uint32_t)g_gps_local_hour,
                   (uint32_t)g_gps_local_minute,
                   (uint32_t)g_gps_local_second,
                   g_gps_last_line,
                   g_gps_last_rmc,
                   (g_gps_line_ready != 0U) ? "true" : "false");

    resp_len = strlen(response_template);
    resp_len += snprintf(&response_template[strlen(response_template)],
                         sizeof(response_template) - strlen(response_template),
                         "Content-Length: %" PRIu32 "\r\n\r\n", (uint32_t)strlen(data));

    (void)http_server_write(client, response_template, resp_len);
    (void)http_server_write(client, data, strlen(data));
    vPortFree(data);
    return;
  }

  if (strncmp(recv_buffer, "GET /config_submit", strlen("GET /config_submit")) == 0)
  {
    bool config_ok = http_apply_config_submit(recv_buffer);

    if (config_ok)
    {
      LogInfo("[CONFIG] Data received from web form: latitude=%.2f longitude=%.2f pan=%.2f tilt=%.2f tilt_correction=%s country=%s\n",
              g_SPAInputs.latitude,
              g_SPAInputs.longitude,
              g_AOIInputs.pan,
              g_AOIInputs.tilt,
              g_AOIInputs.tilt_correction ? "true" : "false",
              g_country);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong parameters",
                             "Fill all configuration fields. Latitude must be between -90 and 90, and longitude must be between -180 and 180.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /movement_gain_submit", strlen("GET /movement_gain_submit")) == 0)
  {
    bool gain_ok = http_apply_movement_gain_submit(recv_buffer);

    if (gain_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Horizontal hysteresis gain=%.6f\n",
              (double)g_movement_hysteresis_gain);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong movement gain",
                             "Fill a numeric movement gain between -1 and 1.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /movement_offset_submit", strlen("GET /movement_offset_submit")) == 0)
  {
    bool offset_ok = http_apply_movement_offset_submit(recv_buffer);

    if (offset_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Horizontal hysteresis offset=%.3f mm\n",
              (double)g_movement_hysteresis_offset_mm);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong movement offset",
                             "Fill a numeric movement offset between -50 mm and 50 mm.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /vertical_movement_gain_submit", strlen("GET /vertical_movement_gain_submit")) == 0)
  {
    bool gain_ok = http_apply_vertical_movement_gain_submit(recv_buffer);

    if (gain_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Vertical hysteresis gain=%.6f\n",
              (double)g_vertical_movement_hysteresis_gain);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong vertical movement gain",
                             "Fill a numeric vertical movement gain between -1 and 1.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /vertical_movement_offset_submit", strlen("GET /vertical_movement_offset_submit")) == 0)
  {
    bool offset_ok = http_apply_vertical_movement_offset_submit(recv_buffer);

    if (offset_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Vertical hysteresis offset=%.3f mm\n",
              (double)g_vertical_movement_hysteresis_offset_mm);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong vertical movement offset",
                             "Fill a numeric vertical movement offset between -50 mm and 50 mm.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /movement_x_range_submit", strlen("GET /movement_x_range_submit")) == 0)
  {
    bool range_ok = http_apply_movement_x_range_submit(recv_buffer);

    if (range_ok)
    {
      LogInfo("[MOVEMENT CONFIG] X range=%.3f mm\n",
              (double)g_movement_max_x_mm);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong X range",
                             "Fill a numeric X range between 0 mm and 200 mm.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /movement_z_range_submit", strlen("GET /movement_z_range_submit")) == 0)
  {
    bool range_ok = http_apply_movement_z_range_submit(recv_buffer);

    if (range_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Z range=%.3f mm\n",
              (double)g_movement_max_z_mm);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong Z range",
                             "Fill a numeric Z range between 0 mm and 200 mm.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /movement_speed_submit", strlen("GET /movement_speed_submit")) == 0)
  {
    bool speed_ok = http_apply_movement_speed_submit(recv_buffer);

    if (speed_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Movement step delay=%" PRIu32 " us\n",
              (uint32_t)g_movement_speed_us);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong movement speed",
                             "Fill an integer movement step delay between 100 us and 10000 us.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /movement_home_speed_submit", strlen("GET /movement_home_speed_submit")) == 0)
  {
    bool speed_ok = http_apply_movement_home_speed_submit(recv_buffer);

    if (speed_ok)
    {
      LogInfo("[MOVEMENT CONFIG] Home step delay=%" PRIu32 " us\n",
              (uint32_t)g_movement_home_speed_us);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong home speed",
                             "Fill an integer homing step delay between 100 us and 10000 us. Second touch stays fixed.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if ((strncmp(recv_buffer, "POST /set_datetime", strlen("POST /set_datetime")) == 0) ||
      (strncmp(recv_buffer, "GET /set_datetime", strlen("GET /set_datetime")) == 0))
  {
    bool datetime_ok;

    if (fsmGetState() != STDBY)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Manual date and time can only be changed from STDBY.",
                             true);
      return;
    }

    datetime_ok = http_apply_manual_datetime(recv_buffer);

    if (datetime_ok)
    {
      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Wrong date/time",
                             "Use date format YYYY-MM-DD and time format HH:MM:SS.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /sync_time", strlen("GET /sync_time")) == 0)
  {
    char data[80];
    char date_str[11];
    char time_str[9];
    uint8_t synced;

    /* Sync Time is state-independent because it only changes RTC ownership/time. */
    synced = GPS_Task_SyncTimeNow();
    http_get_rtc_datetime(date_str, sizeof(date_str), time_str, sizeof(time_str));

    (void)snprintf(data, sizeof(data),
                   "{\"synced\":%s,\"date\":\"%s\",\"time\":\"%s\"}",
                   synced ? "true" : "false",
                   date_str,
                   time_str);

    resp_len = strlen(response_template);
    resp_len += snprintf(&response_template[strlen(response_template)],
                         sizeof(response_template) - strlen(response_template),
                         "Content-Length: %" PRIu32 "\r\n\r\n%s",
                         (uint32_t)strlen(data),
                         data);

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /shutdown_submit", strlen("GET /shutdown_submit")) == 0)
  {
    ControledShutdownStatus_t wifi_off_status;

    wifi_off_status = ControledShutdown_RequestWifiOffTest();
    if (wifi_off_status == CONTROLED_SHUTDOWN_BUSY)
    {
      http_send_html_message(client,
                             "WiFi off already running",
                             "The WiFi-off test task is already running. Wait a few seconds before trying again.",
                             true);
      return;
    }

    if (wifi_off_status != CONTROLED_SHUTDOWN_OK)
    {
      http_send_html_message(client,
                             "WiFi off blocked",
                             "The WiFi-off test task could not be created.",
                             true);
      return;
    }

    /*
     * What: acknowledge the web command before the WiFi module is deinitialized.
     * How: the created task waits briefly, then stops SoftAP and deinitializes the ST67.
     * Why: the STM32U575 must stay alive so current measurement isolates the WiFi module.
     */
    http_send_html_message(client,
                           "WiFi off test armed",
                           "The STM32U575 stays running. The ST67 WiFi module should stop after this response.",
                           false);
    return;
  }

  if (strncmp(recv_buffer, "GET /auto_mode_toggle", strlen("GET /auto_mode_toggle")) == 0)
  {
    States current_state = fsmGetState();

    if ((current_state != STDBY) && (current_state != AUTO_MODE))
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Auto mode can only be toggled from STDBY or AUTO_MODE. End manual or ephemeris mode first.",
                             true);
      return;
    }

    (void)fsmPostEvent(toggle_auto_mode);

    (void)http_server_write(client, response_ok_html, sizeof(response_ok_html));
    return;
  }

  if (strncmp(recv_buffer, "GET /auto_mode_on", strlen("GET /auto_mode_on")) == 0)
  {
    if (fsmGetState() != STDBY)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Auto mode can only start from STDBY.",
                             true);
      return;
    }

    if (fsmGetState() != AUTO_MODE)
    {
      (void)fsmPostEvent(toggle_auto_mode);
    }

    (void)http_server_write(client, response_ok_html, sizeof(response_ok_html));
    return;
  }

  if (strncmp(recv_buffer, "GET /auto_mode_off", strlen("GET /auto_mode_off")) == 0)
  {
    if (fsmGetState() != AUTO_MODE)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Auto mode is not active.",
                             true);
      return;
    }

    if (fsmGetState() == AUTO_MODE)
    {
      (void)fsmPostEvent(toggle_auto_mode);
    }

    (void)http_server_write(client, response_ok_html, sizeof(response_ok_html));
    return;
  }

  if (strncmp(recv_buffer, "GET /eph_begin", strlen("GET /eph_begin")) == 0)
  {
    if (fsmGetState() == AUTO_MODE)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Auto mode must be stopped before starting ephemeris input.",
                             true);
      return;
    }

    (void)fsmPostEvent(begin_eph_input);

    resp_len = snprintf(response_template, sizeof(response_template),
                        "HTTP/1.1 303 See Other\r\n"
                        "Server: U5\r\n"
                        "Access-Control-Allow-Origin: * \r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "Location: /\r\n"
                        "Content-Length: 0\r\n\r\n");
    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /end_eph", strlen("GET /end_eph")) == 0)
  {
    if (fsmGetState() != EPH_INPUT)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Ephemeris mode is not active.",
                             true);
      return;
    }

    (void)fsmPostEvent(end_eph_input);

    resp_len = snprintf(response_template, sizeof(response_template),
                        "HTTP/1.1 303 See Other\r\n"
                        "Server: U5\r\n"
                        "Access-Control-Allow-Origin: * \r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "Location: /\r\n"
                        "Content-Length: 0\r\n\r\n");
    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /manual_begin", strlen("GET /manual_begin")) == 0)
  {
    if (fsmGetState() == AUTO_MODE)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Auto mode must be stopped before starting manual mode.",
                             true);
      return;
    }

    (void)fsmPostEvent(begin_manual);

    resp_len = snprintf(response_template, sizeof(response_template),
                        "HTTP/1.1 303 See Other\r\n"
                        "Server: U5\r\n"
                        "Access-Control-Allow-Origin: * \r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "Location: /\r\n"
                        "Content-Length: 0\r\n\r\n");
    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /end_manual", strlen("GET /end_manual")) == 0)
  {
    if (fsmGetState() != MANUAL)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Manual mode is not active.",
                             true);
      return;
    }

    (void)fsmPostEvent(end_manual);

    resp_len = snprintf(response_template, sizeof(response_template),
                        "HTTP/1.1 303 See Other\r\n"
                        "Server: U5\r\n"
                        "Access-Control-Allow-Origin: * \r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "Location: /\r\n"
                        "Content-Length: 0\r\n\r\n");
    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /eph_submit", strlen("GET /eph_submit")) == 0)
  {
    if (fsmGetState() != EPH_INPUT)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Press Begin in Ephemeris Input before submitting azimuth and elevation.",
                             true);
      return;
    }

    bool eph_ok = http_apply_eph_submit(recv_buffer);

    if (eph_ok)
    {
      LogInfo("[EPH] Data received from web form: azimuth=%.2f elevation=%.2f\n",
              g_AOIInputs.azimuth,
              g_AOIInputs.elevation);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Missing parameters",
                             "Fill azimuth and elevation before submitting ephemeris input.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if (strncmp(recv_buffer, "GET /manual_goto", strlen("GET /manual_goto")) == 0)
  {
    if (fsmGetState() != MANUAL)
    {
      http_send_html_message(client,
                             "Action blocked",
                             "Press Begin in Manual Mode before sending X/Z targets.",
                             true);
      return;
    }

    bool manual_ok = http_apply_manual_goto(recv_buffer);

    if (manual_ok)
    {
      LogInfo("[MANUAL] Goto received from web form: x=%.6f z=%.6f\n",
              (double)g_x_val,
              (double)g_z_val);

      resp_len = snprintf(response_template, sizeof(response_template),
                          "HTTP/1.1 303 See Other\r\n"
                          "Server: U5\r\n"
                          "Access-Control-Allow-Origin: * \r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Location: /\r\n"
                          "Content-Length: 0\r\n\r\n");
    }
    else
    {
      http_send_html_message(client,
                             "Missing parameters",
                             "Fill X target and Z target before submitting manual movement.",
                             true);
      return;
    }

    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  if ((strncmp(recv_buffer, "GET /home", strlen("GET /home")) == 0) ||
      (strncmp(recv_buffer, "GET /manual_home", strlen("GET /manual_home")) == 0))
  {
    (void)fsmPostEvent(submit_home);

    resp_len = snprintf(response_template, sizeof(response_template),
                        "HTTP/1.1 200 OK\r\n"
                        "Server: U5\r\n"
                        "Access-Control-Allow-Origin: * \r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "Content-Type: text/plain; charset=utf-8\r\n"
                        "Content-Length: 2\r\n\r\n"
                        "OK");
    (void)http_server_write(client, response_template, resp_len);
    return;
  }

  /* USER CODE END http_process_response_2 */

  if (response == BUTTON_STATE) /* Prepare a custom response for the button state */
  {
    LogInfo("\nPending request for the PIN STATUS received\n\n");
    /* Wait for the pin status update */
    (void)xEventGroupWaitBits(pin_handle, EVENT_FLAG_PIN, pdTRUE, pdFALSE, pdMS_TO_TICKS(PIN_TIMEOUT_MS));
    /* Send anyway the pin status update or the last known status if timeouted */
    /* Prepare the HTTP content data */
    char data[60];
    uint32_t green = (pins_info.led_green_state == GPIO_PIN_RESET) ? 0U : 1U;
    uint32_t red = (pins_info.led_red_state == GPIO_PIN_RESET) ? 0U : 1U;
    uint32_t button = (pins_info.btn_state == GPIO_PIN_RESET) ? 0U : 1U;
    (void)snprintf(data, sizeof(data),
                   "{\"LedGreenPin\":%" PRIu32 ",\"LedRedPin\":%" PRIu32 ",\"BtnPin\":%" PRIu32 "}",
                   green, red, button);

    resp_len = strlen(response_template);
    /* Append the content length and the data to the response */
    resp_len += snprintf(&response_template[strlen(response_template)],
                         sizeof(response_template) - strlen(response_template),
                         "Content-Length: %" PRIu32 "\r\n\r\n%s", (uint32_t)strlen(data), data);

    LogInfo("Pins status :\n%s\n", data);
    response_data = response_template;
  }
  else
  {
    /* Unknown request */
  }

  /* Send the response */
  if (1 == http_server_write(client, response_data, resp_len))
  {
    return;
  }

  /* Finalize the response */
  if (response == LED_RED_STATE) /* Toggle the red LED */
  {
    HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
  }
  else if (response == LED_GREEN_STATE) /* Toggle the green LED */
  {
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
  }
  else
  {
    /* Nothing to do */
  }

  /* USER CODE BEGIN http_process_response_last */

  /* USER CODE END http_process_response_last */
}

/* USER CODE BEGIN PFD */

/* USER CODE END PFD */
