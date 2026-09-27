#include "gps.h"
#include "cmsis_os2.h"
#include "global_structs.h"
#include "main.h"
#include "spa_func.h"
#include "storage.h"
#include <string.h>

extern RTC_HandleTypeDef hrtc; //main.c RTC access
extern UART_HandleTypeDef hlpuart1;

#define GPS_REQUIRE_FIX_FOR_RTC_SYNC 0U
#define GPS_SYNC_BYTES_PER_SLICE     128U
#define GPS_POWER_SETTLE_DELAY_MS    1U
#define GPS_ACTIVE_POLL_DELAY_MS     1U
#define GPS_IDLE_POLL_DELAY_MS       250U

/* Last complete NMEA sentences kept for live expressions/debug. */
char g_gps_last_line[GPS_NMEA_LINE_MAX];
char g_gps_last_rmc[GPS_NMEA_LINE_MAX];
volatile uint32_t g_gps_line_count = 0U;
volatile uint32_t g_gps_rmc_count = 0U;
volatile uint32_t g_gps_overflow_count = 0U;
volatile uint8_t g_gps_line_ready = 0U;

/* UTC date-time extracted from GPS NMEA time sentences. */
volatile uint8_t g_gps_utc_ready = 0U;
volatile uint8_t g_gps_fix_valid = 0U;
volatile uint16_t g_gps_utc_year = 0U;
volatile uint8_t g_gps_utc_month = 0U;
volatile uint8_t g_gps_utc_day = 0U;
volatile uint8_t g_gps_utc_hour = 0U;
volatile uint8_t g_gps_utc_minute = 0U;
volatile uint8_t g_gps_utc_second = 0U;

/* Local date-time after applying the configured country timezone. */
volatile int8_t g_gps_timezone_offset_hours = 0;
volatile uint8_t g_gps_local_ready = 0U;
volatile uint32_t g_gps_local_update_count = 0U;
volatile uint16_t g_gps_local_year = 0U;
volatile uint8_t g_gps_local_month = 0U;
volatile uint8_t g_gps_local_day = 0U;
volatile uint8_t g_gps_local_hour = 0U;
volatile uint8_t g_gps_local_minute = 0U;
volatile uint8_t g_gps_local_second = 0U;

/* Web-triggered RTC synchronization state. */
volatile uint8_t g_gps_time_sync_requested = 0U;
volatile uint8_t g_gps_rtc_synced = 0U;
volatile uint8_t g_gps_power_enabled = 0U;
volatile uint32_t g_gps_task_loop_count = 0U;
volatile uint32_t g_gps_rtc_sync_count = 0U;
volatile uint32_t g_gps_rtc_sync_error_count = 0U;

static char gps_line_buffer[GPS_NMEA_LINE_MAX];
static uint32_t gps_line_index = 0U;
static uint32_t gps_last_processed_rmc_count = 0U;
static uint8_t gps_receiving_line = 0U;

static void GPS_DelayMs(uint32_t delay_ms);
static void GPS_PowerOnForSync(void);
static void GPS_PowerOffAfterSync(void);
static void GPS_ClearTimeCapture(void);
static void GPS_BeginTimeSyncRequest(void);

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t fix_valid;
} GPS_UtcDateTime_t;

static bool GPS_IsDigit(char c)
{
    return (c >= '0') && (c <= '9');
}

static uint8_t GPS_Parse2Digits(const char *text)
{
    return (uint8_t)(((text[0] - '0') * 10) + (text[1] - '0'));
}

static uint16_t GPS_Parse4Digits(const char *text)
{
    return (uint16_t)(((text[0] - '0') * 1000U) +
                      ((text[1] - '0') * 100U) +
                      ((text[2] - '0') * 10U) +
                      (text[3] - '0'));
}

static int8_t GPS_HexToNibble(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return (int8_t)(c - '0');
    }

    if ((c >= 'A') && (c <= 'F'))
    {
        return (int8_t)(c - 'A' + 10);
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return (int8_t)(c - 'a' + 10);
    }

    return -1;
}

