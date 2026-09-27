#include "storage.h"

#include <stddef.h>
#include <string.h>

#include "gps.h"
#include "global_structs.h"
#include "main.h"
#include "movement_parameters.h"
#include "stm32u5xx_hal_flash.h"
#include "stm32u5xx_hal_flash_ex.h"

#define STORAGE_MAGIC              0x57435631UL /* "WCV1": Window Control Variables v1. */
#define STORAGE_VERSION            4UL
#define STORAGE_LEGACY_VERSION     1UL
#define STORAGE_V2_VERSION         2UL
#define STORAGE_V3_VERSION         3UL
#define STORAGE_FLASH_ADDRESS      (FLASH_BASE + FLASH_SIZE - FLASH_PAGE_SIZE)
#define STORAGE_FLASH_BANK         FLASH_BANK_2
#define STORAGE_FLASH_PAGE         (FLASH_PAGE_NB - 1U)
#define STORAGE_COUNTRY_MAX        32U
#define STORAGE_PROGRAM_UNIT       16U

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t crc;

  double latitude;
  double longitude;
  double pan;
  double tilt;

  uint32_t tilt_correction;
  uint32_t auto_mode_on;
  uint32_t manual_time_on;
  uint32_t fsm_state;

  float x_pos;
  float z_pos;

  int32_t manual_year;
  int32_t manual_month;
  int32_t manual_day;
  int32_t manual_hour;
  int32_t manual_minute;
  int32_t manual_second;

  char country[STORAGE_COUNTRY_MAX];
} StorageRecordV1_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t crc;

  double latitude;
  double longitude;
  double pan;
  double tilt;

  uint32_t tilt_correction;
  uint32_t auto_mode_on;
  uint32_t manual_time_on;
  uint32_t fsm_state;

  float x_pos;
  float z_pos;
  float movement_hysteresis_gain;
  float movement_hysteresis_offset_mm;

  int32_t manual_year;
  int32_t manual_month;
  int32_t manual_day;
  int32_t manual_hour;
  int32_t manual_minute;
  int32_t manual_second;

  char country[STORAGE_COUNTRY_MAX];
} StorageRecordV2_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t crc;

  double latitude;
  double longitude;
  double pan;
  double tilt;

  uint32_t tilt_correction;
  uint32_t auto_mode_on;
  uint32_t manual_time_on;
  uint32_t fsm_state;

  float x_pos;
  float z_pos;
  float movement_hysteresis_gain;
  float movement_hysteresis_offset_mm;
  float vertical_movement_hysteresis_gain;
  float vertical_movement_hysteresis_offset_mm;

  int32_t manual_year;
  int32_t manual_month;
  int32_t manual_day;
  int32_t manual_hour;
  int32_t manual_minute;
  int32_t manual_second;

  char country[STORAGE_COUNTRY_MAX];
} StorageRecordV3_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t crc;

  double latitude;
  double longitude;
  double pan;
  double tilt;

  uint32_t tilt_correction;
  uint32_t auto_mode_on;
  uint32_t manual_time_on;
  uint32_t fsm_state;

  float x_pos;
  float z_pos;
  float movement_hysteresis_gain;
  float movement_hysteresis_offset_mm;
  float vertical_movement_hysteresis_gain;
  float vertical_movement_hysteresis_offset_mm;
  float movement_max_x_mm;
  float movement_max_z_mm;
  uint32_t movement_speed_us;
  uint32_t movement_home_speed_us;

  int32_t manual_year;
  int32_t manual_month;
  int32_t manual_day;
  int32_t manual_hour;
  int32_t manual_minute;
  int32_t manual_second;

  char country[STORAGE_COUNTRY_MAX];
} StorageRecord_t;

static StorageRecord_t storage_cache;
static bool storage_cache_valid = false;
static bool storage_record_was_valid = false;
static volatile bool storage_save_in_progress = false;

/*
 * What: calculate a checksum for the persistent record.
 * How: walks every byte except the crc field itself with a small FNV-style hash.
 * Why: lets boot ignore erased/corrupted flash instead of loading unsafe globals.
 */
static uint32_t Storage_CalculateCrcBytes(const void *record,
                                          size_t record_size,
                                          size_t crc_offset,
                                          size_t crc_size)
{
  uint32_t crc = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)record;

  for (size_t i = 0U; i < record_size; i++)
  {
    if ((i >= crc_offset) && (i < (crc_offset + crc_size)))
    {
      continue;
    }

    crc ^= bytes[i];
    crc *= 16777619UL;
  }

  return crc;
}

static uint32_t Storage_CalculateCrcV1(const StorageRecordV1_t *record)
{
  return Storage_CalculateCrcBytes(record,
                                   sizeof(StorageRecordV1_t),
                                   offsetof(StorageRecordV1_t, crc),
                                   sizeof(record->crc));
}

static uint32_t Storage_CalculateCrcV2(const StorageRecordV2_t *record)
{
  return Storage_CalculateCrcBytes(record,
                                   sizeof(StorageRecordV2_t),
                                   offsetof(StorageRecordV2_t, crc),
                                   sizeof(record->crc));
}

static uint32_t Storage_CalculateCrcV3(const StorageRecordV3_t *record)
{
  return Storage_CalculateCrcBytes(record,
                                   sizeof(StorageRecordV3_t),
                                   offsetof(StorageRecordV3_t, crc),
                                   sizeof(record->crc));
}

static uint32_t Storage_CalculateCrc(const StorageRecord_t *record)
{
  return Storage_CalculateCrcBytes(record,
                                   sizeof(StorageRecord_t),
                                   offsetof(StorageRecord_t, crc),
                                   sizeof(record->crc));
}

/*
 * What: create the first-boot values for configuration, state, position and time mode.
 * How: fills one complete record, then calculates its crc as if it came from flash.
 * Why: keeps STM32 startup behavior close to ESP32 loadData() when no NVS data exists.
 */
static void Storage_SetDefaults(StorageRecord_t *record)
{
  (void)memset(record, 0, sizeof(*record));

  record->magic = STORAGE_MAGIC;
  record->version = STORAGE_VERSION;
  record->size = sizeof(StorageRecord_t);
  record->latitude = 40.4168;
  record->longitude = -3.7038;
  record->pan = 0.0;
  record->tilt = 0.0;
  record->tilt_correction = 0U;
  record->auto_mode_on = 0U;
  record->manual_time_on = 0U;
  record->fsm_state = (uint32_t)STDBY;
  record->x_pos = 0.0f;
  record->z_pos = 0.0f;
  record->movement_hysteresis_gain = MOVEMENT_HYSTERESIS_DEFAULT_GAIN;
  record->movement_hysteresis_offset_mm = MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM;
  record->vertical_movement_hysteresis_gain = VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_GAIN;
  record->vertical_movement_hysteresis_offset_mm = VERTICAL_MOVEMENT_HYSTERESIS_DEFAULT_OFFSET_MM;
  record->movement_max_x_mm = MOVEMENT_DEFAULT_MAX_X_MM;
  record->movement_max_z_mm = MOVEMENT_DEFAULT_MAX_Z_MM;
  record->movement_speed_us = MOVEMENT_DEFAULT_STEP_DELAY_US;
  record->movement_home_speed_us = MOVEMENT_DEFAULT_HOME_STEP_DELAY_US;
  record->manual_year = 2026;
  record->manual_month = 1;
  record->manual_day = 1;
  record->manual_hour = 0;
  record->manual_minute = 0;
  record->manual_second = 0;
  (void)strncpy(record->country, "Spain", sizeof(record->country) - 1U);
  record->crc = Storage_CalculateCrc(record);
}

static bool Storage_IsMovementConfigValid(float gain, float offset_mm)
{
  return MovementParameters_IsGainValid(gain) &&
         MovementParameters_IsOffsetMmValid(offset_mm);
}

static bool Storage_IsMovementRangeConfigValid(float max_x_mm, float max_z_mm)
{
  return MovementParameters_IsRangeMmValid(max_x_mm) &&
         MovementParameters_IsRangeMmValid(max_z_mm);
}

static bool Storage_IsMovementSpeedConfigValid(uint32_t movement_speed_us,
                                               uint32_t movement_home_speed_us)
{
  return MovementParameters_IsSpeedUsValid(movement_speed_us) &&
         MovementParameters_IsSpeedUsValid(movement_home_speed_us);
}

static bool Storage_IsLegacyRecordValid(const StorageRecordV1_t *record)
{
  if ((record->magic != STORAGE_MAGIC) ||
      (record->version != STORAGE_LEGACY_VERSION) ||
      (record->size != sizeof(StorageRecordV1_t)) ||
      (record->fsm_state > (uint32_t)EPH_INPUT) ||
      (record->manual_year < 2000) ||
      (record->manual_year > 3000) ||
      (record->manual_month < 1) ||
      (record->manual_month > 12) ||
      (record->manual_day < 1) ||
      (record->manual_day > 31) ||
      (record->manual_hour < 0) ||
      (record->manual_hour > 23) ||
      (record->manual_minute < 0) ||
      (record->manual_minute > 59) ||
      (record->manual_second < 0) ||
      (record->manual_second > 59) ||
      (record->country[0] == '\0'))
  {
    return false;
  }

  return (record->crc == Storage_CalculateCrcV1(record));
}

static bool Storage_IsV2RecordValid(const StorageRecordV2_t *record)
{
  if ((record->magic != STORAGE_MAGIC) ||
      (record->version != STORAGE_V2_VERSION) ||
      (record->size != sizeof(StorageRecordV2_t)) ||
      (record->fsm_state > (uint32_t)EPH_INPUT) ||
      (record->manual_year < 2000) ||
      (record->manual_year > 3000) ||
      (record->manual_month < 1) ||
      (record->manual_month > 12) ||
      (record->manual_day < 1) ||
      (record->manual_day > 31) ||
      (record->manual_hour < 0) ||
      (record->manual_hour > 23) ||
      (record->manual_minute < 0) ||
      (record->manual_minute > 59) ||
      (record->manual_second < 0) ||
      (record->manual_second > 59) ||
      (!Storage_IsMovementConfigValid(record->movement_hysteresis_gain,
                                      record->movement_hysteresis_offset_mm)) ||
      (record->country[0] == '\0'))
  {
    return false;
  }

  return (record->crc == Storage_CalculateCrcV2(record));
}

static bool Storage_IsV3RecordValid(const StorageRecordV3_t *record)
{
  if ((record->magic != STORAGE_MAGIC) ||
      (record->version != STORAGE_V3_VERSION) ||
      (record->size != sizeof(StorageRecordV3_t)) ||
      (record->fsm_state > (uint32_t)EPH_INPUT) ||
      (record->manual_year < 2000) ||
      (record->manual_year > 3000) ||
      (record->manual_month < 1) ||
      (record->manual_month > 12) ||
      (record->manual_day < 1) ||
      (record->manual_day > 31) ||
      (record->manual_hour < 0) ||
      (record->manual_hour > 23) ||
      (record->manual_minute < 0) ||
      (record->manual_minute > 59) ||
      (record->manual_second < 0) ||
      (record->manual_second > 59) ||
      (!Storage_IsMovementConfigValid(record->movement_hysteresis_gain,
                                      record->movement_hysteresis_offset_mm)) ||
      (!Storage_IsMovementConfigValid(record->vertical_movement_hysteresis_gain,
                                      record->vertical_movement_hysteresis_offset_mm)) ||
      (record->country[0] == '\0'))
  {
    return false;
  }

  return (record->crc == Storage_CalculateCrcV3(record));
}

