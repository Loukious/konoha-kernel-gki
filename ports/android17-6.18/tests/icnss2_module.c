#include "icnss2_mock.h"
static bool valid=true,aux=true,debug_live,main_live,aux_live,direct_link_registered;
static int fail_main,fail_aux,icnss_driver,icnss_direct_link_driver,pool_freed;
static bool icnss_has_valid_dt_node(void){return valid;}
static bool icnss_direct_link_has_valid_dt_node(void){return aux;}
static void icnss_debug_init(void){assert(!debug_live);debug_live=true;}
static void icnss_debug_deinit(void){assert(debug_live&&!main_live&&!aux_live);debug_live=false;}
static void icnss_deinitialize_mem_pool(void){assert(!main_live);pool_freed++;}
static int platform_driver_register(int *d){assert(debug_live);if(d==&icnss_driver){assert(!main_live && (!aux || aux_live));if(fail_main)return -EIO;main_live=true;}else{assert(!main_live&&!aux_live);if(fail_aux)return -ENOMEM;aux_live=true;}return 0;}
static void platform_driver_unregister(int*d){if(d==&icnss_driver){assert(main_live);main_live=false;}else{assert(aux_live&&!main_live);aux_live=false;}}
/* FUNCTIONS */
int main(void){unsigned cases=0;valid=false;assert(icnss_initialize()==-ENODEV);valid=true;for(int a=0;a<2;a++){aux=a;for(int m=0;m<2;m++)for(int x=0;x<2;x++){fail_main=m;fail_aux=x;direct_link_registered=false;int ret=icnss_initialize();assert(ret==(aux&&x?-ENOMEM:m?-EIO:0));if(!ret)icnss_exit();assert(!main_live&&!aux_live&&!debug_live);cases++;}}printf("{\"module_failure_and_order_cases\":%u,\"auxiliary_before_irreversible_main_probe\":true}\n",cases+1);}
