#include "eph_input_mode.h"
#include "aoicalc_func.h"
#include "interpolation_func.h"
#include "global_structs.h"
#include <cmath>

bool ephInputMode(void)
{
	/*
	 * What: calculate one ephemeris-input target from web azimuth/elevation.
	 * How: AOI uses g_AOIInputs.azimuth/elevation, interpolation updates g_x_target/g_z_target,
	 *      and the result is checked before the FSM asks movement_task to move.
	 * Why: ephemeris mode already receives sun angles, so it must not call SPA or block HTTP.
	 */
	aoicalc_f();
	interpolation_f();

	if ((!std::isfinite(g_x_target)) || (!std::isfinite(g_z_target)))
	{
		return false;
	}

	return true;
}	
