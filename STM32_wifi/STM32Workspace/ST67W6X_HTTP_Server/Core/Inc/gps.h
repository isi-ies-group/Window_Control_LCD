#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include <time.h>

#define GPS_NMEA_LINE_MAX 256U

#ifdef __cplusplus
extern "C" {
#endif

extern char g_gps_last_line[GPS_NMEA_LINE_MAX];
extern char g_gps_last_rmc[GPS_NMEA_LINE_MAX];
extern volatile uint32_t g_gps_line_count;
extern volatile uint32_t g_gps_rmc_count;
extern volatile uint32_t g_gps_overflow_count;
extern volatile uint8_t g_gps_line_ready;
extern volatile uint8_t g_gps_utc_ready;
extern volatile uint8_t g_gps_fix_valid;
extern volatile uint16_t g_gps_utc_year;
extern volatile uint8_t g_gps_utc_month;
extern volatile uint8_t g_gps_utc_day;
extern volatile uint8_t g_gps_utc_hour;
extern volatile uint8_t g_gps_utc_minute;
extern volatile uint8_t g_gps_utc_second;

extern volatile int8_t g_gps_timezone_offset_hours;
extern volatile uint8_t g_gps_local_ready;
extern volatile uint32_t g_gps_local_update_count;
extern volatile uint16_t g_gps_local_year;
extern volatile uint8_t g_gps_local_month;
extern volatile uint8_t g_gps_local_day;
extern volatile uint8_t g_gps_local_hour;
extern volatile uint8_t g_gps_local_minute;
extern volatile uint8_t g_gps_local_second;

extern volatile uint8_t g_gps_time_sync_requested;
extern volatile uint8_t g_gps_rtc_synced;
extern volatile uint8_t g_gps_power_enabled;
extern volatile uint32_t g_gps_task_loop_count;
extern volatile uint32_t g_gps_rtc_sync_count;
extern volatile uint32_t g_gps_rtc_sync_error_count;

void RTC_SetFromTM(struct tm *t);
void RTC_GetToTM(struct tm *t);

void setManualTime(int year, int month, int day,
                   int hour, int min, int sec);

uint8_t GPS_ProcessUart(void);
uint8_t GPS_UpdateLocalTimeFromUtc(void);
uint8_t GPS_Task_SyncTimeNow(void);
void GPS_Task_RequestTimeSync(void);
void GPS_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */


