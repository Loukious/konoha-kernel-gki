#define IPA_REAL_MUTEX
#define BT_REAL_SRCU
#include "slim_mock.h"
#include <unistd.h>
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{int used;};struct idr{void*slot[256];};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
/* PUBLIC HEADER */
/* PRIVATE HEADER */
#define SRCU_INIT {PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,false}
static struct srcu_struct slim_creation_srcu=SRCU_INIT,slim_status_srcu=SRCU_INIT;
#define srcu_dereference(p,s) READ_ONCE(p)
#define RCU_INIT_POINTER(p,v) WRITE_ONCE(p,v)
static struct ida ctrl_ida;
static int destroyed,nodes;
static void of_node_put(struct device_node*n){if(n)nodes--;}
static bool refcount_dec_and_test(refcount_t*r){return __atomic_sub_fetch(r,1,__ATOMIC_SEQ_CST)==0;}
#undef complete
#define complete(c) WRITE_ONCE((c)->done,1)
static void wait_for_completion(struct completion*c){while(!READ_ONCE(c->done))usleep(1000);}
static void idr_destroy(struct idr*i){destroyed++;}
static void ida_destroy(struct ida*i){destroyed++;}
static void ida_free(struct ida*i,int n){destroyed++;}
static void slim_dev_release(struct device*d);
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t event=PTHREAD_COND_INITIALIZER;
static bool entered,proceed,creating;static int callback_count,removed;
static struct slim_controller*active;
static struct slim_device*children[4];static int num_children;
void slim_msg_response(struct slim_controller*c,u8*p,u8 tid,u8 len){}
static int slim_ctrl_remove_device(struct device*d,void*p){put_device(d);return 0;}
static int device_for_each_child(struct device*d,void*p,int(*fn)(struct device*,void*)){assert(!READ_ONCE(creating));for(int i=0;i<num_children;i++)fn(&children[i]->dev,p);return 0;}
static struct slim_device*find_slim_device(struct slim_controller*c,struct slim_eaddr*ea){for(int i=0;i<num_children;i++)if(children[i]->e_addr.instance==ea->instance)return (void*)get_device(&children[i]->dev);return NULL;}
static struct slim_device*slim_alloc_device(struct slim_controller*c,struct slim_eaddr*ea,struct device_node*n){assert(!pthread_mutex_trylock(&c->devices_lock.lock));pthread_mutex_unlock(&c->devices_lock.lock);if(!ea->instance){pthread_mutex_lock(&gate);creating=entered=true;pthread_cond_broadcast(&event);while(!proceed)pthread_cond_wait(&event,&gate);pthread_mutex_unlock(&gate);struct slim_eaddr peer={.instance=1};struct slim_device*other=slim_get_device(c,&peer);if(IS_ERR(other))assert(PTR_ERR(other)==-ESHUTDOWN);else put_device(&other->dev);}struct slim_device*d=kzalloc(sizeof(*d),0);d->e_addr=*ea;d->ctrl=c;d->dev.refs=1;d->dev.release=slim_dev_release;d->dev.of_node=(void*)1;nodes++;refcount_inc(&c->device_refs);children[num_children++]=d;WRITE_ONCE(creating,false);return d;}
static int status(struct slim_device*d,enum slim_device_status s){pthread_mutex_lock(&gate);callback_count++;entered=true;pthread_cond_broadcast(&event);while(!proceed)pthread_cond_wait(&event,&gate);assert(!READ_ONCE(removed));pthread_mutex_unlock(&gate);return 0;}
static void remove_cb(struct slim_device*d){WRITE_ONCE(removed,1);pthread_mutex_lock(&slim_status_srcu.lock);pthread_cond_broadcast(&slim_status_srcu.changed);pthread_mutex_unlock(&slim_status_srcu.lock);}
/* FUNCTIONS */
static void wait_entered(void){pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&event,&gate);pthread_mutex_unlock(&gate);}
static void wait_sync(struct srcu_struct*s){pthread_mutex_lock(&s->lock);while(!s->synchronizing){assert(s!=&slim_status_srcu||!READ_ONCE(removed));pthread_cond_wait(&s->changed,&s->lock);}pthread_mutex_unlock(&s->lock);}
static void release_gate(void){pthread_mutex_lock(&gate);proceed=true;pthread_cond_broadcast(&event);pthread_mutex_unlock(&gate);}
static void*update(void*p){slim_device_update_status(p,SLIM_DEVICE_STATUS_UP);return NULL;}
static void*remove_device(void*p){slim_device_remove(p);return NULL;}
static void*create(void*p){struct slim_eaddr ea={0};return slim_get_device(p,&ea);}
static void*remove_controller(void*p){slim_unregister_controller(p);return NULL;}
int main(void){alarm(15);pthread_t a,b;struct slim_driver driver={.device_status=status,.remove=remove_cb};struct slim_device d={.status=SLIM_DEVICE_STATUS_DOWN,.status_driver=&driver,.dev={.driver=&driver.driver}};assert(!pthread_create(&a,NULL,update,&d));wait_entered();assert(!pthread_create(&b,NULL,remove_device,&d.dev));wait_sync(&slim_status_srcu);assert(!removed);release_gate();pthread_join(a,NULL);pthread_join(b,NULL);assert(removed&&callback_count==1&&!d.status_driver);slim_device_update_status(&d,SLIM_DEVICE_STATUS_DOWN);assert(callback_count==1);
entered=proceed=false;struct device parent={0};struct slim_controller c={.dev=&parent,.device_refs=1};mutex_init(&c.devices_lock);assert(!pthread_create(&a,NULL,create,&c));wait_entered();assert(!pthread_create(&b,NULL,remove_controller,&c));wait_sync(&slim_creation_srcu);assert(!destroyed&&creating);release_gate();void*out;pthread_join(a,&out);assert(!IS_ERR(out));struct slim_device*held=out;while(READ_ONCE(c.device_refs)>1)usleep(1000);assert(!destroyed&&held->dev.refs==1);assert(PTR_ERR(slim_get_device(&c,&(struct slim_eaddr){0}))==-ESHUTDOWN);put_device(&held->dev);pthread_join(b,NULL);assert(destroyed==3&&!nodes&&!live&&c.devices_gone.done);printf("{\"status_remove_race_drained\":true,\"creation_remove_race_drained\":true,\"recursive_probe_lookup_unlocked\":true,\"retained_device_delays_controller_release\":true}\n");}
