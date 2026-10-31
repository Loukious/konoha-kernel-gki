#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uint8_t u8;typedef int wait_queue_head_t;
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>(uintptr_t)-4096)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define DISP_ERROR(...) ((void)0)
struct kthread_work { void (*fn)(struct kthread_work*); };
struct kthread_worker { int ready; };
struct disp_display { struct kthread_worker *worker;void *display;int pending_wq; };
struct disp_work { struct kthread_work work;struct disp_display *dd_ptr;wait_queue_head_t *wq;void *data; };
struct disp_feature_ctl { u32 feature_id;int feature_val;u32 tx_len;u8 *tx_ptr;u32 rx_len;u8 *rx_ptr; };
struct disp_count_info { u32 count_info_type;int count_info_val;u32 tx_len;u8 *tx_ptr;u32 rx_len;u8 *rx_ptr; };
struct disp_event { u32 disp_id,type,length; };
static int alloc_step,fail_step,live,queue_fail,consume,event_calls;static size_t expected_len;static struct kthread_work *queued;
static void *kzalloc(size_t n,int flags) { (void)flags;if(++alloc_step==fail_step)return NULL;void *p=calloc(1,n);assert(p);live++;return p; }
static void *kmemdup(const void *p,size_t n,int flags) { void *r=kzalloc(n,flags);if(r)memcpy(r,p,n);return r; }
static void kfree(void *p) { if(p){assert(live>0);live--;free(p);} }
static void kthread_init_work(struct kthread_work *w,void (*fn)(struct kthread_work*)) {w->fn=fn;}
static bool kthread_queue_work(struct kthread_worker *worker,struct kthread_work *w) {assert(worker&&worker->ready);if(queue_fail)return false;assert(!queued);queued=w;return true;}
static void validate(const u8 *p,u32 len) {assert(len==expected_len);for(u32 i=0;i<len;i++)assert(p[i]==(u8)(i*7+3));consume++;}
static int mi_dsi_display_set_disp_param(void *d,struct disp_feature_ctl *c) {(void)d;validate(c->tx_ptr,c->tx_len);return 0;}
static int mi_dsi_display_set_count_info(void *d,struct disp_count_info *c) {(void)d;validate(c->tx_ptr,c->tx_len);return 0;}
static bool is_support_disp_id(int id) {return id>=0&&id<2;}
static bool is_support_disp_event_type(u32 type) {return type<32;}
static void mi_disp_feature_event_notify(struct disp_event *e,u8 *p) {assert(e->length<=8);for(u32 i=0;i<e->length;i++)consume+=p[i]==0x5a;event_calls++;}
/* FUNCTIONS */
int main(void) {
 struct kthread_worker worker={1};struct disp_display d={.worker=&worker};int cases=0,faults=0;
 for(unsigned kind=0;kind<2;kind++)for(unsigned len=0;len<=256;len++) {
  u8 *input=malloc(len?len:1);assert(input);for(unsigned i=0;i<len;i++)input[i]=(u8)(i*7+3);expected_len=len;alloc_step=0;fail_step=0;
  struct disp_feature_ctl f={.tx_len=len,.tx_ptr=input};struct disp_count_info c={.tx_len=len,.tx_ptr=input};
  assert(!(kind?mi_disp_ioctl_set_count_info_queue_work(&d,&c,sizeof(c)):mi_disp_set_feature_queue_work(&d,&f,sizeof(f))));
  free(input);assert(queued);struct kthread_work *w=queued;queued=NULL;w->fn(w);assert(!live);cases++;
 }
 for(unsigned kind=0;kind<2;kind++)for(unsigned fault=1;fault<=3;fault++) {
  u8 input[4]={3,10,17,24};struct disp_feature_ctl f={.tx_len=4,.tx_ptr=input};struct disp_count_info c={.tx_len=4,.tx_ptr=input};
  alloc_step=0;fail_step=fault<=2?(int)fault:0;queue_fail=fault==3;
  assert((kind?mi_disp_ioctl_set_count_info_queue_work(&d,&c,sizeof(c)):mi_disp_set_feature_queue_work(&d,&f,sizeof(f)))<0);assert(!live&&!queued);faults++;
 }
 fail_step=queue_fail=0;
 struct disp_feature_ctl f={.rx_len=1};assert(mi_disp_set_feature_queue_work(&d,&f,sizeof(f))==-EINVAL);f.rx_len=0;assert(mi_disp_set_feature_queue_work(&d,&f,sizeof(f)-1)==-EINVAL);d.worker=NULL;assert(mi_disp_set_feature_queue_work(&d,&f,sizeof(f))==-ENODEV);
 int events=0;for(int id=-1;id<=2;id++)for(u32 type=0;type<=32;type++)for(u32 len=0;len<64;len++) {int before=event_calls;int r=mi_disp_feature_event_notify_by_type(id,type,len,UINT64_C(0x5a5a5a5a5a5a5a5a));bool valid=id>=0&&id<2&&type<32&&len<=8;assert((r==0)==valid);assert(event_calls-before==(int)valid);events++;}
 printf("{\"async_payload_ownership_cases\":%d,\"async_allocator_and_queue_faults\":%d,\"event_length_and_id_cases\":%d}\n",cases,faults,events);
}