static void Storage_MigrateLegacyRecord(const StorageRecordV1_t *legacy,
                                        StorageRecord_t *record)
{
  Storage_SetDefaults(record);

  record->latitude = legacy->latitude;
  record->longitude = legacy->longitude;
  record->pan = legacy->pan;
  record->tilt = legacy->tilt;
  record->tilt_correction = legacy->tilt_correction;
  record->auto_mode_on = legacy->auto_mode_on;
  record->manual_time_on = legacy->manual_time_on;
  record->fsm_state = legacy->fsm_state;
  record->x_pos = legacy->x_pos;
  record->z_pos = legacy->z_pos;
  record->manual_year = legacy->manual_year;
  record->manual_month = legacy->manual_month;
  record->manual_day = legacy->manual_day;
  record->manual_hour = legacy->manual_hour;
  record->manual_minute = legacy->manual_minute;
  record->manual_second = legacy->manual_second;
  (void)strncpy(record->country, legacy->country, sizeof(record->country) - 1U);
  record->country[sizeof(record->country) - 1U] = '\0';
  record->crc = Storage_CalculateCrc(record);
}

static void Storage_MigrateV2Record(const StorageRecordV2_t *v2,
                                    StorageRecord_t *record)
{
  Storage_SetDefaults(record);

  record->latitude = v2->latitude;
  record->longitude = v2->longitude;
  record->pan = v2->pan;
  record->tilt = v2->tilt;
  record->tilt_correction = v2->tilt_correction;
  record->auto_mode_on = v2->auto_mode_on;
  record->manual_time_on = v2->manual_time_on;
  record->fsm_state = v2->fsm_state;
  record->x_pos = v2->x_pos;
  record->z_pos = v2->z_pos;
  record->movement_hysteresis_gain = v2->movement_hysteresis_gain;
  record->movement_hysteresis_offset_mm = v2->movement_hysteresis_offset_mm;
  record->manual_year = v2->manual_year;
  record->manual_month = v2->manual_month;
  record->manual_day = v2->manual_day;
  record->manual_hour = v2->manual_hour;
  record->manual_minute = v2->manual_minute;
  record->manual_second = v2->manual_second;
  (void)strncpy(record->country, v2->country, sizeof(record->country) - 1U);
  record->country[sizeof(record->country) - 1U] = '\0';
  record->crc = Storage_CalculateCrc(record);
}

static void Storage_MigrateV3Record(const StorageRecordV3_t *v3,
                                    StorageRecord_t *record)
{
  Storage_SetDefaults(record);

  record->latitude = v3->latitude;
  record->longitude = v3->longitude;
  record->pan = v3->pan;
  record->tilt = v3->tilt;
  record->tilt_correction = v3->tilt_correction;
  record->auto_mode_on = v3->auto_mode_on;
  record->manual_time_on = v3->manual_time_on;
  record->fsm_state = v3->fsm_state;
  record->x_pos = v3->x_pos;
  record->z_pos = v3->z_pos;
  record->movement_hysteresis_gain = v3->movement_hysteresis_gain;
  record->movement_hysteresis_offset_mm = v3->movement_hysteresis_offset_mm;
  record->vertical_movement_hysteresis_gain = v3->vertical_movement_hysteresis_gain;
  record->vertical_movement_hysteresis_offset_mm = v3->vertical_movement_hysteresis_offset_mm;
  record->manual_year = v3->manual_year;
  record->manual_month = v3->manual_month;
  record->manual_day = v3->manual_day;
  record->manual_hour = v3->manual_hour;
  record->manual_minute = v3->manual_minute;
  record->manual_second = v3->manual_second;
  (void)strncpy(record->country, v3->country, sizeof(record->country) - 1U);
  record->country[sizeof(record->country) - 1U] = '\0';
  record->crc = Storage_CalculateCrc(record);
}

/*
 * What: decide whether the flash page contains a usable Window Control record.
 * How: checks identity fields, basic ranges, country string and the stored crc.
 * Why: protects the application from random flash contents after erase/program loss.
 */
