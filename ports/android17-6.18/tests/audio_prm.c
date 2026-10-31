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
#include <limits.h>
#define __init
#define __exit
#define __packed __attribute__((packed))
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define THIS_MODULE ((struct module *)1)
#define CONFIG_MSM_QDSP6_NOTIFIER 1
#define IS_ENABLED(x) (x)
#define DECLARE_FLEX_ARRAY(t,n) t n[]
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELEASE)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_info(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define NOTIFY_OK 1
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef uint32_t u32,__u32;
typedef uint8_t u8;
typedef int32_t s32;
typedef unsigned long kernel_ulong_t;
typedef struct {atomic_uint value;} atomic_t;
#define ATOMIC_INIT(v) {ATOMIC_VAR_INIT(v)}
static int atomic_inc_return(atomic_t *a){return atomic_fetch_add(&a->value,1)+1;}
static unsigned atomic_read(atomic_t *a){return atomic_load(&a->value);}
static void atomic_set(atomic_t *a,unsigned n){atomic_store(&a->value,n);}
struct mutex {pthread_mutex_t lock;bool initialized;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER,true}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));m->initialized=true;}
static void mutex_lock(struct mutex *m){assert(m->initialized&&!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(m->initialized&&!pthread_mutex_unlock(&m->lock));}
static void mutex_destroy(struct mutex *m){assert(m->initialized&&!pthread_mutex_destroy(&m->lock));m->initialized=false;}
typedef struct {pthread_mutex_t lock;} spinlock_t;
#define DEFINE_SPINLOCK(n) spinlock_t n={PTHREAD_MUTEX_INITIALIZER}
#define spin_lock_irqsave(s,f) ((f)=0,assert(!pthread_mutex_lock(&(s)->lock)))
#define spin_unlock_irqrestore(s,f) ((void)(f),assert(!pthread_mutex_unlock(&(s)->lock)))
struct list_head {struct list_head *next,*prev;};
static atomic_int live;
static bool fail_alloc;
static void *kzalloc(size_t n,int f){(void)f;if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
struct device_node {bool sleep,property_absent;};
struct device_driver {const char *name;const void *of_match_table;void *bus;struct module *owner;};
struct device {struct device_node *of_node;struct device_driver *driver;void *data;atomic_uint refs;};
static struct device *get_device(struct device *d){assert(atomic_fetch_add(&d->refs,1)>0);return d;}
static void put_device(struct device *d){assert(atomic_fetch_sub(&d->refs,1)>1);}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static int of_property_read_u32(struct device_node *n,const char *p,u32 *v){assert(n&&!strcmp(p,"qcom,sleep-api-supported"));if(n->property_absent)return -EINVAL;*v=n->sleep;return 0;}
struct notifier_block {int (*notifier_call)(struct notifier_block *,unsigned long,void *);int priority;};
typedef struct {pthread_mutex_t lock;pthread_cond_t changed;bool initialized;} wait_queue_head_t;
#define DECLARE_WAIT_QUEUE_HEAD(n) wait_queue_head_t n={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,true}
static void init_waitqueue_head(wait_queue_head_t *w){assert(!pthread_mutex_init(&w->lock,NULL)&&!pthread_cond_init(&w->changed,NULL));w->initialized=true;}
static void wake_up(wait_queue_head_t *w){assert(w->initialized);pthread_mutex_lock(&w->lock);pthread_cond_broadcast(&w->changed);pthread_mutex_unlock(&w->lock);}
static unsigned long msecs_to_jiffies(int ms){return ms;}
static pthread_mutex_t waiter_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t waiter_changed=PTHREAD_COND_INITIALIZER;
static bool wait_entered,block_wait;
static unsigned wait_calls;
#define wait_event_timeout(w,c,t) ({long result=0;wait_calls++;assert((t)==450&&(w).initialized);pthread_mutex_lock(&(w).lock);if(block_wait&&!(c)){pthread_mutex_lock(&waiter_gate);wait_entered=true;pthread_cond_broadcast(&waiter_changed);pthread_mutex_unlock(&waiter_gate);struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec++;while(!(c)){int r=pthread_cond_timedwait(&(w).changed,&(w).lock,&until);if(r==ETIMEDOUT)break;assert(!r);}}if(c)result=1;pthread_mutex_unlock(&(w).lock);result;})
static unsigned sleeps;
static void msleep(unsigned ms){assert(ms==2);sleeps++;}
struct of_device_id {const char *compatible;};
#define of_match_ptr(t) (t)
/* HEADERS */
static struct device_node node;
static struct gpr_device service={.dev.refs=1,.dev.of_node=&node};
static struct gpr_driver *registered_driver;
static bool service_bound,fail_register;
static int last_probe;
static bool notifier_ready=true,fail_notifier,synchronous_notifier;
static unsigned initial_event=AUDIO_NOTIFIER_SERVICE_UP;
static struct notifier_block *registered_nb;
static pthread_mutex_t notifier_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned notifier_registers,notifier_deregisters;
static void notify(unsigned event){struct audio_notifier_cb_data data={.service=AUDIO_NOTIFIER_PDR_SERVICE,.domain=AUDIO_NOTIFIER_ADSP_DOMAIN};pthread_mutex_lock(&notifier_lock);assert(registered_nb);assert(registered_nb->notifier_call(registered_nb,event,&data)==NOTIFY_OK);pthread_mutex_unlock(&notifier_lock);}
bool audio_notifier_probe_status(void){return notifier_ready;}
int audio_notifier_register(char *name,int domain,struct notifier_block *nb){assert(!strcmp(name,"audio_prm")&&domain==AUDIO_NOTIFIER_ADSP_DOMAIN);if(fail_notifier)return -EPROBE_DEFER;assert(!registered_nb);registered_nb=nb;notifier_registers++;if(synchronous_notifier)notify(initial_event);return 0;}
int audio_notifier_deregister(char *name){assert(!strcmp(name,"audio_prm"));pthread_mutex_lock(&notifier_lock);assert(registered_nb);registered_nb=NULL;notifier_deregisters++;pthread_mutex_unlock(&notifier_lock);return 0;}
int __gpr_driver_register(struct gpr_driver *d,struct module *m){assert(m==THIS_MODULE);if(fail_register)return -EIO;assert(!registered_driver);registered_driver=d;last_probe=d->probe(&service);service_bound=!last_probe;return 0;}
static void withdraw(void){assert(registered_driver&&service_bound);assert(!registered_driver->remove(&service));service_bound=false;}
static void rebind(void){assert(registered_driver&&!service_bound);assert(!registered_driver->probe(&service));service_bound=true;}
void gpr_driver_unregister(struct gpr_driver *d){assert(registered_driver==d);if(service_bound)withdraw();registered_driver=NULL;}
static enum gpr_subsys_state q6_state=GPR_SUBSYS_LOADED;
enum gpr_subsys_state gpr_get_q6_state(void){return q6_state;}
static unsigned ready_calls;
static int ready_failures,ready_event;
static pthread_t readiness_notifier;
static bool readiness_notifier_started;
static void *ready_notify_thread(void *unused){(void)unused;notify(AUDIO_NOTIFIER_SERVICE_DOWN);notify(AUDIO_NOTIFIER_SERVICE_UP);return NULL;}
static bool spf_core_is_apm_ready(int ms){assert(!ms);ready_calls++;if(ready_event){assert(!pthread_create(&readiness_notifier,NULL,ready_notify_thread,NULL));readiness_notifier_started=true;struct timespec pause={.tv_nsec=20000000};nanosleep(&pause,NULL);ready_event=0;}if(ready_failures>0){ready_failures--;return false;}return true;}
static u8 last_request[256];
static unsigned sends;
static int send_error,response_mode=1,callback_result;
static u32 stale_token;
static int respond(struct gpr_device *d,unsigned mode);
int gpr_send_pkt(struct gpr_device *d,struct gpr_pkt *p){assert(d==&service&&p);if(send_error)return send_error;size_t n=GPR_PKT_GET_PACKET_BYTE_SIZE(p->hdr.header);assert(n<=sizeof(last_request));memcpy(last_request,p,n);sends++;if(response_mode)callback_result=respond(d,response_mode);return n;}
static int respond(struct gpr_device *d,unsigned mode){struct gpr_pkt *request=(void *)last_request;apm_module_param_data_t module;memcpy(&module,request->payload+sizeof(apm_cmd_header_t),sizeof(module));u32 words[2]={module.param_id,0};unsigned payload=mode==8?0:8;struct gpr_pkt *p=calloc(1,24+payload);assert(p);p->hdr=request->hdr;p->hdr.header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,24+payload);p->hdr.opcode=request->hdr.opcode==PRM_CMD_REQUEST_HW_RSC?PRM_CMD_RSP_REQUEST_HW_RSC:PRM_CMD_RSP_RELEASE_HW_RSC;if(mode==2)words[1]=1;if(mode==3)p->hdr.token++;if(mode==4)p->hdr.opcode=p->hdr.opcode==PRM_CMD_RSP_REQUEST_HW_RSC?PRM_CMD_RSP_RELEASE_HW_RSC:PRM_CMD_RSP_REQUEST_HW_RSC;if(mode==5)words[0]++;if(mode==6||mode==7||mode==10){p->hdr.opcode=GPR_IBASIC_RSP_RESULT;words[0]=request->hdr.opcode+(mode==6);words[1]=mode==7;}if(mode==9)p->hdr.token=stale_token;if(mode==11)words[1]=0x80000000;if(payload)memcpy(p->payload,words,8);int r=registered_driver->callback(d,p);free(p);return r;}
/* SOURCE */
_Static_assert(sizeof(struct gpr_hdr)==24,"GPR wire");
_Static_assert(sizeof(struct prm_rsp_req_rel_hw_rsc)==8,"PRM response");
static struct clk_cfg clock_cfg={.clk_id=CLOCK_ID_TX_CORE_MCLK,.clk_freq_in_hz=OSR_CLOCK_12_P288_MHZ,.clk_attri=CLOCK_ATTRIBUTE_COUPLE_NO,.clk_root=CLOCK_ROOT_SRC_XO};
static void bind(void){assert(!audio_prm_module_init()&&registered_driver&&service_bound&&registered_nb);}
static void unbind(void){audio_prm_module_exit();assert(!registered_driver&&!registered_nb&&!service_bound&&!live&&service.dev.refs==1);}
static apm_module_param_data_t request_module(void){apm_module_param_data_t m;memcpy(&m,last_request+24+sizeof(apm_cmd_header_t),sizeof(m));return m;}
static unsigned responses(void){bind();assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));response_mode=2;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-EINVAL);response_mode=3;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT);response_mode=4;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT);response_mode=5;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT);response_mode=6;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT);response_mode=7;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-EINVAL);response_mode=8;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT&&callback_result==-EINVAL);response_mode=10;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,0));response_mode=11;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,0)==-EINVAL);response_mode=0;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT);unbind();return 11;}
static unsigned truncated(void){bind();for(unsigned n=0;n<8;n++){u8 *p=calloc(1,24+n);assert(p);struct gpr_hdr *h=(void *)p;h->header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,24+n);h->opcode=PRM_CMD_RSP_REQUEST_HW_RSC;assert(audio_prm_callback(&service,p)==-EINVAL);free(p);}u8 p[32]={0};struct gpr_hdr *h=(void *)p;h->header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,7)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,24);assert(audio_prm_callback(&service,p)==-EINVAL);h->header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,5)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,32);assert(audio_prm_callback(&service,p)==-EINVAL);unbind();return 10;}
static unsigned readiness(void){bind();ready_failures=10;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT&&ready_calls==3&&!sends&&sleeps==2);ready_failures=0;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1)&&ready_calls==4&&sends==1);assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1)&&ready_calls==4);notify(AUDIO_NOTIFIER_SERVICE_DOWN);assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ENETRESET);notify(AUDIO_NOTIFIER_SERVICE_UP);assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1)&&ready_calls==5);q6_state=GPR_SUBSYS_DOWN;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ENETRESET);q6_state=GPR_SUBSYS_LOADED;unbind();return 6;}
static unsigned ready_epoch(void){bind();ready_event=2;int ret=audio_prm_set_lpass_clk_cfg(&clock_cfg,1);assert(readiness_notifier_started&&!pthread_join(readiness_notifier,NULL));assert(ret==-ENETRESET&&!sends);assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1)&&ready_calls==2);unbind();return 2;}
static unsigned identities(void){bind();assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));stale_token=((struct gpr_pkt *)last_request)->hdr.token;assert(stale_token);withdraw();rebind();response_mode=9;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT&&((struct gpr_pkt *)last_request)->hdr.token!=stale_token);response_mode=1;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));stale_token=((struct gpr_pkt *)last_request)->hdr.token;unbind();bind();response_mode=9;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ETIMEDOUT&&((struct gpr_pkt *)last_request)->hdr.token!=stale_token);response_mode=1;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));unbind();return 5;}
static unsigned payloads(void){bind();assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));struct gpr_pkt *p=(void *)last_request;assert(p->hdr.token&&p->hdr.src_port==GPR_SVC_ASM&&p->hdr.dst_port==PRM_MODULE_INSTANCE_ID&&p->hdr.src_domain_id==3&&p->hdr.dst_domain_id==2&&!p->hdr.reserved&&!p->hdr.client_data);prm_cmd_request_rsc_t expected={0};expected.payload_header.payload_size=sizeof(expected)-sizeof(apm_cmd_header_t);expected.module_payload_0.module_instance_id=PRM_MODULE_INSTANCE_ID;expected.module_payload_0.param_id=PARAM_ID_RSC_AUDIO_HW_CLK;expected.module_payload_0.param_size=sizeof(expected)-sizeof(apm_cmd_header_t)-sizeof(apm_module_param_data_t);expected.num_clk_id_t.num_clock_id=1;expected.clock_ids_t[0].clock_id=clock_cfg.clk_id;expected.clock_ids_t[0].clock_freq=clock_cfg.clk_freq_in_hz;expected.clock_ids_t[0].clock_attri=clock_cfg.clk_attri;expected.clock_ids_t[0].clock_root=clock_cfg.clk_root;assert(!memcmp(p->payload,&expected,sizeof(expected)));audio_prm_set_lpi_logging_status(1);assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));expected.clock_ids_t[0].clock_root=CLOCK_ROOT_SRC_RCO;assert(!memcmp(p->payload,&expected,sizeof(expected)));clock_cfg.clk_id=CLOCK_ID_MCLK_1;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));expected.clock_ids_t[0].clock_id=clock_cfg.clk_id;expected.clock_ids_t[0].clock_root=clock_cfg.clk_root;assert(!memcmp(p->payload,&expected,sizeof(expected)));assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,0));prm_cmd_release_rsc_t release={0};release.payload_header.payload_size=sizeof(release)-sizeof(apm_cmd_header_t);release.module_payload_0.module_instance_id=PRM_MODULE_INSTANCE_ID;release.module_payload_0.param_id=PARAM_ID_RSC_AUDIO_HW_CLK;release.module_payload_0.param_size=sizeof(release)-sizeof(apm_cmd_header_t)-sizeof(apm_module_param_data_t);release.num_clk_id_t.num_clock_id=1;release.clock_ids_t[0].clock_id=clock_cfg.clk_id;assert(p->hdr.opcode==PRM_CMD_RELEASE_HW_RSC&&!memcmp(p->payload,&release,sizeof(release)));unbind();return 5;}
static unsigned cores(void){node.sleep=true;bind();assert(!audio_prm_set_vote_against_sleep(1)&&request_module().param_id==PARAM_ID_RSC_CPU_LPR);u32 value;memcpy(&value,last_request+56,4);assert(value==LPR_CPU_SS_SLEEP_DISABLED);assert(!audio_prm_set_vote_against_sleep(0)&&((struct gpr_pkt *)last_request)->hdr.opcode==PRM_CMD_RELEASE_HW_RSC&&request_module().param_id==PARAM_ID_RSC_CPU_LPR);assert(!audio_prm_set_lpass_hw_core_req(NULL,HW_CORE_ID_DCODEC,1)&&request_module().param_id==PARAM_ID_RSC_HW_CORE);memcpy(&value,last_request+56,4);assert(value==HW_CORE_ID_DCODEC);withdraw();node.property_absent=true;rebind();assert(!audio_prm_set_vote_against_sleep(1)&&request_module().param_id==PARAM_ID_RSC_HW_CORE);memcpy(&value,last_request+56,4);assert(value==HW_CORE_ID_LPASS);unbind();return 5;}
static unsigned codec(void){bind();struct prm_earpa_hw_intf_config cfg={.ear_pa_hw_reg_cfg={0x123400,0x567800},.ear_pa_pkd_cfg={0x1234,0x5678}};assert(!audio_prm_set_cdc_earpa_duty_cycling_req(&cfg,1)&&request_module().param_id==PARAM_ID_RSC_HW_CODEC_REG_INFO);prm_cmd_request_cdc_duty_cycling_t *p=(void *)(last_request+24);assert(p->hw_codec_reg_info_t.num_reg_info_t==2);audio_hw_codec_reg_op_info_t *d=&p->hw_codec_reg_info_t.hw_codec_reg[0],*a=&p->hw_codec_reg_info_t.hw_codec_reg[1];assert(d->hw_codec_reg_id==HW_CODEC_DIG_REG_ID_MUTE_CTRL&&!d->hw_codec_reg_addr_msw&&d->hw_codec_reg_addr_lsw==0x123400&&d->num_ops==2&&d->hw_codec_op[0].hw_codec_op_value==DIG_MUTE_ENABLE&&d->hw_codec_op[1].hw_codec_op_value==DIG_MUTE_DISABLE);assert(a->hw_codec_reg_id==HW_CODEC_ANALOG_REG_ID_CMD_FIFO_WRITE&&!a->hw_codec_reg_addr_msw&&a->hw_codec_reg_addr_lsw==0x567800&&a->num_ops==2&&a->hw_codec_op[0].hw_codec_op_value==0x1234&&a->hw_codec_op[1].hw_codec_op_value==0x5678);assert(!audio_prm_set_cdc_earpa_duty_cycling_req(&cfg,0)&&((struct gpr_pkt *)last_request)->hdr.opcode==PRM_CMD_RELEASE_HW_RSC);unbind();return 4;}
static unsigned failures(void){fail_register=true;assert(audio_prm_module_init()==-EIO&&!registered_driver&&!registered_nb);fail_register=false;bind();fail_alloc=true;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ENOMEM);fail_alloc=false;send_error=-EIO;assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-EIO);send_error=0;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));unbind();assert(audio_prm_set_vote_against_sleep(1)==-ENODEV);return 5;}
static unsigned sync_notifier(void){synchronous_notifier=true;initial_event=AUDIO_NOTIFIER_SERVICE_DOWN;bind();assert(audio_prm_set_lpass_clk_cfg(&clock_cfg,1)==-ENETRESET&&!sends);notify(AUDIO_NOTIFIER_SERVICE_UP);assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));unbind();return 2;}
static int threaded_result;
static void *request_thread(void *unused){(void)unused;threaded_result=audio_prm_set_lpass_clk_cfg(&clock_cfg,1);return NULL;}
static void wait_until_request(void){pthread_mutex_lock(&waiter_gate);struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec+=5;while(!wait_entered)assert(!pthread_cond_timedwait(&waiter_changed,&waiter_gate,&until));pthread_mutex_unlock(&waiter_gate);}
static unsigned removal(void){bind();response_mode=0;block_wait=true;pthread_t t;assert(!pthread_create(&t,NULL,request_thread,NULL));wait_until_request();withdraw();assert(!pthread_join(t,NULL)&&threaded_result==-ENETRESET&&!service.dev.data&&service.dev.refs==1);block_wait=false;response_mode=1;rebind();assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));unbind();return 2;}
static unsigned service_down(void){bind();response_mode=0;block_wait=true;pthread_t t;assert(!pthread_create(&t,NULL,request_thread,NULL));wait_until_request();notify(AUDIO_NOTIFIER_SERVICE_DOWN);notify(AUDIO_NOTIFIER_SERVICE_UP);assert(!pthread_join(t,NULL)&&threaded_result==-ENETRESET);block_wait=false;response_mode=1;assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));unbind();return 2;}
#if !NATIVE
static unsigned probe_faults(void){notifier_ready=false;assert(audio_prm_probe(&service)==-EPROBE_DEFER);notifier_ready=true;fail_notifier=true;assert(audio_prm_probe(&service)==-EPROBE_DEFER&&!registered_nb&&service.dev.refs==1);fail_notifier=false;bind();struct device_node other_node={0};struct gpr_device other={.dev.refs=1,.dev.of_node=&other_node};assert(audio_prm_probe(&other)==-EBUSY&&!other.dev.data&&other.dev.refs==1);assert(!audio_prm_remove(&other));assert(audio_prm_set_lpass_clk_cfg(NULL,1)==-EINVAL&&audio_prm_set_cdc_earpa_duty_cycling_req(NULL,1)==-EINVAL);unbind();return 5;}
static unsigned wrap(void){bind();atomic_set(&prm_command_token,UINT32_MAX);assert(!audio_prm_set_vote_against_sleep(1)&&((struct gpr_pkt *)last_request)->hdr.token==1);unbind();return 1;}
#endif
static unsigned wire_frames;
static void dump_wire(void){struct gpr_pkt *p=(void *)last_request;p->hdr.token=0;if(wire_frames++)putchar(',');putchar('"');for(unsigned i=0;i<GPR_PKT_GET_PACKET_BYTE_SIZE(p->hdr.header);i++)printf("%02x",last_request[i]);putchar('"');}
static int wire_dump(void){bind();printf("{\"wire_packets\":[");assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));dump_wire();assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,0));dump_wire();audio_prm_set_lpi_logging_status(1);assert(!audio_prm_set_lpass_clk_cfg(&clock_cfg,1));dump_wire();assert(!audio_prm_set_lpass_hw_core_req(NULL,HW_CORE_ID_DCODEC,1));dump_wire();assert(!audio_prm_set_lpass_hw_core_req(NULL,HW_CORE_ID_DCODEC,0));dump_wire();assert(!audio_prm_set_vote_against_sleep(1));dump_wire();assert(!audio_prm_set_vote_against_sleep(0));dump_wire();withdraw();node.sleep=true;rebind();assert(!audio_prm_set_vote_against_sleep(1));dump_wire();assert(!audio_prm_set_vote_against_sleep(0));dump_wire();assert(!audio_prm_set_lpass_hw_core_req(NULL,HW_CORE_ID_LPASS,1));dump_wire();struct prm_earpa_hw_intf_config cfg={.ear_pa_hw_reg_cfg={0x123400,0x567800},.ear_pa_pkd_cfg={0x1234,0x5678}};assert(!audio_prm_set_cdc_earpa_duty_cycling_req(&cfg,1));dump_wire();assert(!audio_prm_set_cdc_earpa_duty_cycling_req(&cfg,0));dump_wire();
#if !NATIVE
unbind();
#endif
printf("]}");return 0;}
int main(int argc,char **argv){assert(argc==2);if(!strcmp(argv[1],"wire_dump"))return wire_dump();unsigned n=0;
#define CASE(name) if(!strcmp(argv[1],#name))n=name();else
CASE(responses) CASE(truncated) CASE(readiness) CASE(ready_epoch) CASE(identities) CASE(payloads) CASE(cores) CASE(codec) CASE(failures) CASE(sync_notifier) CASE(removal) CASE(service_down)
#if !NATIVE
CASE(probe_faults) CASE(wrap)
#endif
abort();printf("{\"prm_%s_cases\":%u}\n",argv[1],n);}
