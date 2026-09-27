#include <cmath>
#include <time.h>
#include "autoMode.h"
#include "aoicalc_func.h"
#include "interpolation_func.h"
#include "spa_func.h"
#include "matrices.h"
#include "gps.h"
#include "movement_parameters.h"
#include "movement_task.h"


#define AUTOMODE_MIN_MOVE_MM 0.5f
#define AUTOMODE_SAFE_X_MM 10.0f
#define AUTOMODE_SAFE_Z_MM 10.0f
#define AUTOMODE_MIN_VALID_X_MM 0.0f
#define AUTOMODE_MIN_VALID_Z_MM 0.0f


static bool autoModeTargetNeedsMove(float current_x, float current_z,
                                    float target_x, float target_z)
{
    /*
     * What: decide if an automatic target is large enough to move the mechanics.
     * How: accepts the target only when X or Z changes at least 0.5 mm from the last accepted position.
     * Why: sub-millimetre automatic corrections create accumulated step error without useful panel motion.
     */
    if ((!std::isfinite(target_x)) || (!std::isfinite(target_z)))
    {
        return false;
    }

    return (std::fabs(target_x - current_x) >= AUTOMODE_MIN_MOVE_MM) ||
           (std::fabs(target_z - current_z) >= AUTOMODE_MIN_MOVE_MM);
}

static bool autoModeTargetIsValid(float target_x, float target_z)
{
    /*
     * What: validate the automatic target before sending it to the motors.
     * How: accepts only finite values inside the expected solar tracking window.
     * Why: invalid interpolation/solar values must park the panel instead of driving into a limit.
     */
    const float max_x = MovementParameters_ValidatedRangeMm(g_movement_max_x_mm,
                                                            MOVEMENT_DEFAULT_MAX_X_MM);
    const float max_z = MovementParameters_ValidatedRangeMm(g_movement_max_z_mm,
                                                            MOVEMENT_DEFAULT_MAX_Z_MM);

    return std::isfinite(target_x) &&
           std::isfinite(target_z) &&
           (target_x >= AUTOMODE_MIN_VALID_X_MM) &&
           (target_x <= max_x) &&
           (target_z >= AUTOMODE_MIN_VALID_Z_MM) &&
           (target_z <= max_z);
}

static bool autoModeIsNight(time_t now)
{
    /*
     * What: decide whether the current local time is outside today's sunlight window.
     * How: SPA_f() updates g_sunrise_epoch/g_sunset_epoch for the current date.
     * Why: at night the panel should stay in the safe parking position.
     */
    if ((now <= 0) || (g_sunrise_epoch <= 0) || (g_sunset_epoch <= 0))
    {
        return false;
    }

    return (now < g_sunrise_epoch) || (now > g_sunset_epoch);
}

static void autoModeSetSafeParkTarget(void)
{
    /*
     * What: set the fallback target used for night or invalid automatic calculations.
     * How: writes the same global target variables consumed by requestMove().
     * Why: the movement task should receive a normal absolute move command to the safe position.
     */
    g_x_target = MovementParameters_ClampXTarget(AUTOMODE_SAFE_X_MM);
    g_z_target = MovementParameters_ClampZTarget(AUTOMODE_SAFE_Z_MM);
}




void updateSPAInputsFromTime(struct tm *time_info, SPAInputs *spa) {
    /*
     * What: copy local calendar time into the SPA input structure.
     * How: converts struct tm offsets into normal year/month/day/hour fields.
     * Why: SPA expects explicit local date/time values, not an RTC or epoch object.
     */
    spa->year   = time_info->tm_year + 1900;
    spa->month  = time_info->tm_mon + 1;
    spa->day    = time_info->tm_mday;
    spa->hour   = time_info->tm_hour;
    spa->minute = time_info->tm_min;
    spa->second = time_info->tm_sec;
}


void autoMode() {
    /*
     * What: run one automatic tracking cycle.
     * How: reads local RTC, calculates SPA/AOI/XZ, parks at night or invalid targets, then queues useful movement.
     * Why: automatic mode must follow the configured local time source without driving outside the valid range.
     */
    struct tm time_info = {0};
    RTC_GetToTM(&time_info);
    time_t now = mktime(&time_info);

    updateSPAInputsFromTime(&time_info, &g_SPAInputs);

    SPA_f();

    if (autoModeIsNight(now))
    {
        autoModeSetSafeParkTarget();
    }
    else
    {
        aoicalc_f();
        interpolation_f();

        if (!autoModeTargetIsValid(g_x_target, g_z_target))
        {
            autoModeSetSafeParkTarget();
        }
    }

    if (autoModeTargetNeedsMove(g_x_val, g_z_val, g_x_target, g_z_target))
    {
        requestMove();
    }

}

