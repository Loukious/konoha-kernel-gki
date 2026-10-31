#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#define __iomem
#define BIT(n) (1U << (n))
#define GFP_KERNEL 0
#define THIS_MODULE ((void *)1)
#define CRYPTO_ALG_TYPE_RNG 1
#define IORESOURCE_MEM 1
#define EPROBE_DEFER 517
#define ERESTARTSYS 512
#define QRNG_IOC_MAGIC 0x100
#define QRNG_IOCTL_RESET_BUS_BANDWIDTH _IO(QRNG_IOC_MAGIC, 1)
#define min_t(type,a,b) ((type)(a)<(type)(b)?(type)(a):(type)(b))
#define container_of(p,type,member) ((type *)((char *)(p)-offsetof(type,member)))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define MKDEV(a,b) (((a)<<8)|(b))
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define dev_err(...) ((void)0)
#define mb() ((void)0)
typedef uint8_t u8;
typedef uint32_t u32;
typedef unsigned long kernel_ulong_t;
typedef atomic_int atomic_t;
#define atomic_set(p,v) atomic_store(p,v)
#define atomic_xchg(p,v) atomic_exchange(p,v)
struct mutex { pthread_mutex_t p; };
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static _Thread_local int locks;
static int interrupted;
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->p,NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->p)); ++locks; }
static void mutex_unlock(struct mutex *m) { --locks; assert(!pthread_mutex_unlock(&m->p)); }
static int mutex_lock_interruptible(struct mutex *m) { if(interrupted)return -1; mutex_lock(m);return 0; }
#define current NULL
static int signal_pending(void *p) { return interrupted; }
struct kref { atomic_int refs; };
static void kref_init(struct kref *r) { atomic_store(&r->refs,1); }
static void kref_get(struct kref *r) { assert(atomic_fetch_add(&r->refs,1)>0); }
static void kref_put(struct kref *r,void (*release)(struct kref *)) { int n=atomic_fetch_sub(&r->refs,1);assert(n>0);if(n==1)release(r); }
static int fail_alloc,allocations,frees;
static void *kzalloc(size_t n,int flags) { if(fail_alloc)return NULL; ++allocations;return calloc(1,n); }
static void kfree(void *p) { ++frees;free(p); }
#define kfree_sensitive kfree
struct resource { uintptr_t start;size_t size; };
struct device { void *data,*of_node;struct device *parent;void (*release)(struct device *);struct class *class;unsigned devt;atomic_int refs;void *match; };
struct platform_device { struct device dev;struct resource res;void *map;struct clk *clock; };
struct clk { int enabled;bool alive; };
struct icc_path { int bw;bool alive; };
struct hwrng { const char *name;int (*read)(struct hwrng *,void *,size_t,bool);int quality;bool registered; };
struct crypto_alg;
struct crypto_tfm { struct crypto_alg *__crt_alg;void *ctx; };
struct crypto_rng { void *ctx; };
struct crypto_alg { const char *cra_name,*cra_driver_name;int cra_flags,cra_priority,cra_ctxsize;void *cra_module;int (*cra_init)(struct crypto_tfm *);void (*cra_destroy)(struct crypto_alg *);int refs;bool registered; };
struct rng_alg { int (*generate)(struct crypto_rng *,const u8 *,unsigned,u8 *,unsigned);int (*seed)(struct crypto_rng *,const u8 *,unsigned);int seedsize;struct crypto_alg base; };
#define crypto_tfm_ctx(t) ((t)->ctx)
#define crypto_rng_ctx(t) ((t)->ctx)
static void platform_set_drvdata(struct platform_device *p,void *d) { p->dev.data=d; }
static void *platform_get_drvdata(struct platform_device *p) { return p->dev.data; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static void *device_get_match_data(struct device *d) { return d->match; }
static void device_initialize(struct device *d) { atomic_store(&d->refs,1); }
static struct device *get_device(struct device *d) { assert(atomic_fetch_add(&d->refs,1)>0);return d; }
static void put_device(struct device *d) { int n=atomic_fetch_sub(&d->refs,1);assert(n>0);if(n==1)d->release(d); }
static int fail_res,fail_map,fail_clk,fail_icc,fail_hwrng,fail_chrdev,fail_class,fail_name,fail_cdev,fail_crypto;
static int map_count,clk_count,icc_count,hwrng_count,chrdev_count,class_count,cdev_count,alg_count;
static int no_clock,skip_config,clock_error,high_error,low_error,delays,reads,writes,clock_enables,clock_disables,high_votes,low_votes;
static int status_seq[512],status_len,status_pos,default_status=1;
static u32 word=0x12345678;
static pthread_mutex_t block_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t block_cond=PTHREAD_COND_INITIALIZER;
static bool block_read,read_entered,read_continue;
static void *active_map;
static struct clk *active_clk;
static struct resource *platform_get_resource(struct platform_device *p,int t,int n) { return fail_res?NULL:&p->res; }
static size_t resource_size(struct resource *r) { return r->size; }
static void *ioremap(uintptr_t addr,size_t size) { if(fail_map)return NULL;++map_count;active_map=calloc(1,4096);return active_map; }
static void iounmap(void *p) { assert(map_count>0);--map_count;free(p);if(active_map==p)active_map=NULL; }
static void *devm_platform_ioremap_resource(struct platform_device *p,int n) { p->map=ioremap(0,4096);return p->map?p->map:ERR_PTR(-ENOMEM); }
static struct clk *clk_get(struct device *d,const char *name) { if(fail_clk)return ERR_PTR(-EPROBE_DEFER);struct clk *c=calloc(1,sizeof(*c));c->alive=true;active_clk=c;++clk_count;return c; }
static struct clk *devm_clk_get_optional(struct device *d,const char *name) { struct platform_device *p=container_of(d,struct platform_device,dev);p->clock=no_clock?NULL:clk_get(d,name);return p->clock; }
static void clk_put(struct clk *c) { if(!c)return;assert(c->alive&&!c->enabled);c->alive=false;--clk_count;free(c);if(active_clk==c)active_clk=NULL; }
static int clk_prepare_enable(struct clk *c) { if(!c)return 0;assert(c->alive&&locks>0);++clock_enables;if(clock_error)return clock_error;++c->enabled;return 0; }
static void clk_disable_unprepare(struct clk *c) { if(!c)return;assert(c->alive&&c->enabled>0&&locks>0);--c->enabled;++clock_disables; }
static struct icc_path *of_icc_get(struct device *d,const char *name) { if(fail_icc)return ERR_PTR(-EPROBE_DEFER);struct icc_path *p=calloc(1,sizeof(*p));p->alive=true;++icc_count;return p; }
static int icc_set_bw(struct icc_path *p,int avg,int peak) { if(!p)return 0;assert(p->alive&&locks>0);if(peak){++high_votes;if(high_error)return high_error;}else{++low_votes;if(low_error)return low_error;}p->bw=peak;return 0; }
static void icc_put(struct icc_path *p) { if(!p)return;assert(p->alive);p->alive=false;--icc_count;free(p); }
static bool of_property_read_bool(void *node,const char *name) { if(!node)return false;return !strcmp(name,"qcom,no-clock-support")?no_clock:skip_config; }
static u32 readl_relaxed(void *p) { assert(active_map&&locks>0);if(active_clk)assert(active_clk->alive&&active_clk->enabled>0);size_t off=(char *)p-(char *)active_map;assert(off<4096);++reads;
 if(off==4){pthread_mutex_lock(&block_mutex);if(block_read){read_entered=true;pthread_cond_broadcast(&block_cond);while(!read_continue)pthread_cond_wait(&block_cond,&block_mutex);}pthread_mutex_unlock(&block_mutex);return status_pos<status_len?status_seq[status_pos++]:default_status;}
 if(off==0)return word;
 return *(u32 *)p;
}
#define readl readl_relaxed
static void writel_relaxed(u32 v,void *p) { assert(active_map&&locks>0);if(active_clk)assert(active_clk->enabled);++writes;memcpy(p,&v,4); }
#define writel writel_relaxed
#define readl_poll_timeout(p,val,cond,delay,max) ({ int rc=-ETIMEDOUT; for(int i=0;i<=50;++i){val=readl(p);if(cond){rc=0;break;}++delays;}rc; })
static void udelay(int us) { ++delays; }
static int hwrng_register(struct hwrng *r) { if(fail_hwrng)return -EIO;if(hwrng_count)return -EEXIST;r->registered=true;++hwrng_count;return 0; }
static void hwrng_unregister(struct hwrng *r) { assert(r->registered);r->registered=false;--hwrng_count;u8 b[4];assert(r->read(r,b,4,false)==-ENODEV); }
static int crypto_register_rng(struct rng_alg *a) { if(fail_crypto==1)return -EINVAL;if(fail_crypto==2){a->base.cra_destroy(&a->base);return -EEXIST;}if(alg_count){a->base.cra_destroy(&a->base);return -EEXIST;}a->base.refs=1;a->base.registered=true;++alg_count;return 0; }
static void crypto_unregister_rng(struct rng_alg *a) { assert(a->base.registered);a->base.registered=false;--alg_count;if(!--a->base.refs)a->base.cra_destroy(&a->base); }
static void devres_release(struct platform_device *p) { if(p->clock&&!IS_ERR(p->clock))clk_put(p->clock);if(p->map)iounmap(p->map);p->map=NULL;p->clock=NULL; }
struct file { void *private_data; };
struct inode { struct cdev *i_cdev; };
struct file_operations { void *owner;int (*open)(struct inode *,struct file *);int (*release)(struct inode *,struct file *);long (*unlocked_ioctl)(struct file *,unsigned,unsigned long); };
struct cdev { void *owner;const struct file_operations *ops;struct device *parent;int inode_refs;bool live; };
struct class { int unused; };
static struct class *class_create(const char *n) { if(fail_class)return ERR_PTR(-ENOMEM);++class_count;return calloc(1,sizeof(struct class)); }
static void class_destroy(struct class *c) { --class_count;free(c); }
static int register_chrdev_region(unsigned d,int n,const char *name) { assert(d==MKDEV(256,0));if(fail_chrdev)return -EBUSY;++chrdev_count;return 0; }
static void unregister_chrdev_region(unsigned d,int n) { assert(chrdev_count>0);--chrdev_count; }
static int dev_set_name(struct device *d,const char *s) { return fail_name?-ENOMEM:0; }
static void cdev_init(struct cdev *c,const struct file_operations *f) { c->ops=f; }
static int cdev_device_add(struct cdev *c,struct device *d) { if(fail_cdev)return -EIO;assert(c->owner==THIS_MODULE&&c->ops->owner==THIS_MODULE);c->parent=get_device(d);c->live=true;++cdev_count;return 0; }
static void cdev_device_del(struct cdev *c,struct device *d) { assert(c->live);c->live=false;--cdev_count;if(!c->inode_refs)put_device(c->parent); }
static void inode_get(struct inode *i,struct cdev *c) { i->i_cdev=c;++c->inode_refs; }
static void inode_put(struct inode *i) { struct cdev *c=i->i_cdev;assert(c->inode_refs>0);if(!--c->inode_refs&&!c->live)put_device(c->parent); }
static void reset_io(void) { default_status=1;word=0x12345678;status_len=status_pos=0;clock_error=high_error=low_error=0;delays=reads=writes=clock_enables=clock_disables=high_votes=low_votes=0; }
static void clean(void) { assert(!map_count&&!clk_count&&!icc_count&&!hwrng_count&&!chrdev_count&&!class_count&&!cdev_count&&!alg_count);assert(allocations==frees);assert(!locks); }