static bool GPS_IsNmeaChecksumValid(const char *line)
{
    uint8_t calculated_checksum = 0U;
    uint8_t expected_checksum;
    int8_t checksum_high;
    int8_t checksum_low;
    const char *checksum_start;

    if ((line == NULL) || (line[0] != '$'))
    {
        return false;
    }

    checksum_start = strchr(line, '*');
    if ((checksum_start == NULL) || (checksum_start[1] == '\0') || (checksum_start[2] == '\0'))
    {
        return false;
    }

    for (const char *p = line + 1; p < checksum_start; p++)
    {
        calculated_checksum ^= (uint8_t)(*p);
    }

    checksum_high = GPS_HexToNibble(checksum_start[1]);
    checksum_low = GPS_HexToNibble(checksum_start[2]);
    if ((checksum_high < 0) || (checksum_low < 0))
    {
        return false;
    }

    expected_checksum = (uint8_t)(((uint8_t)checksum_high << 4) | (uint8_t)checksum_low);
    return calculated_checksum == expected_checksum;
}

static bool GPS_IsRmcSentence(const char *line)
{
    /*
     * What: identify raw RMC sentences before validating their date/time fields.
     * How: checks the NMEA sentence type positions and ignores the talker prefix.
     * Why: the web Last RMC field must show received RMC lines even when GPS has no fix yet.
     */
    return ((line != NULL) &&
            (strlen(line) >= 7U) &&
            (line[0] == '$') &&
            (line[3] == 'R') &&
            (line[4] == 'M') &&
            (line[5] == 'C') &&
            (line[6] == ','));
}

static bool GPS_ParseRmcUtc(const char *line, GPS_UtcDateTime_t *utc)
{
    const char *fields[10] = {0};
    uint8_t field_index = 0U;
    const char *p;
    const char *time_field;
    const char *status_field;
    const char *date_field;

    if ((line == NULL) || (utc == NULL))
    {
        return false;
    }

    memset(utc, 0, sizeof(*utc));

    if ((strlen(line) < 7U) ||
        (line[0] != '$') ||
        (line[3] != 'R') ||
        (line[4] != 'M') ||
        (line[5] != 'C') ||
        (line[6] != ','))
    {
        return false;
    }

    fields[0] = line;

    for (p = line; (*p != '\0') && (*p != '*'); p++)
    {
        if (*p == ',')
        {
            field_index++;

            if (field_index < 10U)
            {
                fields[field_index] = p + 1;
            }
        }
    }

    time_field = fields[1];
    status_field = fields[2];
    date_field = fields[9];

    if ((time_field == NULL) || (status_field == NULL) || (date_field == NULL))
    {
        return false;
    }

    if (!GPS_IsDigit(time_field[0]) || !GPS_IsDigit(time_field[1]) ||
        !GPS_IsDigit(time_field[2]) || !GPS_IsDigit(time_field[3]) ||
        !GPS_IsDigit(time_field[4]) || !GPS_IsDigit(time_field[5]))
    {
        return false;
    }

    if (!GPS_IsDigit(date_field[0]) || !GPS_IsDigit(date_field[1]) ||
        !GPS_IsDigit(date_field[2]) || !GPS_IsDigit(date_field[3]) ||
        !GPS_IsDigit(date_field[4]) || !GPS_IsDigit(date_field[5]))
    {
        return false;
    }

    utc->hour = GPS_Parse2Digits(&time_field[0]);
    utc->minute = GPS_Parse2Digits(&time_field[2]);
    utc->second = GPS_Parse2Digits(&time_field[4]);
    utc->day = GPS_Parse2Digits(&date_field[0]);
    utc->month = GPS_Parse2Digits(&date_field[2]);
    utc->year = (uint16_t)(2000U + GPS_Parse2Digits(&date_field[4]));
    utc->fix_valid = (status_field[0] == 'A') ? 1U : 0U;

    return true;
}

