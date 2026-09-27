#include "solar_app.h"
#include "global_structs.h"
#include "autoMode.h"
#include "movement_task.h"
#include "state_machine.h"
#include "storage.h"

void SolarApp_Start(void)
{
  /*
   * What: start the application-level services that are not owned by the ST WiFi demo.
   * How: restores persistent globals, starts movement resources, then launches the FSM task.
   * Why: WiFi/HTTP should come up while startup homing and GPS sync run in the FSM.
   */
  (void)Storage_LoadAll();
  initMovementTask();
  initFSM();
}

void SolarApp_Process(void)
{
  /*
   * What: keep the old polling automode hook available for simple tests.
   * How: calls autoMode() only when auto_on is true.
   * Why: current production flow uses the FSM task, but this preserves compatibility.
   */
  if (auto_on)
  {
    autoMode();
    auto_counter++;
  }
}
