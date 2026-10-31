#include "rmnet_mock.h"
static int step,fail,genl,notifier,link,ready_pending,ready_error,ready_called,ctl_register_error;
static int rmnet_core_genl_family,rmnet_dev_notifier,rmnet_link_ops;
static int acquire(void) { return ++step==fail?-EIO:0; }
static int genl_register_family(void *p) { int e=acquire();if(!e)genl++;return e; }
static int genl_unregister_family(void *p) { assert(genl==1);genl--;return 0; }
static int register_netdevice_notifier(void *p) { int e=acquire();assert(genl);if(!e)notifier++;return e; }
static int unregister_netdevice_notifier(void *p) { assert(notifier && !link);notifier--;return 0; }
static int rtnl_link_register(void *p) { int e=acquire();assert(notifier);if(!e)link++;return e; }
static void rtnl_link_unregister(void *p) { assert(link);link--; }
static int rmnet_ll_init(void) { int e=acquire();assert(link && notifier && genl);if(!e)ready_pending++;return e; }
static int rmnet_core_genl_deinit(void) { assert(!notifier && !link);return genl_unregister_family(NULL); }
#define RMNET_GENL_SUCCESS 0
#define RMNET_GENL_FAILURE -1
#define RMNET_CORE_GENL_FAMILY_NAME "RMNET"
static int ipa_register_ipa_ready_cb(void (*cb)(void*),void *data) { if(!ready_error)ready_pending++;return ready_error; }
static int ctl_ipa_dev;
static void rmnet_ctl_probe(void *data) { }
static void rmnet_ctl_remove(void *data) { }
static void rmnet_ctl_dl_callback(void *data,void *rx) { }
static int ipa_register_rmnet_ctl_cb(void (*probe)(void*),void *p,void (*remove)(void*),void *r,void (*rx)(void*,void*),void *d) { ready_called++;return ctl_register_error; }
/* FUNCTIONS */
int main(void)
{
 for(fail=1;fail<=4;fail++) { step=0;assert(rmnet_init()==-EIO);assert(!genl && !notifier && !link && !ready_pending); }
 fail=0;step=0;assert(!rmnet_init() && ready_pending==1 && genl && notifier && link);assert(step==4);
 ready_pending=0;for(unsigned i=0;i<3;i++) { ready_error=(int[]){-ENOMEM,-EPERM,-EIO}[i];assert(rmnet_ctl_init()==ready_error && !ready_pending); }
 ready_error=-EEXIST;assert(!rmnet_ctl_init() && ready_called==1 && !ready_pending);
 ctl_register_error=-EIO;assert(rmnet_ctl_init()==-EIO && ready_called==2 && !ready_pending);ctl_register_error=0;
 ready_error=0;assert(!rmnet_ctl_init() && ready_pending==1 && ready_called==2);
 puts("{\"module_acquisition_failures\":8,\"ready_registration_is_last_fallible_step\":true,\"genl_error_does_not_unregister_failed_family\":true,\"already_ready_dispatch_preserved\":true}");
}