static bool GPS_ParseZdaUtc(const char *line, GPS_UtcDateTime_t *utc)
{
    const char *fields[5] = {0};
    uint8_t field_index = 0U;
    const char *p;
    const char *time_field;
    const char *day_field;
    const char *month_field;
    const char *year_field;

    if ((line == NULL) || (utc == NULL))
    {
        return false;
    }

    memset(utc, 0, sizeof(*utc));

    if ((strlen(line) < 7U) ||
        (line[0] != '$') ||
        (line[3] != 'Z') ||
        (line[4] != 'D') ||
        (line[5] != 'A') ||
        (line[6] != ','))
    {
        return false;
    }

    fields[0] = line;

    for (p = line; (*p != '\0') && (*p != '*'); p++)
    {
        if (*p == ',')
        {
            field_index++;

            if (field_index < 5U)
            {
                fields[field_index] = p + 1;
            }
        }
    }

    time_field = fields[1];
    day_field = fields[2];
    month_field = fields[3];
    year_field = fields[4];

    if ((time_field == NULL) || (day_field == NULL) ||
        (month_field == NULL) || (year_field == NULL))
    {
        return false;
    }

    if (!GPS_IsDigit(time_field[0]) || !GPS_IsDigit(time_field[1]) ||
        !GPS_IsDigit(time_field[2]) || !GPS_IsDigit(time_field[3]) ||
        !GPS_IsDigit(time_field[4]) || !GPS_IsDigit(time_field[5]) ||
        !GPS_IsDigit(day_field[0]) || !GPS_IsDigit(day_field[1]) ||
        !GPS_IsDigit(month_field[0]) || !GPS_IsDigit(month_field[1]) ||
        !GPS_IsDigit(year_field[0]) || !GPS_IsDigit(year_field[1]) ||
        !GPS_IsDigit(year_field[2]) || !GPS_IsDigit(year_field[3]))
    {
        return false;
    }

    /*
     * What: parse the NMEA ZDA UTC calendar sentence.
     * How: hhmmss, day, month and year are copied into the common UTC structure.
     * Why: some GPS modules provide valid time in ZDA even when RMC is delayed/noisy.
     */
    utc->hour = GPS_Parse2Digits(&time_field[0]);
    utc->minute = GPS_Parse2Digits(&time_field[2]);
    utc->second = GPS_Parse2Digits(&time_field[4]);
    utc->day = GPS_Parse2Digits(&day_field[0]);
    utc->month = GPS_Parse2Digits(&month_field[0]);
    utc->year = GPS_Parse4Digits(&year_field[0]);
    utc->fix_valid = 1U;

    return true;
}

static void GPS_ResetRxState(void)
{
    /* Reset the partial line so the next '$' starts a clean NMEA frame. */
    gps_line_index = 0U;
    gps_receiving_line = 0U;
}

static void GPS_ClearUartErrorFlags(void)
{
    /*
     * What: recover the GPS UART after overrun/noise/framing errors.
     * How: clears HAL error flags and discards the partial NMEA line.
     * Why: one bad byte must not leave the GPS reader permanently stuck.
     */
    if (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&hlpuart1);
    }
    if (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_FE) != RESET)
    {
        __HAL_UART_CLEAR_FEFLAG(&hlpuart1);
    }
    if (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_NE) != RESET)
    {
        __HAL_UART_CLEAR_NEFLAG(&hlpuart1);
    }
    if (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_PE) != RESET)
    {
        __HAL_UART_CLEAR_PEFLAG(&hlpuart1);
    }

    GPS_ResetRxState();
}

static void GPS_DelayMs(uint32_t delay_ms)
{
    if (delay_ms == 0U)
    {
        return;
    }

    if (osKernelGetState() == osKernelRunning)
    {
        osDelay(delay_ms);
    }
    else
    {
        HAL_Delay(delay_ms);
    }
}

static void GPS_PowerOnForSync(void)
{
    if (g_gps_power_enabled != 0U)
    {
        return;
    }

    HAL_GPIO_WritePin(GPS_enable_GPIO_Port, GPS_enable_Pin, GPIO_PIN_SET);
    g_gps_power_enabled = 1U;
    GPS_ResetRxState();
    GPS_DelayMs(GPS_POWER_SETTLE_DELAY_MS);
    GPS_ClearUartErrorFlags();
}

static void GPS_PowerOffAfterSync(void)
{
    HAL_GPIO_WritePin(GPS_enable_GPIO_Port, GPS_enable_Pin, GPIO_PIN_RESET);
    g_gps_power_enabled = 0U;
    GPS_ResetRxState();
}

static void GPS_ClearTimeCapture(void)
{
    g_gps_line_ready = 0U;
    g_gps_utc_ready = 0U;
    g_gps_fix_valid = 0U;
    g_gps_local_ready = 0U;
    gps_last_processed_rmc_count = g_gps_rmc_count;
}

static void GPS_BeginTimeSyncRequest(void)
{
    /*
     * What: start a fresh GPS-powered RTC sync attempt.
     * How: clears stale parsed time, enables the GPS switch and lets the rail settle.
     * Why: pressing Sync Time must prove the GPS can provide a new time sentence.
     */
    if (g_gps_time_sync_requested == 0U)
    {
        GPS_ClearTimeCapture();
    }

    g_gps_time_sync_requested = 1U;
    g_gps_rtc_synced = 0U;
    GPS_PowerOnForSync();
}