static bool Storage_IsRecordValid(const StorageRecord_t *record)
{
  if ((record->magic != STORAGE_MAGIC) ||
      (record->version != STORAGE_VERSION) ||
      (record->size != sizeof(StorageRecord_t)) ||
      (record->fsm_state > (uint32_t)EPH_INPUT) ||
      (record->manual_year < 2000) ||
      (record->manual_year > 3000) ||
      (record->manual_month < 1) ||
      (record->manual_month > 12) ||
      (record->manual_day < 1) ||
      (record->manual_day > 31) ||
      (record->manual_hour < 0) ||
      (record->manual_hour > 23) ||
      (record->manual_minute < 0) ||
      (record->manual_minute > 59) ||
      (record->manual_second < 0) ||
      (record->manual_second > 59) ||
      (!Storage_IsMovementConfigValid(record->movement_hysteresis_gain,
                                      record->movement_hysteresis_offset_mm)) ||
      (!Storage_IsMovementConfigValid(record->vertical_movement_hysteresis_gain,
                                      record->vertical_movement_hysteresis_offset_mm)) ||
      (!Storage_IsMovementRangeConfigValid(record->movement_max_x_mm,
                                           record->movement_max_z_mm)) ||
      (!Storage_IsMovementSpeedConfigValid(record->movement_speed_us,
                                           record->movement_home_speed_us)) ||
      (record->country[0] == '\0'))
  {
    return false;
  }

  return (record->crc == Storage_CalculateCrc(record));
}

/*
 * What: copy the persistent page from flash into RAM.
 * How: treats the last flash page as a StorageRecord_t and validates the result.
 * Why: flash cannot be edited field-by-field, so later saves work from a RAM cache.
 */
static bool Storage_ReadRecord(StorageRecord_t *record)
{
  const StorageRecord_t *flash_record = (const StorageRecord_t *)STORAGE_FLASH_ADDRESS;
  const StorageRecordV3_t *flash_v3_record = (const StorageRecordV3_t *)STORAGE_FLASH_ADDRESS;
  const StorageRecordV2_t *flash_v2_record = (const StorageRecordV2_t *)STORAGE_FLASH_ADDRESS;
  const StorageRecordV1_t *flash_legacy_record = (const StorageRecordV1_t *)STORAGE_FLASH_ADDRESS;
  StorageRecordV3_t v3_record;
  StorageRecordV2_t v2_record;
  StorageRecordV1_t legacy_record;

  (void)memcpy(record, flash_record, sizeof(*record));
  if (Storage_IsRecordValid(record))
  {
    return true;
  }

  (void)memcpy(&v3_record, flash_v3_record, sizeof(v3_record));
  if (Storage_IsV3RecordValid(&v3_record))
  {
    Storage_MigrateV3Record(&v3_record, record);
    return true;
  }

  (void)memcpy(&v2_record, flash_v2_record, sizeof(v2_record));
  if (Storage_IsV2RecordValid(&v2_record))
  {
    Storage_MigrateV2Record(&v2_record, record);
    return true;
  }

  (void)memcpy(&legacy_record, flash_legacy_record, sizeof(legacy_record));
  if (Storage_IsLegacyRecordValid(&legacy_record))
  {
    Storage_MigrateLegacyRecord(&legacy_record, record);
    return true;
  }

  return false;
}

/*
 * What: load the saved local timestamp into the STM32 RTC.
 * How: converts the stored calendar fields into struct tm and calls RTC_SetFromTM().
 * Why: after reset/power loss the clock must resume from the latest persisted local time.
 */
static void Storage_ApplyRecordToRtc(const StorageRecord_t *record)
{
  struct tm stored_time = {0};

  stored_time.tm_year = record->manual_year - 1900;
  stored_time.tm_mon = record->manual_month - 1;
  stored_time.tm_mday = record->manual_day;
  stored_time.tm_hour = record->manual_hour;
  stored_time.tm_min = record->manual_minute;
  stored_time.tm_sec = record->manual_second;

  RTC_SetFromTM(&stored_time);
}

/*
 * What: publish stored values into the globals used by config, FSM, GPS and movement.
 * How: copies each persisted field to its runtime owner and restores RTC from saved local time.
 * Why: after boot, the rest of the firmware can keep using the same globals as before.
 */
