#include "manual_mode.h"
#include "global_structs.h"

void manualModeGoto(float x, float z)
{
  /*
   * What: store the manual destination requested from the web form.
   * How: writes the target globals consumed by movement_task.
   * Why: g_x_val/g_z_val must remain the accepted position until the movement finishes.
   */
  g_x_target = x;
  g_z_target = z;
}
