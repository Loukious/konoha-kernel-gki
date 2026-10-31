#include "fastrpc_services.h"
struct device {int unused;}; struct device_driver {int unused;};
#include "client_types.inc"
struct fastrpc_user {struct list_head user,fastrpc_drivers;int tgid_frpc;atomic_t state;struct fastrpc_device *device;};
struct fastrpc_channel_ctx {spinlock_t lock;struct list_head users;atomic_t teardown;};
struct {spinlock_t glock;struct fastrpc_channel_ctx *gctx[FASTRPC_DEV_MAX];} g_frpc;
#include "client_decl.inc"
void fastrpc_driver_unregister(struct fastrpc_driver *);
#include "clients.inc"
static struct fastrpc_user user;
static struct fastrpc_channel_ctx channel;
static struct completion entered,proceed;
static int probe_mode,cb_mode;static atomic_int callbacks,unreg_done,release_done;
static struct fastrpc_driver *self;
static int probe(struct fastrpc_device *dev) {assert(dev==user.device);if(probe_mode==1)return -EIO;if(probe_mode==2){complete_all(&entered);wait_for_completion(&proceed);}return 0;}
static int callback(struct fastrpc_device *dev,enum fastrpc_driver_status status) {assert(dev && status==FASTRPC_PROC_DOWN);atomic_fetch_add(&callbacks,1);if(cb_mode==1)fastrpc_driver_unregister(self);if(cb_mode==2){complete_all(&entered);wait_for_completion(&proceed);}return 0;}
static void *release_thread(void *unused) {(void)unused;fastrpc_clients_release(&user);atomic_store(&release_done,1);return NULL;}
static void *unregister_thread(void *arg) {fastrpc_driver_unregister(arg);atomic_store(&unreg_done,1);return NULL;}
static void *register_thread(void *arg) {assert(!fastrpc_driver_register(arg));return NULL;}
static void setup(void) {INIT_LIST_HEAD(&user.fastrpc_drivers);user.device=kzalloc(sizeof(*user.device),0);user.device->refs=1;user.device->fl=&user;user.tgid_frpc=42;atomic_set(&user.state,DSP_CREATE_COMPLETE);probe_mode=cb_mode=0;atomic_store(&callbacks,0);atomic_store(&unreg_done,0);atomic_store(&release_done,0);reinit_completion(&entered);reinit_completion(&proceed);}
static struct fastrpc_driver driver(void){return (struct fastrpc_driver){.handle=42,.probe=probe,.callback=callback};}
static void teardown(void){assert(list_empty(&fastrpc_clients));assert(list_empty(&user.fastrpc_drivers));spin_lock(&g_frpc.glock);fastrpc_device_put_locked(user.device);user.device=NULL;spin_unlock(&g_frpc.glock);assert(!atomic_load(&allocations));}
int main(void) {
 spin_lock_init(&g_frpc.glock);spin_lock_init(&channel.lock);INIT_LIST_HEAD(&channel.users);INIT_LIST_HEAD(&user.user);list_add_tail(&user.user,&channel.users);g_frpc.gctx[0]=&channel;init_completion(&entered);init_completion(&proceed);
 setup();struct fastrpc_driver a=driver(),b=driver();assert(fastrpc_driver_register(NULL)==-EINVAL);fail_alloc=1;assert(fastrpc_driver_register(&a)==-ENOMEM);a.handle=99;assert(fastrpc_driver_register(&a)==-ESRCH);a.handle=42;atomic_set(&channel.teardown,1);assert(fastrpc_driver_register(&a)==-ESRCH);atomic_set(&channel.teardown,0);atomic_set(&user.state,0);assert(fastrpc_driver_register(&a)==-ESRCH);atomic_set(&user.state,DSP_CREATE_COMPLETE);probe_mode=1;assert(fastrpc_driver_register(&a)==-EIO && !a.device);probe_mode=0;assert(!fastrpc_driver_register(&a));assert(fastrpc_driver_register(&a)==-EALREADY);assert(!fastrpc_driver_register(&b));assert(user.device->refs==3);fastrpc_clients_release(&user);assert(atomic_load(&callbacks)==2);fastrpc_driver_unregister(&a);fastrpc_driver_unregister(&a);fastrpc_driver_unregister(&b);teardown();
 setup();a=driver();self=&a;cb_mode=1;assert(!fastrpc_driver_register(&a));fastrpc_clients_release(&user);assert(atomic_load(&callbacks)==1 && !a.device);teardown();
 for(int i=0;i<100;i++) {setup();a=driver();cb_mode=2;assert(!fastrpc_driver_register(&a));pthread_t t,u;pthread_create(&t,NULL,release_thread,NULL);wait_for_completion(&entered);pthread_create(&u,NULL,unregister_thread,&a);usleep(1000);assert(!atomic_load(&unreg_done));complete_all(&proceed);pthread_join(t,NULL);pthread_join(u,NULL);assert(atomic_load(&callbacks)==1);teardown();}
 for(int i=0;i<100;i++) {setup();a=driver();probe_mode=2;pthread_t t,u;pthread_create(&t,NULL,register_thread,&a);wait_for_completion(&entered);pthread_create(&u,NULL,release_thread,NULL);usleep(1000);assert(!atomic_load(&release_done));complete_all(&proceed);pthread_join(t,NULL);pthread_join(u,NULL);assert(atomic_load(&callbacks)==1);fastrpc_driver_unregister(&a);teardown();}
 puts("{\"registry_fault_scenarios\":10,\"callback_unregister_races\":100,\"probe_release_races\":100,\"self_unregister\":true}");
}
