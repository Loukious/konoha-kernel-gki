#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/poll.h>
#define __init
#define __exit
#define __user
#define __packed __attribute__((packed))
#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define THIS_MODULE ((struct module *)1)
#define EPROBE_DEFER 517
#define ERESTARTSYS 512
#define SZ_4K 4096
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DECLARE_FLEX_ARRAY(t,n) t n[]
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((intptr_t)(p))
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELEASE)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define check_mul_overflow(a,b,out) __builtin_mul_overflow((a),(b),(out))
#define check_add_overflow(a,b,out) __builtin_add_overflow((a),(b),(out))
#define lower_32_bits(a) ((uint32_t)(a))
#define upper_32_bits(a) ((uint32_t)((uint64_t)(a)>>32))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define MAJOR(d) ((unsigned)((d)>>8))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define EPOLLIN POLLIN
#define EPOLLRDNORM POLLRDNORM
#define EPOLLERR POLLERR
#define EPOLLHUP POLLHUP
#define module_param_named(...)
typedef uint32_t u32,__u32;
typedef uint16_t u16;
typedef uint64_t u64,dma_addr_t;
typedef uint8_t u8;
typedef int32_t s32;
typedef unsigned long kernel_ulong_t;
typedef unsigned __poll_t;
typedef struct {atomic_int value;} atomic_t;
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
static void mutex_destroy(struct mutex *m){assert(!pthread_mutex_destroy(&m->lock));}
static bool fail_lock;
static int mutex_lock_interruptible(struct mutex *m){if(fail_lock)return -ERESTARTSYS;mutex_lock(m);return 0;}
typedef struct {pthread_mutex_t lock;} spinlock_t;
static void spin_lock_init(spinlock_t *s){assert(!pthread_mutex_init(&s->lock,NULL));}
#define spin_lock_irqsave(s,f) ((f)=0,assert(!pthread_mutex_lock(&(s)->lock)))
#define spin_unlock_irqrestore(s,f) ((void)(f),assert(!pthread_mutex_unlock(&(s)->lock)))
struct list_head {struct list_head *next,*prev;};
static atomic_int live;
static int fail_alloc=-1;
static void *kzalloc(size_t n,int f){(void)f;if(!fail_alloc)return NULL;if(fail_alloc>0)fail_alloc--;void *p=calloc(1,n);assert(p);live++;return p;}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
static bool fail_copy,fail_dup;
static unsigned duplicates;
static void *memdup_user(const void *p,size_t n){duplicates++;if(fail_dup)return ERR_PTR(-EFAULT);void *q=kzalloc(n,0);if(!q)return ERR_PTR(-ENOMEM);memcpy(q,p,n);return q;}
static unsigned copy_to_user(void *to,const void *from,size_t n){if(fail_copy)return n;memcpy(to,from,n);return 0;}
struct device;
struct cdev;
struct inode {struct cdev *i_cdev;};
struct file {void *private_data;int f_flags;};
typedef struct {int unused;} poll_table;
struct file_operations {struct module *owner;int (*open)(struct inode *,struct file *);int (*release)(struct inode *,struct file *);ssize_t (*read)(struct file *,char *,size_t,loff_t *);ssize_t (*write)(struct file *,const char *,size_t,loff_t *);__poll_t (*poll)(struct file *,poll_table *);};
struct class {unsigned unused;};
struct device_node {unsigned unused;};
struct device_driver {const char *name;const void *of_match_table;void *bus;struct module *owner;};
struct device {atomic_uint refs;void (*release)(struct device *);struct device *parent;struct class *class;dev_t devt;struct device_driver *driver;struct device_node *of_node;void *data,*managed[4];unsigned allocations;bool added;};
struct cdev {const struct file_operations *fops;struct module *owner;struct device *parent;bool added;};
struct platform_device {struct device dev;};
static struct device *get_device(struct device *d){assert(d&&atomic_fetch_add(&d->refs,1)>0);return d;}
static void put_device(struct device *d){assert(d);unsigned old=atomic_fetch_sub(&d->refs,1);assert(old);if(old==1&&d->release)d->release(d);}
static void device_initialize(struct device *d){atomic_init(&d->refs,1);}
static void platform_set_drvdata(struct platform_device *p,void *v){p->dev.data=v;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
static void dev_set_drvdata(struct device *d,void *v){d->data=v;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static void *devm_kzalloc(struct device *d,size_t n,int f){void *p=kzalloc(n,f);if(p){assert(d->allocations<4);d->managed[d->allocations++]=p;}return p;}
static void managed_cleanup(struct device *d){for(unsigned i=0;i<d->allocations;i++)kfree(d->managed[i]);d->allocations=0;}
static int fail_stage;
static unsigned next_major=10,regions,classes,cdevs;
static dev_t active_regions[8];
static int alloc_chrdev_region(dev_t *d,unsigned minor,unsigned n,const char *name){assert(!minor&&n==1&&!strcmp(name,"aud_pasthru_adsp"));if(fail_stage==1)return -ENOMEM;*d=(next_major++)<<8;for(unsigned i=0;i<8;i++)if(!active_regions[i]){active_regions[i]=*d;regions++;return 0;}abort();}
static void unregister_chrdev_region(dev_t d,unsigned n){assert(n==1);for(unsigned i=0;i<8;i++)if(d&&active_regions[i]==d){active_regions[i]=0;assert(regions-->0);return;}}
static struct class *class_create(const char *n){assert(!strcmp(n,"aud_pasthru_adsp"));if(fail_stage==2)return ERR_PTR(-ENOMEM);struct class *c=kzalloc(sizeof(*c),0);if(!c)return ERR_PTR(-ENOMEM);classes++;return c;}
static void class_destroy(struct class *c){assert(c&&!IS_ERR(c)&&classes-->0);kfree(c);}
static int dev_set_name(struct device *d,const char *fmt,...){assert(d&&fmt);return fail_stage==3?-ENOMEM:0;}
static void standalone_release(struct device *d){kfree(d);}
static struct device *native_devices[8];
static struct device *device_create(struct class *c,struct device *parent,dev_t devt,void *v,const char *n){assert(n);if(fail_stage==3)return ERR_PTR(-ENOMEM);struct device *d=kzalloc(sizeof(*d),0);if(!d)return ERR_PTR(-ENOMEM);device_initialize(d);d->release=standalone_release;d->class=c;d->parent=parent;d->devt=devt;d->data=v;d->added=true;for(unsigned i=0;i<8;i++)if(!native_devices[i]){native_devices[i]=d;return d;}abort();}
static void device_destroy(struct class *c,dev_t d){for(unsigned i=0;i<8;i++)if(native_devices[i]&&native_devices[i]->class==c&&native_devices[i]->devt==d){put_device(native_devices[i]);native_devices[i]=NULL;return;}}
static struct cdev *published;
static bool open_on_publish;
static struct file published_file;
static struct inode published_inode;
static void publish_hook(struct cdev *c){published=c;if(open_on_publish){published_inode.i_cdev=c;assert(!c->fops->open(&published_inode,&published_file));}}
static void cdev_init(struct cdev *c,const struct file_operations *f){c->fops=f;}
static int cdev_add(struct cdev *c,dev_t t,unsigned n){assert(t&&n==1);if(fail_stage==4)return -ENOMEM;cdevs++;c->added=true;publish_hook(c);return 0;}
static void cdev_del(struct cdev *c){assert(c->added&&cdevs-->0);c->added=false;published=NULL;}
static int cdev_device_add(struct cdev *c,struct device *d){if(fail_stage==4)return -ENOMEM;if(fail_stage==5){get_device(d);publish_hook(c);put_device(d);published=NULL;return -ENOMEM;}get_device(d);c->parent=d;cdevs++;c->added=d->added=true;publish_hook(c);return 0;}
static void cdev_device_del(struct cdev *c,struct device *d){assert(c->parent==d&&c->added&&d->added&&cdevs-->0);c->added=d->added=false;c->parent=NULL;published=NULL;put_device(d);}
static size_t strscpy(char *to,const char *from,size_t n){size_t z=strlen(from);assert(z<n);memcpy(to,from,z+1);return z;}
static bool fail_logger,require_logger;
static unsigned loggers,missing_logs;
static void *ipc_log_context_create(int pages,const char *name,int flags){assert(pages==2&&!strcmp(name,"aud_pasthru_adsp")&&!flags);if(fail_logger)return NULL;void *p=kzalloc(1,0);if(p)loggers++;return p;}
static void ipc_log_context_destroy(void *p){assert(p&&loggers-->0);kfree(p);}
static void ipc_log_string(void *p,const char *fmt,...){assert(fmt);if(!p){missing_logs++;assert(!require_logger);}}
struct sk_buff {struct sk_buff *next;size_t len,capacity;u8 *data;};
struct sk_buff_head {struct sk_buff *head,*tail;atomic_uint qlen;spinlock_t lock;};
static bool fail_atomic_skb,fail_kernel_skb;
static unsigned skb_allocations,skb_live;
static struct sk_buff *alloc_skb(size_t n,int f){skb_allocations++;if((f==GFP_ATOMIC&&fail_atomic_skb)||(f==GFP_KERNEL&&fail_kernel_skb))return NULL;struct sk_buff *b=kzalloc(sizeof(*b),0);if(!b)return NULL;b->data=kzalloc(n?n:1,0);if(!b->data){kfree(b);return NULL;}b->capacity=n;skb_live++;return b;}
static void kfree_skb(struct sk_buff *b){if(b){assert(skb_live-->0);kfree(b->data);kfree(b);}}
static size_t skb_tailroom(struct sk_buff *b){assert(b->len<=b->capacity);return b->capacity-b->len;}
static void skb_put_data(struct sk_buff *b,const void *p,size_t n){assert(n<=skb_tailroom(b));memcpy(b->data+b->len,p,n);b->len+=n;}
static void skb_queue_head_init(struct sk_buff_head *q){q->head=q->tail=NULL;atomic_init(&q->qlen,0);spin_lock_init(&q->lock);}
static struct sk_buff *skb_peek(struct sk_buff_head *q){return q->head;}
static struct sk_buff *__skb_dequeue(struct sk_buff_head *q){struct sk_buff *b=q->head;if(b){q->head=b->next;if(!q->head)q->tail=NULL;b->next=NULL;assert(atomic_fetch_sub(&q->qlen,1)>0);}return b;}
static struct sk_buff *skb_dequeue(struct sk_buff_head *q){unsigned long f;spin_lock_irqsave(&q->lock,f);struct sk_buff *b=__skb_dequeue(q);spin_unlock_irqrestore(&q->lock,f);return b;}
static void skb_queue_tail(struct sk_buff_head *q,struct sk_buff *b){unsigned long f;spin_lock_irqsave(&q->lock,f);if(q->tail)q->tail->next=b;else q->head=b;q->tail=b;atomic_fetch_add(&q->qlen,1);spin_unlock_irqrestore(&q->lock,f);}
static bool skb_queue_empty(struct sk_buff_head *q){return atomic_load(&q->qlen)==0;}
static unsigned skb_queue_len(struct sk_buff_head *q){return atomic_load(&q->qlen);}
static void skb_queue_purge(struct sk_buff_head *q){struct sk_buff *b;while((b=skb_dequeue(q)))kfree_skb(b);}
struct work_struct {void (*fn)(struct work_struct *);bool queued,running;};
#define INIT_WORK(w,f) ((w)->fn=(f))
static struct work_struct *pending_work;
static void *system_unbound_wq=(void *)1;
static bool queue_work(void *q,struct work_struct *w){assert(q==system_unbound_wq&&w->fn);pending_work=w;w->queued=true;return true;}
static bool cancel_work_sync(struct work_struct *w){assert(!w->running);w->queued=false;if(pending_work==w)pending_work=NULL;return true;}
static void run_pending(void){if(pending_work){struct work_struct *w=pending_work;pending_work=NULL;w->queued=false;w->running=true;w->fn(w);w->running=false;}}
typedef struct {pthread_mutex_t lock;pthread_cond_t changed;} wait_queue_head_t;
static pthread_mutex_t wait_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_changed=PTHREAD_COND_INITIALIZER;
static bool wait_entered;
static void init_waitqueue_head(wait_queue_head_t *w){assert(!pthread_mutex_init(&w->lock,NULL)&&!pthread_cond_init(&w->changed,NULL));}
static void wake_up_interruptible(wait_queue_head_t *w){pthread_mutex_lock(&w->lock);pthread_cond_broadcast(&w->changed);pthread_mutex_unlock(&w->lock);}
static bool interrupt_wait;
#define wait_event_interruptible(w,c) ({int result=0;pthread_mutex_lock(&(w).lock);while(!(c)){if(interrupt_wait){result=-ERESTARTSYS;break;}pthread_mutex_lock(&wait_gate);wait_entered=true;pthread_cond_broadcast(&wait_changed);pthread_mutex_unlock(&wait_gate);struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec+=2;int r=pthread_cond_timedwait(&(w).changed,&(w).lock,&until);if(r==ETIMEDOUT){result=-ERESTARTSYS;break;}assert(!r);}pthread_mutex_unlock(&(w).lock);result;})
static void poll_wait(struct file *f,wait_queue_head_t *q,poll_table *p){assert(f&&q);(void)p;}
struct of_device_id {const char *compatible;};
#define of_match_ptr(t) (t)
struct platform_driver {int (*probe)(struct platform_device *);void (*remove)(struct platform_device *);struct device_driver driver;};
static int platform_driver_register(struct platform_driver *p){assert(p);return 0;}
static void platform_driver_unregister(struct platform_driver *p){assert(p);}
/* HEADERS */
static struct gpr_device service={.dev.refs=1};
static struct gpr_driver *registered_driver;
static bool service_bound,callback_on_register;
static pthread_mutex_t binding_lock=PTHREAD_MUTEX_INITIALIZER;
static int model_rx(void *p){pthread_mutex_lock(&binding_lock);int r=registered_driver&&service_bound?registered_driver->callback(&service,p):-ENODEV;pthread_mutex_unlock(&binding_lock);return r;}
static void model_unbind(void){pthread_mutex_lock(&binding_lock);if(service_bound){assert(!registered_driver->remove(&service));service_bound=false;}pthread_mutex_unlock(&binding_lock);}
static void model_rebind(void){pthread_mutex_lock(&binding_lock);assert(registered_driver&&!service_bound);assert(!registered_driver->probe(&service));service_bound=true;pthread_mutex_unlock(&binding_lock);}
int __gpr_driver_register(struct gpr_driver *d,struct module *m){assert(m==THIS_MODULE);if(fail_stage==6)return -EIO;assert(!registered_driver);registered_driver=d;model_rebind();if(callback_on_register){struct {struct gpr_hdr hdr;u32 words[2];} p={.hdr.header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,32),.hdr.opcode=GPR_IBASIC_RSP_RESULT,.words={0x01001000,0}};assert(!model_rx(&p));}return 0;}
void gpr_driver_unregister(struct gpr_driver *d){assert(registered_driver==d);model_unbind();registered_driver=NULL;}
static u8 last_sent[4096];
static unsigned sends;
static size_t sent_bytes;
static int send_error;
static pthread_mutex_t send_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t send_changed=PTHREAD_COND_INITIALIZER;
static bool block_send,send_entered,send_release;
int gpr_send_pkt(struct gpr_device *d,struct gpr_pkt *p){assert(d==&service&&p);if(block_send){pthread_mutex_lock(&send_gate);send_entered=true;pthread_cond_broadcast(&send_changed);while(!send_release)pthread_cond_wait(&send_changed,&send_gate);pthread_mutex_unlock(&send_gate);assert(dev_get_drvdata(&d->dev));}if(send_error)return send_error;sent_bytes=GPR_PKT_GET_PACKET_BYTE_SIZE(p->hdr.header);assert(sent_bytes<=sizeof(last_sent));memcpy(last_sent,p,sent_bytes);sends++;return sent_bytes;}
static unsigned close_alls,ion_crashes,translations;
static int translated_fds[8],translate_error_fd=-1;
static dma_addr_t addresses[8]={0x100000000ULL,0x200001000ULL,0x300002000ULL};
static size_t lengths[8]={4096,8192,16384};
static pthread_mutex_t close_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t close_changed=PTHREAD_COND_INITIALIZER;
static bool block_close,close_entered,close_release;
static atomic_uint close_inflight;
static void spf_core_apm_close_all(void){assert(!atomic_fetch_add(&close_inflight,1));if(block_close){pthread_mutex_lock(&close_gate);close_entered=true;pthread_cond_broadcast(&close_changed);while(!close_release)pthread_cond_wait(&close_changed,&close_gate);pthread_mutex_unlock(&close_gate);}close_alls++;assert(atomic_fetch_sub(&close_inflight,1)==1);}
static void msm_audio_ion_crash_handler(void){ion_crashes++;}
static int msm_audio_get_phy_addr(int fd,dma_addr_t *address,size_t *length){assert(address&&length);if(fd<0||fd>=8||!lengths[fd])return -EBADF;if(fd==translate_error_fd)return -EIO;assert(translations<8);translated_fds[translations++]=fd;*address=addresses[fd];*length=lengths[fd];return 0;}
/* SOURCE */
_Static_assert(sizeof(struct gpr_hdr)==24,"native GPR header");
_Static_assert(sizeof(struct gpr_pkt)==24,"native GPR fixed packet");
_Static_assert(offsetof(struct gpr_hdr,opcode)==20,"native GPR opcode");
_Static_assert(sizeof(struct audio_pkt_apm_cmd_shared_mem_map_regions_t)==8,"native map header");
_Static_assert(sizeof(struct audio_pkt_apm_shared_map_region_payload_t)==12,"native map region");
_Static_assert(offsetof(struct audio_pkt_apm_shared_map_region_payload_t,mem_size_bytes)==8,"native region size");
static struct platform_device platform={.dev.refs=1},replacement={.dev.refs=1};
static struct inode inodes[4];
static struct file files[4];
static void bind(struct platform_device *p){assert(!audio_pkt_platform_driver_probe(p));assert(published&&registered_driver&&service_bound);run_pending();}
static void remove_provider(struct platform_device *p){audio_pkt_platform_driver_remove(p);managed_cleanup(&p->dev);}
static void open_file(unsigned i){assert(i<4&&published);inodes[i].i_cdev=published;assert(!audio_pkt_fops.open(&inodes[i],&files[i]));}
static void close_file(unsigned i){assert(!audio_pkt_fops.release(&inodes[i],&files[i]));}
static ssize_t write_file(unsigned i,void *p,size_t n){return audio_pkt_fops.write(&files[i],p,n,NULL);}
static ssize_t read_file(unsigned i,void *p,size_t n){return audio_pkt_fops.read(&files[i],p,n,NULL);}
static void clean(void){assert(!regions&&!classes&&!cdevs&&!loggers&&!skb_live&&!live&&!pending_work&&!registered_driver&&!service_bound&&platform.dev.refs==1&&replacement.dev.refs==1);}
static void packet(void *buf,size_t n,size_t header,u32 opcode){memset(buf,0,n);struct gpr_hdr *h=buf;h->header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,header/4)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,n);h->src_domain_id=3;h->dst_domain_id=2;h->src_port=3;h->dst_port=1;h->token=0xabcdef01;h->opcode=opcode;}
static unsigned tx_lengths(void){bind(&platform);open_file(0);u8 p[4097];packet(p,32,24,0x123);unsigned before=duplicates;assert(write_file(0,p,4097)==-EINVAL&&duplicates==before);assert(write_file(0,p,0)==0&&duplicates==before);for(unsigned n=1;n<24;n++)assert(write_file(0,p,n)==-EINVAL&&duplicates==before);assert(!sends);close_file(0);remove_provider(&platform);clean();return 25;}
static unsigned tx_headers(void){bind(&platform);open_file(0);u8 p[64];packet(p,32,24,0x123);assert(write_file(0,p,40)==-EINVAL&&!sends);for(unsigned h=0;h<24;h+=4){packet(p,32,h,0x123);assert(write_file(0,p,32)==-EINVAL);}packet(p,32,36,0x123);assert(write_file(0,p,32)==-EINVAL);packet(p,64,60,0x123);assert(write_file(0,p,64)==64&&sends==1&&sent_bytes==64&&!memcmp(p,last_sent,64));close_file(0);remove_provider(&platform);clean();return 9;}
static unsigned tx_faults(void){bind(&platform);open_file(0);u8 p[32];packet(p,32,24,0x123);fail_dup=true;assert(write_file(0,p,32)==-EFAULT);fail_dup=false;fail_alloc=0;assert(write_file(0,p,32)==-ENOMEM);fail_alloc=-1;fail_lock=true;assert(write_file(0,p,32)==-ERESTARTSYS);fail_lock=false;send_error=-ECONNRESET;assert(write_file(0,p,32)==-ENETRESET);send_error=-EIO;assert(write_file(0,p,32)==-EIO);send_error=0;assert(write_file(0,p,32)==32);model_unbind();assert(write_file(0,p,32)==-ENETRESET);model_rebind();assert(write_file(0,p,32)==32);close_file(0);remove_provider(&platform);clean();return 8;}
static unsigned rx_headers(void){bind(&platform);open_file(0);u8 p[4096],out[4096];packet(p,24,24,0x123);struct gpr_hdr *h=(void *)p;h->header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,65560);assert(model_rx(p)==-EINVAL);for(unsigned n=0;n<24;n++){h->header=GPR_SET_FIELD(GPR_PKT_HEADER_SIZE,6)|GPR_SET_FIELD(GPR_PKT_PACKET_SIZE,n);assert(model_rx(p)==-EINVAL);}for(unsigned n=0;n<24;n+=4){packet(p,32,n,0x123);assert(model_rx(p)==-EINVAL);}packet(p,24,28,0x123);assert(model_rx(p)==-EINVAL);packet(p,4096,60,0x123);assert(!model_rx(p)&&read_file(0,out,sizeof(out))==4096&&!memcmp(p,out,4096));close_file(0);remove_provider(&platform);clean();return 33;}
static unsigned rx_basic(void){bind(&platform);open_file(0);for(unsigned n=0;n<8;n++){u8 *p=calloc(1,24+n);assert(p);packet(p,24+n,24,GPR_IBASIC_RSP_RESULT);assert(model_rx(p)==-EINVAL);free(p);}u8 p[36],out[36];packet(p,36,28,GPR_IBASIC_RSP_RESULT);u32 rsp[2]={APM_CMD_GRAPH_OPEN,0};memcpy(p+28,rsp,8);assert(!model_rx(p)&&read_file(0,out,36)==36&&!memcmp(p,out,36));close_file(0);remove_provider(&platform);clean();return 9;}
static void map_packet(u8 *p,size_t header,unsigned count){size_t n=header+8+12*count;packet(p,n,header,APM_CMD_SHARED_MEM_MAP_REGIONS);struct audio_pkt_apm_cmd_shared_mem_map_regions_t m={.mem_pool_id=2,.num_regions=count,.property_flag=APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE};memcpy(p+header,&m,8);for(unsigned i=0;i<count;i++){struct audio_pkt_apm_shared_map_region_payload_t r={.shm_addr_lsw=i,.mem_size_bytes=1024*(i+1)};memcpy(p+header+8+12*i,&r,12);}}
static unsigned regions_multiple(void){bind(&platform);open_file(0);u8 p[80],original[80];map_packet(p,24,3);memcpy(original,p,68);assert(write_file(0,p,68)==68&&translations==3&&!memcmp(p,original,68));for(unsigned i=0;i<3;i++){struct audio_pkt_apm_shared_map_region_payload_t r;memcpy(&r,last_sent+32+12*i,12);assert(r.shm_addr_lsw==lower_32_bits(addresses[i])&&r.shm_addr_msw==upper_32_bits(addresses[i])&&r.mem_size_bytes==1024*(i+1)&&translated_fds[i]==(int)i);}translations=0;map_packet(p,28,2);assert(write_file(0,p,60)==60&&translations==2);struct audio_pkt_apm_shared_map_region_payload_t r;memcpy(&r,last_sent+36,12);assert(r.shm_addr_lsw==lower_32_bits(addresses[0])&&r.shm_addr_msw==1);close_file(0);remove_provider(&platform);clean();return 5;}
static unsigned regions_bounds(void){bind(&platform);open_file(0);u8 p[64];map_packet(p,24,1);struct audio_pkt_apm_cmd_shared_mem_map_regions_t *m=(void *)(p+24);m->num_regions=2;assert(write_file(0,p,44)==-EINVAL&&!translations&&!sends);m->num_regions=0;assert(write_file(0,p,44)==-EINVAL);m->num_regions=UINT16_MAX;assert(write_file(0,p,44)==-EINVAL);packet(p,28,24,APM_CMD_SHARED_MEM_MAP_REGIONS);assert(write_file(0,p,28)==-EINVAL);map_packet(p,24,1);m->property_flag=0;assert(write_file(0,p,44)==44&&!translations&&!memcmp(p,last_sent,44));close_file(0);remove_provider(&platform);clean();return 5;}
static unsigned regions_invalid(void){bind(&platform);open_file(0);u8 p[64],original[64];map_packet(p,24,1);struct audio_pkt_apm_shared_map_region_payload_t *r=(void *)(p+32);r->shm_addr_msw=1;assert(write_file(0,p,44)==-EINVAL&&!translations);r->shm_addr_msw=0;r->shm_addr_lsw=UINT32_MAX;assert(write_file(0,p,44)==-EINVAL);r->shm_addr_lsw=0;r->mem_size_bytes=0;assert(write_file(0,p,44)==-EINVAL);r->mem_size_bytes=4097;assert(write_file(0,p,44)==-EINVAL);translations=0;map_packet(p,24,2);translate_error_fd=1;memcpy(original,p,56);assert(write_file(0,p,56)==-EIO&&!sends&&!memcmp(p,original,56)&&translations==1);translate_error_fd=-1;close_file(0);remove_provider(&platform);clean();return 5;}
static unsigned reads(void){bind(&platform);open_file(0);u8 p[40],out[64];packet(p,40,24,0x123);assert(!model_rx(p));assert(read_file(0,out,24)==-EMSGSIZE);assert(audio_pkt_fops.poll(&files[0],NULL)&POLLIN);assert(read_file(0,out,40)==40&&!memcmp(p,out,40));files[0].f_flags=O_NONBLOCK;assert(read_file(0,out,64)==-EAGAIN);assert(read_file(0,out,0)==0);assert(!model_rx(p));fail_copy=true;assert(read_file(0,out,64)==-EFAULT);fail_copy=false;assert(read_file(0,out,64)==-EAGAIN);files[0].f_flags=0;interrupt_wait=true;assert(read_file(0,out,64)==-ERESTARTSYS);interrupt_wait=false;close_file(0);remove_provider(&platform);clean();return 8;}
static unsigned queues(void){bind(&platform);open_file(0);u8 p[32],out[32];for(unsigned i=0;i<32;i++){packet(p,32,24,0x123);((struct gpr_hdr *)p)->token=i;assert(!model_rx(p));}for(unsigned i=0;i<32;i++){assert(read_file(0,out,32)==32&&((struct gpr_hdr *)out)->token==i);}assert(!audio_pkt_fops.poll(&files[0],NULL));close_file(0);remove_provider(&platform);clean();return 33;}
static unsigned backups(void){bind(&platform);open_file(0);u8 p[4096],out[4096];packet(p,4096,24,0x123);fail_atomic_skb=true;for(unsigned i=0;i<10;i++)assert(!model_rx(p));assert(model_rx(p)==-ENOMEM);fail_kernel_skb=true;unsigned before=skb_allocations;run_pending();assert(skb_allocations==before+1);fail_kernel_skb=false;assert(model_rx(p)==-ENOMEM);before=skb_allocations;run_pending();assert(skb_allocations==before+10);assert(!model_rx(p));for(unsigned i=0;i<11;i++)assert(read_file(0,out,sizeof(out))==4096&&!memcmp(p,out,sizeof(out)));assert(pending_work);close_file(0);remove_provider(&platform);clean();return 15;}
static unsigned sessions(void){bind(&platform);open_file(0);open_file(1);u8 p[32],out[32];packet(p,32,24,0x123);assert(!model_rx(p));close_file(0);assert(!close_alls&&!ion_crashes&&read_file(1,out,32)==32);close_file(1);assert(close_alls==1&&ion_crashes==1);open_file(0);assert(write_file(0,p,32)==32);close_file(0);assert(close_alls==2&&ion_crashes==2);remove_provider(&platform);clean();return 3;}
static ssize_t reader_result;
static void *reader_thread(void *unused){(void)unused;u8 out[64];reader_result=read_file(0,out,sizeof(out));return NULL;}
static void wait_until_reader(void){pthread_mutex_lock(&wait_gate);struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec+=5;while(!wait_entered)assert(!pthread_cond_timedwait(&wait_changed,&wait_gate,&until));pthread_mutex_unlock(&wait_gate);}
static unsigned withdrawal(void){bind(&platform);open_file(0);pthread_t t;assert(!pthread_create(&t,NULL,reader_thread,NULL));wait_until_reader();model_unbind();assert(!pthread_join(t,NULL)&&reader_result==-ENETRESET);assert((audio_pkt_fops.poll(&files[0],NULL)&(POLLERR|POLLHUP))==(POLLERR|POLLHUP));model_rebind();u8 p[32],out[32];packet(p,32,24,0x123);assert(!model_rx(p)&&read_file(0,out,32)==32);close_file(0);remove_provider(&platform);clean();return 3;}
static unsigned receive_wait(void){bind(&platform);open_file(0);pthread_t t;assert(!pthread_create(&t,NULL,reader_thread,NULL));wait_until_reader();u8 p[32];packet(p,32,24,0x123);assert(!model_rx(p));assert(!pthread_join(t,NULL)&&reader_result==32);close_file(0);remove_provider(&platform);clean();return 1;}
static unsigned old_file(void){bind(&platform);open_file(0);remove_provider(&platform);bind(&replacement);open_file(1);u8 p[32],out[32];packet(p,32,24,0x123);assert(!model_rx(p));unsigned before=close_alls;close_file(0);assert(close_alls==before&&read_file(1,out,32)==32);close_file(1);remove_provider(&replacement);clean();return 1;}
static unsigned open_remove(void){bind(&platform);open_file(0);remove_provider(&platform);u8 p[32],out[32];packet(p,32,24,0x123);assert(write_file(0,p,32)==-ENETRESET&&read_file(0,out,32)==-ENETRESET);assert(audio_pkt_fops.poll(&files[0],NULL)&POLLHUP);close_file(0);clean();return 2;}
static unsigned failure_paths(void){for(unsigned n=1;n<=4;n++){fail_stage=n;assert(audio_pkt_platform_driver_probe(&platform)==-ENOMEM);managed_cleanup(&platform.dev);assert(!regions&&!classes&&!cdevs&&!loggers&&!skb_live&&!live&&!pending_work&&platform.dev.refs==1);}fail_stage=6;assert(audio_pkt_platform_driver_probe(&platform)==-EIO);managed_cleanup(&platform.dev);fail_stage=0;clean();return 5;}
static unsigned logger(void){fail_logger=true;assert(audio_pkt_platform_driver_probe(&platform)==-ENOMEM);fail_logger=false;clean();return 1;}
static unsigned sync_callback(void){callback_on_register=require_logger=true;bind(&platform);open_file(0);u8 out[32];assert(read_file(0,out,32)==32&&!missing_logs);close_file(0);remove_provider(&platform);clean();return 1;}
#if !NATIVE
static unsigned publication(void){open_on_publish=true;fail_stage=5;assert(audio_pkt_platform_driver_probe(&platform)==-ENOMEM);u8 p[32];packet(p,32,24,0x123);assert(audio_pkt_fops.write(&published_file,p,32,NULL)==-ENETRESET&&!close_alls&&!ion_crashes);assert(!audio_pkt_fops.release(&published_inode,&published_file));open_on_publish=false;fail_stage=0;clean();return 2;}
static unsigned register_publication(void){open_on_publish=true;fail_stage=6;assert(audio_pkt_platform_driver_probe(&platform)==-EIO);assert(!audio_pkt_fops.release(&published_inode,&published_file)&&!close_alls&&!ion_crashes);open_on_publish=false;fail_stage=0;clean();return 1;}
static unsigned initial_faults(void){fail_alloc=0;assert(audio_pkt_platform_driver_probe(&platform)==-ENOMEM);fail_alloc=-1;clean();assert(audio_pkt_probe(&service)==-EPROBE_DEFER&&!service.dev.data);bind(&platform);open_file(0);u8 p[32];packet(p,32,24,0x123);model_unbind();assert(model_rx(p)==-ENODEV);assert(audio_pkt_remove(&service)==0);close_file(0);remove_provider(&platform);clean();return 4;}
static atomic_bool platform_removal_done;
static void *close_thread(void *unused){(void)unused;close_file(0);return NULL;}
static void *platform_remove_thread(void *unused){(void)unused;remove_provider(&platform);atomic_store(&platform_removal_done,true);return NULL;}
static unsigned cleanup_remove(void){bind(&platform);open_file(0);model_unbind();block_close=true;pthread_t closer,remover;assert(!pthread_create(&closer,NULL,close_thread,NULL));pthread_mutex_lock(&close_gate);while(!close_entered)pthread_cond_wait(&close_changed,&close_gate);pthread_mutex_unlock(&close_gate);assert(!pthread_create(&remover,NULL,platform_remove_thread,NULL));struct timespec pause={.tv_nsec=20000000};nanosleep(&pause,NULL);assert(!atomic_load(&platform_removal_done));pthread_mutex_lock(&close_gate);close_release=true;pthread_cond_broadcast(&close_changed);pthread_mutex_unlock(&close_gate);assert(!pthread_join(closer,NULL)&&!pthread_join(remover,NULL));clean();return 1;}
static unsigned singleton(void){bind(&platform);assert(audio_pkt_platform_driver_probe(&replacement)==-EBUSY&&replacement.dev.refs==1);open_file(0);struct audio_pkt_priv *p=files[0].private_data;p->opens=UINT_MAX;struct file extra={0};assert(audio_pkt_fops.open(&inodes[0],&extra)==-EMFILE&&!extra.private_data);p->opens=1;close_file(0);remove_provider(&platform);clean();return 2;}
#endif
static ssize_t writer_result;
static atomic_bool unbind_done;
static void *writer_thread(void *unused){(void)unused;u8 p[32];packet(p,32,24,0x123);writer_result=write_file(0,p,32);return NULL;}
static void *unbind_thread(void *unused){(void)unused;model_unbind();atomic_store(&unbind_done,true);return NULL;}
static unsigned send_unbind(void){bind(&platform);open_file(0);block_send=true;pthread_t writer,remover;assert(!pthread_create(&writer,NULL,writer_thread,NULL));pthread_mutex_lock(&send_gate);while(!send_entered)pthread_cond_wait(&send_changed,&send_gate);pthread_mutex_unlock(&send_gate);assert(!pthread_create(&remover,NULL,unbind_thread,NULL));struct timespec pause={.tv_nsec=20000000};nanosleep(&pause,NULL);assert(!atomic_load(&unbind_done));pthread_mutex_lock(&send_gate);send_release=true;pthread_cond_broadcast(&send_changed);pthread_mutex_unlock(&send_gate);assert(!pthread_join(writer,NULL)&&!pthread_join(remover,NULL)&&writer_result==32);close_file(0);remove_provider(&platform);clean();return 1;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;
#define CASE(name) if(!strcmp(argv[1],#name))n=name();else
CASE(tx_lengths) CASE(tx_headers) CASE(tx_faults) CASE(rx_headers) CASE(rx_basic) CASE(regions_multiple) CASE(regions_bounds) CASE(regions_invalid) CASE(reads) CASE(queues) CASE(backups) CASE(sessions) CASE(withdrawal) CASE(receive_wait) CASE(old_file) CASE(open_remove) CASE(failure_paths) CASE(logger) CASE(sync_callback) CASE(send_unbind)
#if !NATIVE
CASE(publication) CASE(register_publication) CASE(singleton) CASE(initial_faults) CASE(cleanup_remove)
#endif
abort();printf("{\"pkt_%s_cases\":%u}\n",argv[1],n);}