static void Storage_ApplyRecordToGlobals(const StorageRecord_t *record)
{
  g_SPAInputs.latitude = record->latitude;
  g_SPAInputs.longitude = record->longitude;
  g_AOIInputs.pan = record->pan;
  g_AOIInputs.tilt = record->tilt;
  g_AOIInputs.tilt_correction = (record->tilt_correction != 0U);

  (void)strncpy(g_country, record->country, sizeof(g_country) - 1U);
  g_country[sizeof(g_country) - 1U] = '\0';

  auto_on = (record->auto_mode_on != 0U);
  manual_time = (record->manual_time_on != 0U);
  thisSt = (States)record->fsm_state;
  nextSt = thisSt;

  g_x_val = record->x_pos;
  g_z_val = record->z_pos;
  g_x_target = g_x_val;
  g_z_target = g_z_val;
  g_movement_hysteresis_gain = record->movement_hysteresis_gain;
  g_movement_hysteresis_offset_mm = record->movement_hysteresis_offset_mm;
  g_vertical_movement_hysteresis_gain = record->vertical_movement_hysteresis_gain;
  g_vertical_movement_hysteresis_offset_mm = record->vertical_movement_hysteresis_offset_mm;
  g_movement_max_x_mm = MovementParameters_ValidatedRangeMm(record->movement_max_x_mm,
                                                            MOVEMENT_DEFAULT_MAX_X_MM);
  g_movement_max_z_mm = MovementParameters_ValidatedRangeMm(record->movement_max_z_mm,
                                                            MOVEMENT_DEFAULT_MAX_Z_MM);
  g_movement_speed_us = MovementParameters_ValidatedSpeedUs(record->movement_speed_us,
                                                            MOVEMENT_DEFAULT_STEP_DELAY_US);
  g_movement_home_speed_us = MovementParameters_ValidatedSpeedUs(record->movement_home_speed_us,
                                                                 MOVEMENT_DEFAULT_HOME_STEP_DELAY_US);

  Storage_ApplyRecordToRtc(record);
}

/*
 * What: copy the current RTC calendar into the persistent record cache.
 * How: reads RTC through RTC_GetToTM() and stores year/month/day/hour/min/sec fields.
 * Why: periodic saves must keep the reboot fallback clock close to real local time.
 */
static void Storage_UpdateCacheFromRtc(void)
{
  struct tm rtc_time = {0};

  RTC_GetToTM(&rtc_time);

  storage_cache.manual_year = rtc_time.tm_year + 1900;
  storage_cache.manual_month = rtc_time.tm_mon + 1;
  storage_cache.manual_day = rtc_time.tm_mday;
  storage_cache.manual_hour = rtc_time.tm_hour;
  storage_cache.manual_minute = rtc_time.tm_min;
  storage_cache.manual_second = rtc_time.tm_sec;
}

/*
 * What: refresh the flash cache from the current runtime globals.
 * How: reads configuration, state, position and time-mode flags, then recalculates crc.
 * Why: every save writes one full record, so unchanged fields must be preserved too.
 */
static void Storage_UpdateCacheFromGlobals(void)
{
  States persistent_state = fsmGetPersistentState();

  storage_cache.magic = STORAGE_MAGIC;
  storage_cache.version = STORAGE_VERSION;
  storage_cache.size = sizeof(StorageRecord_t);

  storage_cache.latitude = g_SPAInputs.latitude;
  storage_cache.longitude = g_SPAInputs.longitude;
  storage_cache.pan = g_AOIInputs.pan;
  storage_cache.tilt = g_AOIInputs.tilt;
  storage_cache.tilt_correction = g_AOIInputs.tilt_correction ? 1U : 0U;

  storage_cache.auto_mode_on = (persistent_state == AUTO_MODE) ? 1U : 0U;
  storage_cache.manual_time_on = manual_time ? 1U : 0U;
  storage_cache.fsm_state = (uint32_t)persistent_state;
  storage_cache.x_pos = g_x_val;
  storage_cache.z_pos = g_z_val;
  storage_cache.movement_hysteresis_gain = g_movement_hysteresis_gain;
  storage_cache.movement_hysteresis_offset_mm = g_movement_hysteresis_offset_mm;
  storage_cache.vertical_movement_hysteresis_gain = g_vertical_movement_hysteresis_gain;
  storage_cache.vertical_movement_hysteresis_offset_mm = g_vertical_movement_hysteresis_offset_mm;
  storage_cache.movement_max_x_mm = MovementParameters_ValidatedRangeMm(g_movement_max_x_mm,
                                                                        MOVEMENT_DEFAULT_MAX_X_MM);
  storage_cache.movement_max_z_mm = MovementParameters_ValidatedRangeMm(g_movement_max_z_mm,
                                                                        MOVEMENT_DEFAULT_MAX_Z_MM);
  storage_cache.movement_speed_us = MovementParameters_ValidatedSpeedUs(g_movement_speed_us,
                                                                        MOVEMENT_DEFAULT_STEP_DELAY_US);
  storage_cache.movement_home_speed_us = MovementParameters_ValidatedSpeedUs(g_movement_home_speed_us,
                                                                             MOVEMENT_DEFAULT_HOME_STEP_DELAY_US);

  (void)strncpy(storage_cache.country, g_country, sizeof(storage_cache.country) - 1U);
  storage_cache.country[sizeof(storage_cache.country) - 1U] = '\0';

  storage_cache.crc = Storage_CalculateCrc(&storage_cache);
}

