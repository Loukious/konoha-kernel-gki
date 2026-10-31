#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#define EXT_DISPLAY_TYPE_DP 44
#define EXT_DISPLAY_TYPE_HDMI 40
#define EXT_DISPLAY_TYPE_MAX UINT32_MAX
#define MSM_EXT_DISP_MAX_CODECS 2
#define GFP_KERNEL 0
#define THIS_MODULE 0
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void*)(intptr_t)(e))
struct device_node { int dummy; };
struct device { struct device_node *of_node; void *data; };
struct platform_device { struct device dev; };
struct extcon_dev { bool registered; };
struct list_head { struct list_head *next,*prev; };
struct mutex { bool initialized; };
struct msm_ext_disp_codec_id { unsigned type; int ctrl_id,stream_id; };
struct msm_ext_disp_data { int dummy; };
struct msm_ext_disp { struct msm_ext_disp_data ext_disp_data;struct platform_device *pdev;struct msm_ext_disp_codec_id current_codec;void *ops;struct extcon_dev *audio_sdev[2];bool audio_session_on;struct list_head display_list;struct mutex lock;bool update_audio;unsigned long extcon_registered; };
static const unsigned int msm_ext_disp_supported_cable[]={44,40,0};
static int fail_step,steps,live,registered,children,module_refs;
static void *allocations[8];static int allocated;
static bool failure(void){return ++steps==fail_step;}
static void *allocate(size_t s){if(failure())return NULL;void *p=calloc(1,s);allocations[allocated++]=p;live++;return p;}
static void *devm_kzalloc(struct device *d,size_t s,int f){(void)d;(void)f;return allocate(s);}
static void devm_kfree(struct device *d,void *p){(void)d;for(int i=0;i<allocated;i++)if(allocations[i]==p){free(p);allocations[i]=NULL;live--;return;}assert(0);}
static void platform_set_drvdata(struct platform_device *p,void *d){p->dev.data=d;}
static void mutex_init(struct mutex *m){assert(!m->initialized);m->initialized=true;}
static void mutex_destroy(struct mutex *m){assert(m->initialized);m->initialized=false;}
#define INIT_LIST_HEAD(p) ((p)->next=(p)->prev=(p))
static void set_bit(int i,unsigned long *p){*p|=1UL<<i;}
static bool test_and_clear_bit(int i,unsigned long *p){bool b=*p&(1UL<<i);*p&=~(1UL<<i);return b;}
static struct extcon_dev *devm_extcon_dev_allocate(struct device *d,const unsigned int *c){(void)d;(void)c;struct extcon_dev *p=allocate(sizeof(*p));return p?p:ERR_PTR(-ENOMEM);}
static int devm_extcon_dev_register(struct device *d,struct extcon_dev *p){(void)d;assert(!IS_ERR(p)&&p);if(failure())return -EIO;p->registered=true;registered++;return 0;}
static void devm_extcon_dev_unregister(struct device *d,struct extcon_dev *p){(void)d;assert(!IS_ERR(p)&&p&&p->registered);p->registered=false;registered--;}
static int of_platform_populate(struct device_node *n,void *a,void *b,struct device *d){(void)n;(void)a;(void)b;struct msm_ext_disp *p=(void*)d->data;assert(p&&p->lock.initialized&&p->display_list.next==&p->display_list&&p->current_codec.type==EXT_DISPLAY_TYPE_MAX);children=1;return failure()?-EIO:0;}
static void of_platform_depopulate(struct device *d){(void)d;children=0;}
static void __module_get(int m){(void)m;assert(registered==2&&children);module_refs++;}
/* FUNCTIONS */
static void cleanup(void){for(int i=0;i<allocated;i++)if(allocations[i]){free(allocations[i]);live--;}allocated=registered=children=module_refs=0;assert(!live);}
int main(void){
 unsigned faults=0,bounds=0;struct device_node n={0};struct platform_device p={.dev.of_node=&n};
 for(int f=1;f<=6;f++){fail_step=f;steps=0;assert(msm_ext_disp_probe(&p)<0);assert(!p.dev.data&&!registered&&!children&&!module_refs);cleanup();faults++;}
 fail_step=0;steps=0;assert(!msm_ext_disp_probe(&p));assert(module_refs==1&&registered==2&&p.dev.data);struct msm_ext_disp *d=p.dev.data;assert(msm_ext_disp_extcon_register(d,-1)==-EINVAL);assert(msm_ext_disp_extcon_register(d,2)==-EINVAL);msm_ext_disp_extcon_unregister(d,-1);msm_ext_disp_extcon_unregister(d,2);assert(registered==2);msm_ext_disp_extcon_unregister(d,0);msm_ext_disp_extcon_unregister(d,0);msm_ext_disp_extcon_unregister(d,1);assert(!registered);cleanup();p.dev.data=NULL;
 assert(!msm_ext_disp_codec_valid(NULL));for(int type=-4;type<256;type++)for(int ctrl=-4;ctrl<5;ctrl++)for(int stream=-6;stream<7;stream++){struct msm_ext_disp_codec_id c={.type=type,.ctrl_id=ctrl,.stream_id=stream};bool expected=(type==40||type==44)&&ctrl>=0&&ctrl<2&&stream>=0&&stream<2;assert(msm_ext_disp_codec_valid(&c)==expected);bounds++;}
 printf("{\"external_display_probe_fault_cases\":%u,\"external_codec_bounds_cases\":%u}\n",faults,bounds);
}
