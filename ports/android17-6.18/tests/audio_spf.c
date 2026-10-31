#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define __init
#define __exit
#define __packed __attribute__((packed))
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define THIS_MODULE NULL
#define DECLARE_FLEX_ARRAY(t,n) t n[]
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELEASE)
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_info_ratelimited(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define pr_err(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef uint32_t u32,__u32;
typedef uint8_t u8;
typedef int32_t s32;
typedef unsigned long kernel_ulong_t;
typedef struct { atomic_uint value; } atomic_t;
#define ATOMIC_INIT(v) {ATOMIC_VAR_INIT(v)}
static int atomic_inc_return(atomic_t *v){return (int)(atomic_fetch_add(&v->value,1)+1);}
static void atomic_set(atomic_t *v,unsigned n){atomic_store(&v->value,n);}
typedef struct {pthread_mutex_t lock;} spinlock_t;
static void spin_lock_init(spinlock_t *s){assert(!pthread_mutex_init(&s->lock,NULL));}
#define spin_lock_irqsave(s,f) ((f)=0,assert(!pthread_mutex_lock(&(s)->lock)))
#define spin_unlock_irqrestore(s,f) ((void)(f),assert(!pthread_mutex_unlock(&(s)->lock)))
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
struct list_head {struct list_head *next,*prev;};
struct kref {atomic_uint refs;};
static void kref_init(struct kref *r){atomic_store(&r->refs,1);}
static void kref_get(struct kref *r){assert(atomic_fetch_add(&r->refs,1)>0);}
static void kref_put(struct kref *r,void (*release)(struct kref *)){if(atomic_fetch_sub(&r->refs,1)==1)release(r);}
static int live;
static bool fail_alloc;
static void *kzalloc(size_t s,int f){(void)f;if(fail_alloc)return NULL;void *p=calloc(1,s);assert(p);live++;return p;}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
struct device_node {int dummy;};
struct device_driver {const char *name;const void *of_match_table;void *bus,*owner;};
struct module;
struct device {struct device_node *of_node;struct device_driver *driver;void *data,*managed;unsigned refs;};
static struct device *get_device(struct device *d){assert(d->refs);d->refs++;return d;}
static void put_device(struct device *d){assert(d->refs);d->refs--;}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static void *devm_kzalloc(struct device *d,size_t s,int f){assert(!d->managed);return d->managed=kzalloc(s,f);}
struct platform_device {struct device dev;};
static void platform_set_drvdata(struct platform_device *d,void *p){dev_set_drvdata(&d->dev,p);}
static void *platform_get_drvdata(struct platform_device *d){return dev_get_drvdata(&d->dev);}
static unsigned children;
static bool fail_populate;
static int of_platform_populate(struct device_node *n,void *a,void *b,struct device *d){(void)n;(void)a;(void)b;(void)d;children=1;return fail_populate?-EIO:0;}
static void of_platform_depopulate(struct device *d){(void)d;children=0;}
struct work_struct {void (*fn)(struct work_struct *);bool queued;};
#define INIT_WORK(w,f) ((w)->fn=(f))
static bool schedule_work(struct work_struct *w){w->queued=true;return true;}
static bool cancel_work_sync(struct work_struct *w){w->queued=false;return true;}
typedef struct {pthread_mutex_t lock;pthread_cond_t condition;bool signaled;} wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t *w){assert(!pthread_mutex_init(&w->lock,NULL));assert(!pthread_cond_init(&w->condition,NULL));w->signaled=false;}
static void wake_up(wait_queue_head_t *w){pthread_mutex_lock(&w->lock);w->signaled=true;pthread_cond_broadcast(&w->condition);pthread_mutex_unlock(&w->lock);}
static long model_wait(wait_queue_head_t *,long);
#define wait_event_timeout(w,c,t) ({long model_result=model_wait(&(w),(t));(c)?(model_result?model_result:1):0;})
static unsigned long jiffies;
static unsigned long msecs_to_jiffies(int m){return m;}
#define time_after_eq(a,b) ((long)((a)-(b))>=0)
#define time_after(a,b) ((long)((b)-(a))<0)
static void usleep_range(int a,int b){(void)b;jiffies+=a/1000;}
static bool digital_active;
static void digital_cdc_rsc_mgr_init(void){assert(!digital_active);digital_active=true;}
static void digital_cdc_rsc_mgr_exit(void){assert(digital_active);digital_active=false;}
struct of_device_id {const char *compatible;};
#define of_match_ptr(x) (x)
struct platform_driver {int (*probe)(struct platform_device *);void (*remove)(struct platform_device *);struct {const char *name;void *owner;const struct of_device_id *of_match_table;bool suppress_bind_attrs;} driver;};
/* HEADERS */
static struct gpr_device service={.dev.refs=1};
static struct gpr_driver *registered_driver;
static bool bound,fail_register;
static struct gpr_pkt last_request;
static int response_mode,send_error,callback_result;
static unsigned sent;
static u32 stale_token;
int __gpr_driver_register(struct gpr_driver *d,struct module *m){(void)m;if(fail_register)return -EIO;assert(!registered_driver);registered_driver=d;int ret=d->probe(&service);assert(!ret);bound=true;return 0;}
void gpr_driver_unregister(struct gpr_driver *d){assert(registered_driver==d);if(bound){assert(!d->remove(&service));bound=false;}registered_driver=NULL;}
int gpr_send_pkt(struct gpr_device *d,struct gpr_pkt *p){assert(d==&service);if(send_error)return send_error;memcpy(&last_request,p,sizeof(last_request));sent++;assert(GPR_PKT_GET_PACKET_BYTE_SIZE(p->hdr.header)==24&&p->hdr.src_domain_id==3&&p->hdr.dst_domain_id==2&&p->hdr.src_port==3&&p->hdr.dst_port==1&&p->hdr.token&&!p->hdr.reserved&&!p->hdr.client_data);return 24;}
/* SOURCE */
static pthread_mutex_t waiter_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t waiter_condition=PTHREAD_COND_INITIALIZER;
static bool waiter_entered;
static long model_wait(wait_queue_head_t *w,long ticks){
 if(response_mode==6){pthread_mutex_lock(&waiter_gate);waiter_entered=true;pthread_cond_broadcast(&waiter_condition);pthread_mutex_unlock(&waiter_gate);pthread_mutex_lock(&w->lock);struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec++;while(!w->signaled){int r=pthread_cond_timedwait(&w->condition,&w->lock,&until);if(r==ETIMEDOUT)break;assert(!r);}pthread_mutex_unlock(&w->lock);jiffies+=ticks;return 0;}
 if(response_mode){unsigned payload=response_mode==5?0:8;unsigned bytes=24+payload;struct gpr_pkt *p=calloc(1,bytes);assert(p);p->hdr=last_request.hdr;p->hdr.header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,bytes);p->hdr.opcode=response_mode==3||response_mode==4||response_mode==8?GPR_IBASIC_RSP_RESULT:APM_CMD_RSP_GET_SPF_STATE;if(response_mode==2)p->hdr.token++;if(response_mode==7)p->hdr.token=stale_token;if(payload){u32 words[2]={1,0};if(response_mode==3){words[0]=APM_CMD_CLOSE_ALL;words[1]=1;}if(response_mode==4){words[0]=APM_CMD_GET_SPF_STATE;words[1]=1;}if(response_mode==8){words[0]=APM_CMD_CLOSE_ALL;words[1]=0;}memcpy(p->payload,words,8);}callback_result=spf_core_callback(&service,p);free(p);}
 jiffies+=ticks;return 0;
}
static struct device_node node;
static struct platform_device platform={.dev.of_node=&node,.dev.refs=1};
static void bind(void){assert(!spf_core_platform_driver_probe(&platform));assert(registered_driver&&bound&&digital_active);}
static void unbind(void){spf_core_platform_driver_remove(&platform);kfree(platform.dev.managed);platform.dev.managed=NULL;assert(!registered_driver&&!bound&&!digital_active&&!children&&!spf_core_priv&&!platform.dev.data&&platform.dev.refs==1&&!live);}
static unsigned responses(void){bind();response_mode=1;assert(spf_core_is_apm_ready(0));response_mode=2;assert(!spf_core_is_apm_ready(0));response_mode=3;assert(!spf_core_is_apm_ready(0));response_mode=1;assert(spf_core_is_apm_ready(0));response_mode=4;assert(!spf_core_is_apm_ready(0));response_mode=5;assert(!spf_core_is_apm_ready(0)&&callback_result==-EINVAL);response_mode=0;assert(!spf_core_is_apm_ready(0));send_error=-EIO;assert(!spf_core_is_apm_ready(0));send_error=0;response_mode=1;unsigned before=sent;assert(!spf_core_is_apm_ready(-1)&&sent==before);unbind();return 9;}
static unsigned identities(void){
 bind();response_mode=1;assert(spf_core_is_apm_ready(0));stale_token=last_request.hdr.token;
 assert(!spf_core_exit(&service));bound=false;
 assert(!spf_core_probe(&service));bound=true;
 response_mode=7;assert(!spf_core_is_apm_ready(0)&&last_request.hdr.token!=stale_token);
 response_mode=1;assert(spf_core_is_apm_ready(0));stale_token=last_request.hdr.token;
 unbind();bind();response_mode=7;assert(!spf_core_is_apm_ready(0)&&last_request.hdr.token!=stale_token);
 atomic_set(&spf_command_token,UINT32_MAX);response_mode=1;assert(spf_core_is_apm_ready(0)&&last_request.hdr.token==1);
 response_mode=8;struct spf_core_private *priv=spf_core_priv;mutex_lock(&priv->lock);
 assert(!spf_run_command(priv->spf_core_drv,APM_CMD_CLOSE_ALL,Q6_CLOSE_ALL_TIMEOUT_MS));mutex_unlock(&priv->lock);
 response_mode=3;mutex_lock(&priv->lock);assert(spf_run_command(priv->spf_core_drv,APM_CMD_CLOSE_ALL,Q6_CLOSE_ALL_TIMEOUT_MS)==-EREMOTEIO);mutex_unlock(&priv->lock);
 response_mode=1;mutex_lock(&priv->lock);assert(spf_run_command(priv->spf_core_drv,APM_CMD_CLOSE_ALL,Q6_CLOSE_ALL_TIMEOUT_MS)==-ETIMEDOUT);mutex_unlock(&priv->lock);
 unbind();return 7;
}
static unsigned truncated(void){bind();for(unsigned opcode=0;opcode<2;opcode++)for(unsigned bytes=0;bytes<(opcode?4:8);bytes++){struct gpr_pkt *p=calloc(1,24+bytes);assert(p);p->hdr.header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,24+bytes);p->hdr.opcode=opcode?APM_CMD_RSP_GET_SPF_STATE:GPR_IBASIC_RSP_RESULT;assert(spf_core_callback(&service,p)==-EINVAL);free(p);}unbind();return 12;}
static unsigned children_test(void){bind();response_mode=1;struct spf_core_private *priv=spf_core_priv;fail_populate=true;spf_core_add_child_devices(&priv->add_chld_dev_work);assert(!children&&priv->is_initial_boot);fail_populate=false;spf_core_add_child_devices(&priv->add_chld_dev_work);assert(children&&!priv->is_initial_boot);unbind();return 2;}
static unsigned failure_paths(void){fail_alloc=true;assert(spf_core_platform_driver_probe(&platform)==-ENOMEM);fail_alloc=false;fail_register=true;assert(spf_core_platform_driver_probe(&platform)==-EIO);fail_register=false;kfree(platform.dev.managed);platform.dev.managed=NULL;assert(!spf_core_priv&&!digital_active&&!live&&platform.dev.refs==1);bind();struct platform_device other={.dev.refs=1};assert(spf_core_platform_driver_probe(&other)==-EBUSY);assert(other.dev.refs==1);unbind();return 3;}
static bool ready_result;
static void *request_thread(void *unused){(void)unused;ready_result=spf_core_is_apm_ready(0);return NULL;}
static unsigned removal_wait(void){bind();response_mode=6;pthread_t t;assert(!pthread_create(&t,NULL,request_thread,NULL));pthread_mutex_lock(&waiter_gate);struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec+=5;while(!waiter_entered)assert(!pthread_cond_timedwait(&waiter_condition,&waiter_gate,&until));pthread_mutex_unlock(&waiter_gate);assert(!spf_core_exit(&service));bound=false;assert(!pthread_join(t,NULL));assert(!ready_result&&!service.dev.data);unbind();return 1;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;if(!strcmp(argv[1],"responses"))n=responses();else if(!strcmp(argv[1],"identities"))n=identities();else if(!strcmp(argv[1],"truncated"))n=truncated();else if(!strcmp(argv[1],"children"))n=children_test();else if(!strcmp(argv[1],"failures"))n=failure_paths();else if(!strcmp(argv[1],"remove"))n=removal_wait();else abort();printf("{\"spf_%s_cases\":%u}\n",argv[1],n);}