/*
 * What: erase and rewrite the persistent storage page.
 * How: unlocks flash, erases the selected page, then programs 16-byte STM32U5 quadwords.
 * Why: this emulates the ESP32 Preferences/NVS save step with the STM32 internal flash.
 */
static bool Storage_WriteCacheToFlash(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;
  uint32_t address = STORAGE_FLASH_ADDRESS;
  const uint8_t *source = (const uint8_t *)&storage_cache;
  size_t remaining = sizeof(storage_cache);
  HAL_StatusTypeDef status;

  if (storage_save_in_progress)
  {
    return false;
  }

  storage_save_in_progress = true;

  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = STORAGE_FLASH_BANK;
  erase.Page = STORAGE_FLASH_PAGE;
  erase.NbPages = 1U;

  status = HAL_FLASH_Unlock();
  if (status == HAL_OK)
  {
    status = HAL_FLASHEx_Erase(&erase, &page_error);
  }

  while ((status == HAL_OK) && (remaining > 0U))
  {
    uint32_t quadword[4] = {0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL};
    size_t chunk = (remaining >= STORAGE_PROGRAM_UNIT) ? STORAGE_PROGRAM_UNIT : remaining;

    (void)memcpy(quadword, source, chunk);
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, address, (uint32_t)quadword);
    address += STORAGE_PROGRAM_UNIT;
    source += chunk;
    remaining -= chunk;
  }

  (void)HAL_FLASH_Lock();
  storage_save_in_progress = false;

  return (status == HAL_OK);
}

/*
 * What: persist the latest runtime globals.
 * How: initializes the cache if needed, refreshes it from globals and writes flash.
 * Why: gives saveData/saveState/savePos one common path so the record format stays coherent.
 */
static bool Storage_SaveCurrentGlobals(void)
{
  if (!storage_cache_valid)
  {
    Storage_SetDefaults(&storage_cache);
    storage_cache_valid = true;
  }

  Storage_UpdateCacheFromGlobals();

  return Storage_WriteCacheToFlash();
}

/*
 * What: load configuration, state, position and time mode during application startup.
 * How: reads flash, falls back to defaults if invalid, and applies the record to globals.
 * Why: startup should recover the same operating point after reset or power loss.
 */
bool Storage_LoadAll(void)
{
  storage_record_was_valid = Storage_ReadRecord(&storage_cache);

  if (!storage_record_was_valid)
  {
    Storage_SetDefaults(&storage_cache);
  }

  storage_cache_valid = true;
  Storage_ApplyRecordToGlobals(&storage_cache);

  return storage_record_was_valid;
}

/*
 * What: save configuration values.
 * How: copies the current config globals into the cached record and writes flash.
 * Why: mirrors ESP32 saveData() so configuration survives reset and power loss.
 */
bool saveData(void)
{
  return Storage_SaveCurrentGlobals();
}