static uint8_t GPS_IsLeapYear(uint16_t year)
{
    return (uint8_t)(((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U)));
}

static uint8_t GPS_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days_by_month[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if ((month < 1U) || (month > 12U))
    {
        return 31U;
    }

    if ((month == 2U) && (GPS_IsLeapYear(year) != 0U))
    {
        return 29U;
    }

    return days_by_month[month - 1U];
}

static void GPS_AddDay(uint16_t *year, uint8_t *month, uint8_t *day)
{
    (*day)++;

    if (*day <= GPS_DaysInMonth(*year, *month))
    {
        return;
    }

    *day = 1U;
    (*month)++;

    if (*month > 12U)
    {
        *month = 1U;
        (*year)++;
    }
}

static void GPS_SubtractDay(uint16_t *year, uint8_t *month, uint8_t *day)
{
    if (*day > 1U)
    {
        (*day)--;
        return;
    }

    if (*month > 1U)
    {
        (*month)--;
    }
    else
    {
        *month = 12U;
        (*year)--;
    }

    *day = GPS_DaysInMonth(*year, *month);
}

static void GPS_SaveLine(void)
{
    GPS_UtcDateTime_t utc;
    bool is_rmc_sentence;

    gps_line_buffer[gps_line_index] = '\0';
    memcpy(g_gps_last_line, gps_line_buffer, gps_line_index + 1U);
    g_gps_line_count++;
    is_rmc_sentence = GPS_IsRmcSentence(gps_line_buffer);

    if (is_rmc_sentence)
    {
        /*
         * What: keep the latest raw RMC sentence for the web debug panel.
         * How: copies it before field validation, so empty/no-fix RMC lines are still visible.
         * Why: this separates "RMC received" from "RMC had usable date/time".
         */
        memcpy(g_gps_last_rmc, gps_line_buffer, gps_line_index + 1U);
    }

    /*
     * What: report whether the last complete NMEA line contains usable date/time.
     * How: clear it for every new line and set it only after RMC/ZDA parses correctly.
     * Why: the web page must show YES only for the latest valid time sentence, not for old data.
     */
    g_gps_line_ready = 0U;

    if (!GPS_IsNmeaChecksumValid(gps_line_buffer))
    {
        gps_line_index = 0U;
        return;
    }

    if (GPS_ParseRmcUtc(gps_line_buffer, &utc) ||
        GPS_ParseZdaUtc(gps_line_buffer, &utc))
    {
        /*
         * What: store the latest accepted GPS time sentence.
         * How: updates the valid-time counters and keeps raw RMC display separate.
         * Why: the rest of the app only needs "new valid GPS time", not the sentence type.
         */
        g_gps_rmc_count++;
        g_gps_line_ready = 1U;
        g_gps_utc_ready = 1U;
        g_gps_fix_valid = utc.fix_valid;
        g_gps_utc_year = utc.year;
        g_gps_utc_month = utc.month;
        g_gps_utc_day = utc.day;
        g_gps_utc_hour = utc.hour;
        g_gps_utc_minute = utc.minute;
        g_gps_utc_second = utc.second;

        /* Blue LED ON means a GPS time sentence was accepted. */
        HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_SET);
    }

    gps_line_index = 0U;
}

uint8_t GPS_ProcessUart(void)
{
    uint8_t rx;
    HAL_StatusTypeDef rx_status;

    /* Poll one UART byte and rebuild complete NMEA lines. */
    rx_status = HAL_UART_Receive(&hlpuart1, &rx, 1U, 2U);
    if (rx_status == HAL_OK)
    {
        if (rx == '$')
        {
            gps_line_index = 0U;
            gps_line_buffer[gps_line_index] = (char)rx;
            gps_line_index++;
            gps_receiving_line = 1U;
        }
        else if (gps_receiving_line == 0U)
        {
            return 1U;
        }
        else if ((rx == '\r') || (rx == '\n'))
        {
            if (gps_line_index > 1U)
            {
                GPS_SaveLine();
            }
            else
            {
                gps_line_index = 0U;
            }

            gps_receiving_line = 0U;
        }
        else if ((rx >= 0x20U) && (rx <= 0x7EU))
        {
            if (gps_line_index < (GPS_NMEA_LINE_MAX - 1U))
            {
                gps_line_buffer[gps_line_index] = (char)rx;
                gps_line_index++;
            }
            else
            {
                GPS_ResetRxState();
                g_gps_overflow_count++;
            }
        }
        else
        {
            GPS_ResetRxState();
        }

        return 1U;
    }

    if (rx_status == HAL_ERROR)
    {
        GPS_ClearUartErrorFlags();
        g_gps_overflow_count++;
    }

    return 0U;
}

