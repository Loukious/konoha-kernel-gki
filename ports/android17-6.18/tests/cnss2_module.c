#include "cnss2_mock.h"
static bool valid=true,compatible=true,debug_live,genl_live,driver_live;
static int failure,step,removals;
static int cnss_platform_driver;
static bool cnss_is_valid_dt_node_found(void) { return valid; }
static bool cnss_check_compatible_node(void) { return compatible; }
static int cnss_debug_init(void) { assert(!debug_live);if(++step==failure)return -ENOMEM;debug_live=true;return 0; }
static void cnss_debug_deinit(void) { assert(debug_live && !genl_live && !driver_live);debug_live=false; }
static int cnss_genl_init(void) { assert(debug_live && !genl_live);if(++step==failure)return -EBUSY;genl_live=true;return 0; }
static void cnss_genl_exit(void) { assert(genl_live && !driver_live);genl_live=false; }
static int platform_driver_register(int *driver) { assert(debug_live && genl_live && !driver_live);if(++step==failure)return -EIO;driver_live=true;return 0; }
static void platform_driver_unregister(int *driver) { assert(driver_live && genl_live);driver_live=false;removals++; }
/* FUNCTIONS */
int main(void)
{
 int errors[]={-ENOMEM,-EBUSY,-EIO};
 for(failure=1;failure<=3;failure++){step=0;assert(cnss_initialize()==errors[failure-1]);assert(!debug_live && !genl_live && !driver_live);}
 failure=0;valid=false;assert(cnss_initialize()==-ENODEV);valid=true;compatible=false;assert(cnss_initialize()==-ENODEV);compatible=true;
 assert(!cnss_initialize());cnss_exit();assert(!debug_live && !genl_live && !driver_live && removals==1);
 puts("{\"module_init_failure_cases\":5,\"genl_exists_during_probe_and_remove\":true,\"unwind_balanced\":true}");
}