/*
 * What: save the current FSM state.
 * How: records thisSt and the synchronized auto_on flag in the flash record.
 * Why: later low-power/reboot flows can resume from the last logical mode.
 */
bool saveState(void)
{
  return Storage_SaveCurrentGlobals();
}

/*
 * What: save the latest software X/Z position.
 * How: stores g_x_val and g_z_val after movement or homing completes.
 * Why: after power loss the firmware can restart with the last known mechanism position.
 */
bool savePos(void)
{
  return Storage_SaveCurrentGlobals();
}

/*
 * What: save movement calibration values.
 * How: stores the current gain, offset, range and speed globals inside the same flash-backed record.
 * Why: calibration changes from WiFi or any future non-WiFi input must survive reset and power loss.
 */
bool saveMovementConfig(void)
{
  return Storage_SaveCurrentGlobals();
}

/*
 * What: save a manually entered local date/time.
 * How: stores the timestamp fields and marks manual_time as active before writing flash.
 * Why: if RTC stops after power loss, boot can reuse this manual time until GPS sync wins.
 */
bool saveManualTime(int year, int month, int day, int hour, int minute, int second)
{
  if (!storage_cache_valid)
  {
    Storage_SetDefaults(&storage_cache);
    storage_cache_valid = true;
  }

  storage_cache.manual_year = year;
  storage_cache.manual_month = month;
  storage_cache.manual_day = day;
  storage_cache.manual_hour = hour;
  storage_cache.manual_minute = minute;
  storage_cache.manual_second = second;
  manual_time = true;

  return Storage_SaveCurrentGlobals();
}

/*
 * What: save whether RTC time currently comes from manual input or GPS.
 * How: updates only the manual_time flag and preserves the last manual timestamp fields.
 * Why: pressing Sync Time must switch the source back to GPS without losing fallback data.
 */
bool saveTimeMode(bool is_manual_time)
{
  if (!storage_cache_valid)
  {
    Storage_SetDefaults(&storage_cache);
    storage_cache_valid = true;
  }

  manual_time = is_manual_time;
  Storage_UpdateCacheFromRtc();
  return Storage_SaveCurrentGlobals();
}

/*
 * What: save the current RTC local time without changing config/state/position.
 * How: reads RTC into the cached timestamp fields and writes the complete flash record.
 * Why: after reset the firmware should restart from the latest known local clock value.
 */
bool saveRtcTime(void)
{
  if (!storage_cache_valid)
  {
    Storage_SetDefaults(&storage_cache);
    storage_cache_valid = true;
  }

  Storage_UpdateCacheFromRtc();
  return Storage_SaveCurrentGlobals();
}

/*
 * What: reload all stored values through the ESP32-style loadData() entry point.
 * How: delegates to Storage_LoadAll() because STM32 stores one coherent record.
 * Why: keeps old call sites familiar while avoiding split flash records.
 */
bool loadData(void)
{
  return Storage_LoadAll();
}

/*
 * What: reload FSM state through the ESP32-style loadState() entry point.
 * How: delegates to Storage_LoadAll() and reapplies the complete record.
 * Why: state, config and position are intentionally restored as one consistent snapshot.
 */
bool loadState(void)
{
  return Storage_LoadAll();
}

/*
 * What: reload X/Z position through the ESP32-style loadPos() entry point.
 * How: delegates to Storage_LoadAll() and reapplies the complete record.
 * Why: avoids mixing a new position with stale configuration/state fields.
 */
bool loadPos(void)
{
  return Storage_LoadAll();
}

/*
 * What: reload manual time mode and its stored timestamp.
 * How: delegates to Storage_LoadAll(), which calls setManualTime() when needed.
 * Why: RTC recovery is part of the same startup snapshot as the rest of the app.
 */
bool loadManualTime(void)
{
  return Storage_LoadAll();
}

/*
 * What: expose whether boot used a valid flash record or defaults.
 * How: returns the validation result captured during Storage_LoadAll().
 * Why: status/debug code can tell if persistence is actually being used.
 */
bool Storage_HasValidRecord(void)
{
  return storage_record_was_valid;
}
