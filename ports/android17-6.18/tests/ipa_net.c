#include "ipa_mock.h"
#define __init
#define __exit
#define IPADBG(...) ((void)0)
#define IPAERR(...) ((void)0)
static int fail_step,step,wwan,rndis;
static int ipa3_wwan_platform_driver_register(void){step++;if(step==fail_step)return -EIO;assert(!wwan);wwan=1;return 0;}
static int rndis_ipa_init_module(void){step++;if(step==fail_step)return -ENOMEM;assert(!rndis);rndis=1;return 0;}
static void ipa3_wwan_cleanup(void){assert(wwan);wwan=0;}
static void rndis_ipa_cleanup_module(void){assert(rndis);rndis=0;}
/* FUNCTIONS */
int main(void){fail_step=1;assert(ipa_late_init()==-EIO&&!wwan&&!rndis);fail_step=2;step=0;assert(ipa_late_init()==-ENOMEM&&!wwan&&!rndis);fail_step=0;step=0;assert(!ipa_late_init()&&wwan&&rndis);ipa_late_exit();assert(!wwan&&!rndis);puts("{\"late_init_failure_rollbacks\":2,\"successful_exit_balanced\":true}");}