uint8_t GPS_UpdateLocalTimeFromUtc(void)
{
    int16_t local_hour;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    int timezone_offset;

    if ((g_gps_utc_ready == 0U) || (g_gps_rmc_count == gps_last_processed_rmc_count))
    {
        return 0U;
    }

    year = g_gps_utc_year;
    month = g_gps_utc_month;
    day = g_gps_utc_day;

    /* Use the same country/DST rule as the SPA calculation. */
    timezone_offset = getTimezoneForCountryName(g_country, (int)year, (int)month, (int)day);
    g_gps_timezone_offset_hours = (int8_t)timezone_offset;
    local_hour = (int16_t)g_gps_utc_hour + (int16_t)timezone_offset;

    while (local_hour >= 24)
    {
        local_hour -= 24;
        GPS_AddDay(&year, &month, &day);
    }

    while (local_hour < 0)
    {
        local_hour += 24;
        GPS_SubtractDay(&year, &month, &day);
    }

    g_gps_local_year = year;
    g_gps_local_month = month;
    g_gps_local_day = day;
    g_gps_local_hour = (uint8_t)local_hour;
    g_gps_local_minute = g_gps_utc_minute;
    g_gps_local_second = g_gps_utc_second;
    g_gps_local_ready = 1U;
    g_gps_local_update_count++;
    gps_last_processed_rmc_count = g_gps_rmc_count;

    return 1U;
}

static uint8_t GPS_SetRtcFromLocalTime(void)
{
    struct tm local_time = {0};

    if ((g_gps_local_ready == 0U) || (g_gps_local_year < 2020U))
    {
        return 0U;
    }

    if ((GPS_REQUIRE_FIX_FOR_RTC_SYNC != 0U) && (g_gps_fix_valid == 0U))
    {
        return 0U;
    }

    local_time.tm_year = (int)g_gps_local_year - 1900;
    local_time.tm_mon = (int)g_gps_local_month - 1;
    local_time.tm_mday = (int)g_gps_local_day;
    local_time.tm_hour = (int)g_gps_local_hour;
    local_time.tm_min = (int)g_gps_local_minute;
    local_time.tm_sec = (int)g_gps_local_second;

    /* Load the STM32 RTC calendar registers with local GPS time. */
    RTC_SetFromTM(&local_time);
    return 1U;
}

static void GPS_TryPendingRtcSync(void)
{
    /*
     * What: complete a deferred GPS-to-RTC synchronization request.
     * How: waits until local GPS time is ready, writes RTC, clears manual_time in storage.
     * Why: the web button may arrive before the GPS task has parsed a valid time sentence.
     */
    if (g_gps_time_sync_requested == 0U)
    {
        return;
    }

    if (g_gps_local_ready == 0U)
    {
        return;
    }

    /*
     * What: use the last committed GPS local time, not necessarily the latest raw NMEA line.
     * How: GPS_UpdateLocalTimeFromUtc() commits only parsed RMC/ZDA date-time sentences.
     * Why: non-time sentences arrive between RMC frames and must not make Sync Time fail.
     */
    if (GPS_SetRtcFromLocalTime() != 0U)
    {
        g_gps_time_sync_requested = 0U;
        g_gps_rtc_synced = 1U;
        g_gps_rtc_sync_count++;
        (void)saveTimeMode(false);

        /* Keep blue LED ON after RTC is loaded from GPS time. */
        HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
        GPS_PowerOffAfterSync();
    }
    else
    {
        g_gps_rtc_sync_error_count++;
    }
}

uint8_t GPS_Task_SyncTimeNow(void)
{
    /*
     * What: service the HTTP Sync Time button.
     * How: powers the GPS, raises a latch and waits for a fresh valid local GPS time.
     * Why: the button should test the GPS path instead of reusing stale parsed data.
     */
    GPS_BeginTimeSyncRequest();

    (void)GPS_UpdateLocalTimeFromUtc();
    GPS_TryPendingRtcSync();

    return g_gps_rtc_synced;
}

