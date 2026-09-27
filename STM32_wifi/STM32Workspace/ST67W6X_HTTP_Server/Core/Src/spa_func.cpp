#include "spa_func.h"
#include "spa.h"
#include <time.h>
#include "global_structs.h"
#include <string>
#include <gps.h>
#include <stdio.h>


extern AOIInputs g_AOIInputs;

static char normalizeCountryChar(char c) {
    if ((c >= 'A') && (c <= 'Z')) {
        return (char)(c + ('a' - 'A'));
    }

    return c;
}

static bool countryEqualsIgnoreCase(const char *country, const char *expected) {
    if ((country == NULL) || (expected == NULL)) {
        return false;
    }

    while ((*country != '\0') && (*expected != '\0')) {
        if (normalizeCountryChar(*country) != normalizeCountryChar(*expected)) {
            return false;
        }

        country++;
        expected++;
    }

    return (*country == '\0') && (*expected == '\0');
}

/**
 * Calculate timezone including DST
 * European rules for countries with DST
 */
static int getTimezoneForCountryText(const char *country, int year, int month, int day) {

    // --- Base offset--
    int baseOffset = 0;
    bool hasDST = false;

    // --- Countries definition ---
    if (countryEqualsIgnoreCase(country, "Spain")) {
        baseOffset = 1; hasDST = true;
    }
    else if (countryEqualsIgnoreCase(country, "Spain_Canary")) {
        baseOffset = 0; hasDST = true;
    }
    else if (countryEqualsIgnoreCase(country, "UK")) {
        baseOffset = 0; hasDST = true;
    }
    else if (countryEqualsIgnoreCase(country, "Poland")) {
        baseOffset = 1; hasDST = true;
    }
    else if (countryEqualsIgnoreCase(country, "Argentina")) {
        baseOffset = -3; hasDST = false;
    }
    else {
        baseOffset = 0; hasDST = false;
    }

   // --- DST logic ---
    if (hasDST) {
        int lastSundayMarch = 31;
        while (true) {
            struct tm t = {0};
            t.tm_year = year - 1900;
            t.tm_mon = 2; 
            t.tm_mday = lastSundayMarch;
            mktime(&t);
            if (t.tm_wday == 0) break; // tm_wday == 0 -> Sunday
            lastSundayMarch--;
        }

        int lastSundayOctober = 31;
        while (true) {
            struct tm t = {0};
            t.tm_year = year - 1900;
            t.tm_mon = 9;
            t.tm_mday = lastSundayOctober;
            mktime(&t);
            if (t.tm_wday == 0) break;
            lastSundayOctober--;
        }

        bool isDST = false;
        if ((month > 3 && month < 10) ||
            (month == 3 && day >= lastSundayMarch) ||
            (month == 10 && day < lastSundayOctober)) {
            isDST = true;
        }

        if (isDST) baseOffset += 1;
    }

    return baseOffset;
}

int getTimezoneForCountryName(const char *country, int year, int month, int day) {
    /* C API used by GPS code to reuse the SPA country/DST table. */
    return getTimezoneForCountryText(country, year, month, day);
}

int getTimezoneForCountry(const std::string& country, int year, int month, int day) {
    /* C++ API used by SPA code with std::string inputs. */
    return getTimezoneForCountryText(country.c_str(), year, month, day);
}

void SPA_f() {

    spa_data spa;

    spa.year   = g_SPAInputs.year;
    spa.month  = g_SPAInputs.month;
    spa.day    = g_SPAInputs.day;
    spa.hour   = g_SPAInputs.hour;
    spa.minute = g_SPAInputs.minute;
    spa.second = g_SPAInputs.second;

    spa.latitude  = g_SPAInputs.latitude;
    spa.longitude = g_SPAInputs.longitude;

    // ----------- Set timezone ----  
    spa.timezone = getTimezoneForCountry(
        g_country,
        spa.year,
        spa.month,
        spa.day
    );

    spa.delta_ut1     = 0;
    spa.delta_t       = 67;
    spa.elevation     = 670;
    spa.pressure      = 820;
    spa.temperature   = 20;
    spa.slope         = 30;
    spa.azm_rotation  = -10;
    spa.atmos_refract = 0.5667;
    spa.function      = SPA_ZA_RTS;

    printf(
        "[SPA INPUT] %04d-%02d-%02d %02d:%02d:%02.0f  tz=%f\n",
        spa.year, spa.month, spa.day,
        spa.hour, spa.minute, spa.second,
        spa.timezone
    );

    int result = spa_calculate(&spa);

    if (result != 0) {
        g_sunrise_epoch = 0;
        g_sunset_epoch  = 0;
        return;
    }

    g_AOIInputs.azimuth   = spa.azimuth;
    g_AOIInputs.elevation = spa.e;

    printf("Azimuth: %f", spa.azimuth);
    printf("Elevation: %f", spa.elevation);


   //Sunrise - sunstet for automode
    if (spa.sunrise > 0 && spa.sunset > 0) {

        struct tm t_now;
        RTC_GetToTM(&t_now);

       // midnight epoch
        struct tm t_midnight = t_now;
        t_midnight.tm_hour = 0;
        t_midnight.tm_min  = 0;
        t_midnight.tm_sec  = 0;
        time_t midnight = mktime(&t_midnight);

       // DECIMAL HOURS FROM SPA TO EPOCH SECONDS
        time_t sunrise_epoch = midnight + (time_t)(spa.sunrise * 3600.0);
        time_t sunset_epoch  = midnight + (time_t)(spa.sunset  * 3600.0);

       // Store today's sunrise and sunset. Auto mode compares them with the current local RTC.
        g_sunrise_epoch = sunrise_epoch;
        g_sunset_epoch  = sunset_epoch;
    }
    else {
        g_sunrise_epoch = 0;
        g_sunset_epoch  = 0;
    }
}
