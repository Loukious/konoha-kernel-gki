#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define __iomem
#define GFP_KERNEL 0
#define IRQF_ONESHOT 1
#define IRQF_TRIGGER_RISING 2
#define IRQF_TRIGGER_FALLING 4
#define IRQ_HANDLED 1
#define EXPORT_SYMBOL(x)
#define U16_MAX UINT16_MAX
#define BIT(n) (1U << (n))
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define min(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,f) ((t *)((char *)(p)-__builtin_offsetof(t,f)))
#define DEFINE_MUTEX(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define DEFINE_STATIC_SRCU(n) pthread_rwlock_t n=PTHREAD_RWLOCK_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define rmb() atomic_thread_fence(memory_order_acquire)
#define wmb() atomic_thread_fence(memory_order_release)
typedef uint8_t u8;
typedef uint8_t __u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int irqreturn_t;
typedef void (*smem_mailbox_urgent_cb)(u16);
#define FLAG_URGENT 1
struct task_struct {int unused;};
static _Thread_local struct task_struct task;
#define current (&task)
struct device {void *of_node;};
struct platform_device {struct device dev;void *data;};
struct qcom_smem_state {int unused;};
struct kref {atomic_int refs;};
static atomic_int live;
static int fail_step,steps,state_refs,state_bit,sig_error,sig_calls;
static u32 sig_mask,sig_value;
static bool fault(void) {return ++steps==fail_step;}
static void *kzalloc(size_t n,int flags) {(void)flags;if(fault())return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void *kmalloc(size_t n,int flags) {(void)flags;if(fault())return NULL;void *p=malloc(n);assert(p);live++;return p;}
static void kfree(void *p) {if(p){live--;free(p);}}
static void kref_init(struct kref *r) {atomic_init(&r->refs,1);}
static void kref_get(struct kref *r) {assert(atomic_fetch_add(&r->refs,1)>0);}
static void kref_put(struct kref *r,void (*release)(struct kref *)) {if(atomic_fetch_sub(&r->refs,1)==1)release(r);}
static int srcu_read_lock(pthread_rwlock_t *s) {assert(!pthread_rwlock_rdlock(s));return 0;}
static void srcu_read_unlock(pthread_rwlock_t *s,int i) {(void)i;assert(!pthread_rwlock_unlock(s));}
static void synchronize_srcu(pthread_rwlock_t *s) {assert(!pthread_rwlock_wrlock(s));assert(!pthread_rwlock_unlock(s));}
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
static struct qcom_smem_state state;
static struct qcom_smem_state *qcom_smem_state_get(struct device *d,const char *name,u32 *bit) {(void)d;assert(!strcmp(name,"smem-mailbox-smp2p-out"));if(fault())return ERR_PTR(-EPROBE_DEFER);state_refs++;*bit=state_bit;return &state;}
static void qcom_smem_state_put(struct qcom_smem_state *s) {assert(s==&state&&state_refs>0);state_refs--;}
static int qcom_smem_state_update_bits(struct qcom_smem_state *s,u32 mask,u32 value) {assert(s==&state);sig_calls++;sig_mask=mask;sig_value=value;return sig_error;}
static unsigned char memory[2048] __attribute__((aligned(8)));
static size_t memory_size=sizeof(memory);
static int memory_error;
static void *qcom_smem_get(unsigned host,unsigned id,size_t *size) {assert(host==1&&id==655);*size=memory_size;return memory_error?ERR_PTR(memory_error):memory;}
static void io_check(const void *p,size_t n) {assert((const u8 *)p>=memory&&(const u8 *)p+n<=memory+memory_size);}
static void memcpy_fromio(void *d,const void *s,size_t n) {io_check(s,n);memcpy(d,s,n);}
static void memcpy_toio(void *d,const void *s,size_t n) {io_check(d,n);memcpy(d,s,n);}
static u32 readl(const void *p) {u32 n;memcpy_fromio(&n,p,4);return n;}
static void writel(u32 n,void *p) {memcpy_toio(p,&n,4);}
static u64 arch_timer_read_cntvct_el0(void) {return UINT64_C(0x123456789abcdef0);}
static pthread_rwlock_t irq_guards[4]={PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER};
static void *irq_data[4];
static irqreturn_t (*irq_handler[4])(int,void *);
static int of_irq_get_byname(void *node,const char *name) {(void)node;if(fault())return -EPROBE_DEFER;int n=-1;assert(sscanf(name,"smem-mailbox-smp2p-%d-in",&n)==1&&n>=1&&n<=4);return n;}
static int devm_request_threaded_irq(struct device *d,int irq,void *top,irqreturn_t (*fn)(int,void *),unsigned flags,const char *name,void *data) {(void)d;(void)name;assert(!top&&flags==7);if(fault())return -EIO;int i=irq-1;if(irq_data[i])return -EBUSY;assert(!pthread_rwlock_wrlock(&irq_guards[i]));int ret=irq_data[i]?-EBUSY:0;if(!ret){irq_data[i]=data;irq_handler[i]=fn;}assert(!pthread_rwlock_unlock(&irq_guards[i]));return ret;}
static void devm_free_irq(struct device *d,int irq,void *data) {(void)d;int i=irq-1;assert(!pthread_rwlock_wrlock(&irq_guards[i]));assert(irq_data[i]==data);irq_data[i]=NULL;irq_handler[i]=NULL;assert(!pthread_rwlock_unlock(&irq_guards[i]));}
static void platform_set_drvdata(struct platform_device *d,void *p) {d->data=p;}
static void *platform_get_drvdata(struct platform_device *d) {return d->data;}
/* SOURCE */
static void fire_irq(int i) {assert(!pthread_rwlock_rdlock(&irq_guards[i]));if(irq_data[i])assert(irq_handler[i](i+1,irq_data[i])==IRQ_HANDLED);assert(!pthread_rwlock_unlock(&irq_guards[i]));}
static struct smem_info valid_info(void) {return (struct smem_info){START_COOKIE,SMEM_VERSION,2,{{0,80,128,256},{3,480,64,128}},END_COOKIE};}
static void reset_memory(void) {memset(memory,0,sizeof(memory));struct smem_info info=valid_info();memcpy(memory,&info,sizeof(info));memory_size=sizeof(memory);memory_error=0;}
static void set_indices(u32 offset,u32 h,u32 t) {memcpy(memory+offset,&h,4);memcpy(memory+offset+4,&t,4);}
static unsigned frames;
static void frames_test(void) {
 u8 input[96];for(unsigned i=0;i<sizeof(input);i++)input[i]=i^0xa5;
 for(u32 tail=0;tail<128;tail+=4)for(u32 length=0;length<=96;length++) {
  set_indices(80,tail,tail);set_indices(216,tail,tail);assert(smem_mailbox_write(0,0,input,length)>=0);
  u32 total=16+ALIGN(length,4);u8 encoded[128]={0};mailbox_copy_from(encoded,memory+216,256,tail,total);
  struct tlv first,second;u64 time;memcpy(&first,encoded,4);memcpy(&time,encoded+4,8);memcpy(&second,encoded+12,4);
  assert(first.type==0&&first.length==12&&second.type==1&&second.length==length+4&&time==arch_timer_read_cntvct_el0());assert(!memcmp(encoded+16,input,length));for(u32 i=16+length;i<total;i++)assert(!encoded[i]);
  mailbox_copy_to(memory+80,128,tail,encoded,total);writel((tail+total)%128,memory+80);
  u8 *output=(void *)1;u16 got=73;unsigned long long timestamp=0;assert(!smem_mailbox_read(0,&output,&got,&timestamp));assert(got==length&&timestamp==time&&readl(memory+84)==(tail+total)%128);if(length)assert(!memcmp(output,input,length));else assert(!output);kfree(output);frames++;
 }
}
static unsigned schemas;
static void descriptor_test(void) {
 struct smem_info base=valid_info(),bad;
 for(size_t n=0;n<sizeof(base);n++){assert(mailbox_validate_info(&base,n,0)==-EPROTO);schemas++;}
 u32 values[]={0,1,3,79,81,127,65535,65536,INT_MAX,UINT32_MAX};
 for(unsigned field=0;field<4;field++)for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++){
  bad=base;u32 *c=(u32 *)&bad.clients[0];c[field]=values[i];int ret=mailbox_validate_info(&bad,sizeof(memory),0);if(ret)schemas++;
 }
 for(u32 i=0;i<32;i++){bad=base;bad.num_of_clients=i;int ret=mailbox_validate_info(&bad,sizeof(memory),0);if(i==1||i==2)assert(!ret);else assert(ret==-EPROTO);schemas++;}
 bad=base;bad.clients[1].id=0;assert(mailbox_validate_info(&bad,sizeof(memory),0)==-EPROTO);bad=base;bad.clients[1].start_loc=84;assert(mailbox_validate_info(&bad,sizeof(memory),0)==-EPROTO);
 bad=base;bad.start_cookie=0;assert(mailbox_validate_info(&bad,sizeof(memory),0)==-EPROTO);bad=base;bad.end_cookie=0;assert(mailbox_validate_info(&bad,sizeof(memory),0)==-EPROTO);bad=base;bad.version=2;assert(mailbox_validate_info(&bad,sizeof(memory),0)==-EPROTO);assert(mailbox_validate_info(&base,sizeof(memory),32)==-EPROTO);
}
static atomic_bool entered,release_callback,finished;
static unsigned callback_calls;
static void slow_callback(u16 pending) {assert(pending==4);callback_calls++;assert(smem_mailbox_stop(0)==-EDEADLK);assert(smem_mailbox_stop(3)==-EDEADLK);atomic_store(&entered,true);while(!atomic_load(&release_callback))usleep(100);int ret=smem_mailbox_stop(0);assert(ret==-EDEADLK||ret==-ENODEV);}
static void *irq_thread(void *unused) {(void)unused;fire_irq(0);return NULL;}
static void *stop_thread(void *unused) {(void)unused;assert(smem_mailbox_stop(0)==1);atomic_store(&finished,true);return NULL;}
static void *remove_thread(void *pdev) {smem_remove(pdev);atomic_store(&finished,true);return NULL;}
static void race_test(struct platform_device *dev,bool remove) {
 assert(smem_mailbox_start(0,slow_callback)==1);set_indices(80,4,0);atomic_store(&entered,false);atomic_store(&release_callback,false);atomic_store(&finished,false);pthread_t irq,withdraw;assert(!pthread_create(&irq,NULL,irq_thread,NULL));while(!atomic_load(&entered))usleep(100);assert(!pthread_create(&withdraw,NULL,remove?remove_thread:stop_thread,remove?dev:NULL));usleep(1000);assert(!atomic_load(&finished));if(!remove)assert(smem_mailbox_start(0,NULL)==-EBUSY);else {struct platform_device other={0};assert(smem_probe(&other)==-EBUSY&&!other.data);}atomic_store(&release_callback,true);assert(!pthread_join(irq,NULL));assert(!pthread_join(withdraw,NULL));assert(atomic_load(&finished));fire_irq(0);
}
int main(void) {
 assert(sizeof(struct smem_info)==80&&sizeof(struct client_info)==16&&sizeof(struct tlv)==4);struct platform_device dev={0};descriptor_test();int faults=0;
 for(int i=1;i<=10;i++){fail_step=i;steps=0;assert(smem_probe(&dev)<0);assert(!live&&!state_refs&&!mailbox_provider&&!dev.data);for(int j=0;j<4;j++)assert(!irq_data[j]);faults++;}
 fail_step=0;steps=0;state_bit=32;assert(smem_probe(&dev)==-EINVAL&&!live&&!state_refs);state_bit=0;memory_error=-ENOENT;assert(!smem_probe(&dev));assert(smem_mailbox_start(0,NULL)==-ENOENT);memory_size=79;memory_error=0;assert(smem_mailbox_start(0,NULL)==-EPROTO);reset_memory();assert(smem_mailbox_start(-1,NULL)==-EINVAL);assert(smem_mailbox_start(0,NULL)==1);assert(smem_mailbox_start(0,NULL)==-EBUSY);fire_irq(0);assert(!callback_calls);frames_test();
 u8 byte=0;assert(smem_mailbox_write(0,2,&byte,1)==-EINVAL);assert(smem_mailbox_write(0,0,NULL,1)==-EINVAL);assert(smem_mailbox_write(0,0,&byte,65535)==-EINVAL);u8 *data;u16 length;unsigned long long stamp;assert(smem_mailbox_read(0,NULL,&length,&stamp)==-EINVAL);
 set_indices(216,0,4);assert(smem_mailbox_write(0,0,&byte,1)==-ENOSPC);set_indices(216,0,20);assert(smem_mailbox_write(0,0,&byte,1)==-ENOSPC);set_indices(216,0,0);fail_step=1;steps=0;assert(smem_mailbox_write(0,0,&byte,1)==-ENOMEM&&readl(memory+216)==0);fail_step=0;
 set_indices(216,0,0);sig_error=-EIO;assert(smem_mailbox_write(0,FLAG_URGENT,&byte,1)==-EIO&&readl(memory+216)==20&&!mailbox_provider->signal_value);sig_error=0;assert(smem_mailbox_write(0,FLAG_URGENT,&byte,1)>=0&&sig_mask==1&&sig_value==1&&mailbox_provider->signal_value==1);
 struct tlv invalid={TLV_TYPE_DATA,3};set_indices(80,4,0);memcpy(memory+88,&invalid,4);assert(smem_mailbox_read(0,&data,&length,&stamp)==-EPROTO&&!data&&readl(memory+84)==0);invalid.length=8;memcpy(memory+88,&invalid,4);assert(smem_mailbox_read(0,&data,&length,&stamp)==-EPROTO&&!data&&readl(memory+84)==0);invalid.type=TLV_TYPE_TIME;invalid.length=4;memcpy(memory+88,&invalid,4);assert(smem_mailbox_read(0,&data,&length,&stamp)==-EPROTO);
 invalid.type=TLV_TYPE_DATA;invalid.length=8;set_indices(80,8,0);memcpy(memory+88,&invalid,4);fail_step=1;steps=0;assert(smem_mailbox_read(0,&data,&length,&stamp)==-ENOMEM&&!data&&readl(memory+84)==0);fail_step=0;
 set_indices(80,128,0);assert(smem_mailbox_read(0,&data,&length,&stamp)==-EPROTO);set_indices(80,0,1);assert(smem_mailbox_read(0,&data,&length,&stamp)==-EPROTO);set_indices(80,0,0);
 struct smem_info changed=valid_info();changed.clients[0].id=1;memcpy(memory,&changed,sizeof(changed));assert(smem_mailbox_write(0,0,&byte,1)==-ESTALE);reset_memory();assert(smem_mailbox_stop(0)==1);assert(smem_mailbox_stop(0)==-EPERM);
 for(int i=0;i<50;i++){race_test(&dev,false);}race_test(&dev,true);assert(!live&&!state_refs&&!mailbox_provider&&!dev.data);assert(smem_mailbox_start(0,NULL)==-ENODEV);assert(!smem_probe(&dev));assert(smem_mailbox_start(0,NULL)==1);smem_remove(&dev);smem_remove(&dev);assert(!live&&!state_refs);
 printf("{\"probe_faults\":%d,\"descriptor_boundary_cases\":%u,\"wrap_padding_and_wire_cases\":%u,\"callback_withdrawal_races\":%u,\"null_callback_safe\":true,\"malformed_reads_preserve_tail\":true,\"signal_failure_after_commit\":true,\"remove_rebind\":true}\n",faults,schemas,frames,callback_calls);
}