void GPS_Task_RequestTimeSync(void)
{
    /*
     * What: ask the GPS task to synchronize RTC as soon as it has valid local time.
     * How: sets the same latch used by the web Sync Time button.
     * Why: boot can request GPS time without blocking WiFi/server startup.
     */
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    GPS_BeginTimeSyncRequest();
}

void GPS_Task(void *argument)
{
    /*
     * What: parse GPS NMEA only while an RTC sync request is pending.
     * How: enables the GPS switch, reads UART in small slices and turns power off after sync.
     * Why: the receiver should consume power only while getting a fresh time sentence.
     */
    uint32_t bytes_this_slice;

    (void)argument;

    for (;;)
    {
        uint32_t poll_delay_ms;

        bytes_this_slice = 0U;
        g_gps_task_loop_count++;

        if ((g_gps_time_sync_requested != 0U) && (g_gps_power_enabled == 0U))
        {
            GPS_PowerOnForSync();
        }

        if (g_gps_power_enabled == 0U)
        {
            osDelay(GPS_IDLE_POLL_DELAY_MS);
            continue;
        }

        while ((bytes_this_slice < GPS_SYNC_BYTES_PER_SLICE) &&
               (GPS_ProcessUart() != 0U))
        {
            bytes_this_slice++;

            if (GPS_UpdateLocalTimeFromUtc() != 0U)
            {
                GPS_TryPendingRtcSync();
            }

        }

        if (GPS_UpdateLocalTimeFromUtc() != 0U)
        {
            GPS_TryPendingRtcSync();
        }

        GPS_TryPendingRtcSync();

        /*
         * What: poll GPS quickly only while a RTC sync is pending.
         * How: use 1 ms during sync, then relax to 250 ms once the RTC has been served.
         * Why: a permanent 1 ms GPS loop prevents FreeRTOS tickless sleep in idle states.
         */
        poll_delay_ms = (g_gps_time_sync_requested != 0U) ?
                        GPS_ACTIVE_POLL_DELAY_MS :
                        GPS_IDLE_POLL_DELAY_MS;
        osDelay(poll_delay_ms);
    }
}

void RTC_SetFromTM(struct tm *t)
{
    /*
     * What: write a standard tm date/time into STM32 RTC registers.
     * How: converts tm year/month offsets into HAL RTC binary fields and calls HAL setters.
     * Why: GPS and manual web time both need one shared RTC loading function.
     */
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    /* RTC_TimeTypeDef maps to the STM32 RTC time register fields. */
    sTime.Hours   = t->tm_hour;
    sTime.Minutes = t->tm_min;
    sTime.Seconds = t->tm_sec;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    /* RTC_DateTypeDef maps to the STM32 RTC date register fields. */
    sDate.WeekDay = RTC_WEEKDAY_MONDAY;
    sDate.Month   = t->tm_mon + 1;
    sDate.Date    = t->tm_mday;
    sDate.Year    = (t->tm_year + 1900) - 2000;

    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK){
    	Error_Handler();
    }
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK){
    	Error_Handler();
    }
}

/*
 * What: read the STM32 RTC registers into a standard tm structure.
 * How: gets time and date from HAL, then converts RTC month/year fields to tm offsets.
 * Why: higher-level time code can work with tm instead of RTC-specific structures.
 */
void RTC_GetToTM(struct tm *t)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    /*
     * What: clear fields that RTC does not provide directly.
     * How: zeroes the whole tm before filling calendar fields from HAL.
     * Why: later mktime() calls must not see random tm_wday/tm_yday/tm_isdst values.
     */
    memset(t, 0, sizeof(*t));

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    t->tm_hour = sTime.Hours;
    t->tm_min  = sTime.Minutes;
    t->tm_sec  = sTime.Seconds;

    t->tm_mday = sDate.Date;
    t->tm_mon  = sDate.Month - 1; //RTC 1-12 and tm 0-11
    t->tm_year = sDate.Year + 100; //Year since 1900
}

/*
 * What: load a manual local date/time into the RTC.
 * How: converts web form fields to tm, sets manual_time and calls RTC_SetFromTM().
 * Why: manual time must be used until GPS Sync Time explicitly restores GPS ownership.
 */
void setManualTime(int year, int month, int day, int hour, int min, int sec)
{
    struct tm t = {0};

    manual_time = true;

    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;

    RTC_SetFromTM(&t);

}


