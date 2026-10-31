#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t uint8_t, u8;
typedef uint32_t u32;
typedef unsigned dev_t_mock;
typedef int wait_queue_head_t;
typedef int fl_owner_t;
struct mutex { pthread_mutex_t raw; };
struct kref { unsigned refs; };
struct kobject { unsigned refs; };
struct class { int unused; };
struct device { void *of_node; bool wakeup; };
struct i2c_client { struct device dev; void *adapter; const char *name; int irq; void *data; };
struct cdev { struct kobject kobj; void *owner; const void *ops; };
struct inode { struct cdev *i_cdev; };
struct file { void *private_data; };
struct file_operations { int unused; };
struct platform_gpio { int irq, ven, dwl_req, clkreq; };
struct nfc_dev {
 bool removing, published, release_read, region_registered, cdev_added, irq_requested;
 bool clkreq_wake_enabled, post_init_done, is_vreg_enabled;
 unsigned gpio_owned, dev_ref_count, devno;
 u32 clkreq_hwirq;
 uint8_t *read_kbuf, *write_kbuf;
 struct mutex read_mutex, write_mutex, dev_ref_mutex;
 struct kref ref;
 wait_queue_head_t read_wq;
 struct { struct mutex lock; wait_queue_head_t read_wq; int last_src_ese_prot; } cold_reset;
 struct { struct i2c_client *client; bool irq_wake_up; struct mutex irq_enabled_lock; } i2c_dev;
 struct { struct platform_gpio gpio; } configs;
 int interface, nfc_state;
 bool secure_zone;
 struct class *nfc_class;
 struct device *nfc_device;
 struct cdev *c_dev;
 void *reg, *ipcl;
 int (*nfc_read)(struct nfc_dev *, char *, size_t, int);
 int (*nfc_write)(struct nfc_dev *, const char *, size_t, int);
 int (*nfc_enable_intr)(struct nfc_dev *);
 int (*nfc_disable_intr)(struct nfc_dev *);
};
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define DEFINE_MUTEX(n) struct mutex n={.raw=PTHREAD_MUTEX_INITIALIZER}
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
#define guard(kind) (void)
#define BIT(n) (1U<<(n))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define GFP_KERNEL 0
#define GFP_DMA 0
#define THIS_MODULE NULL
#define DEV_COUNT 1
#define PLATFORM_IF_I2C 0
#define NFC_STATE_UNKNOWN 0
#define NFC_STATE_NCI 2
#define MAX_NCI_BUFFER_SIZE 258
#define MAX_DL_BUFFER_SIZE 4100
#define NUM_OF_IPC_LOG_PAGES 2
#define NFC_CHAR_DEV_NAME "nfc"
#define CLASS_NAME "nfc"
#define ESE_COLD_RESET_ORIGIN_NONE 255
#define I2C_FUNC_I2C 1
#define IRQF_TRIGGER_HIGH 1
#define IRQF_NO_AUTOEN 2
#define GPIO_OUTPUT 1
#define GPIO_HIGH 2
#define GPIO_IRQ 4
#define DTS_CLKREQ_GPIO_STR "clkreq"
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
static int step, fail_step, live, irq_live, wakeups, srcu_syncs, gpio_live[256];
static bool functionality=true, fail_open, fail_enable;
static int nfc_io_srcu;
static struct file_operations nfc_i2c_dev_fops;
static DEFINE_MUTEX(nfc_registry_lock);
static struct nfc_dev *nfc_active;
static bool fault(void) { return ++step==fail_step; }
static void *allocate(size_t n) { if(fault())return NULL;void *p=calloc(1,n);assert(p);live++;return p; }
static void *kzalloc(size_t n,int f) { (void)f;return allocate(n); }
static void kfree(void *p) { if(p){live--;free(p);} }
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->raw,NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->raw)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->raw)); }
static bool mutex_trylock(struct mutex *m) { return !pthread_mutex_trylock(&m->raw); }
#define spin_lock_init(p) mutex_init(p)
#define init_waitqueue_head(p) (*(p)=1)
static void wake_up_interruptible(wait_queue_head_t *p) { assert(*p);wakeups++; }
#define wake_up(p) wake_up_interruptible(p)
static void synchronize_srcu(int *p) { assert(p==&nfc_io_srcu);srcu_syncs++; }
static void kref_init(struct kref *r) { r->refs=1; }
static void kref_get(struct kref *r) { assert(r->refs);r->refs++; }
static void kref_put(struct kref *r,void(*release)(struct kref *)) { assert(r->refs);if(!--r->refs)release(r); }
static void kobject_put(struct kobject *p) { assert(p->refs);if(!--p->refs)kfree(container_of(p,struct cdev,kobj)); }
static bool gpio_is_valid(int n) { return n>=0 && n<256; }
static int gpio_request(int n,const char *s) { (void)s;assert(!gpio_live[n]);if(fault())return -EIO;gpio_live[n]=1;return 0; }
static void gpio_free(int n) { assert(gpio_live[n]);gpio_live[n]=0; }
static int gpio_direction_output(int n,int v) { (void)v;assert(gpio_live[n]);return fault()?-EIO:0; }
static int gpio_direction_input(int n) { assert(gpio_live[n]);return fault()?-EIO:0; }
static int gpio_to_irq(int n) { assert(gpio_live[n]);return fault()?-EIO:17; }
static int gpio_get_value(int n) { (void)n;return 0; }
static void set_valid_gpio(int n,int v) { (void)v;if(gpio_is_valid(n))assert(gpio_live[n]); }
static int msm_gpio_mpm_wake_set(u32 n,bool on) { assert(n==115);return on && fault()?-EIO:0; }
static int of_property_read_u32_index(void *np,const char *s,int index,u32 *v) { (void)np;(void)s;assert(index==1);if(fault())return -EIO;*v=115;return 0; }
static int alloc_chrdev_region(unsigned *dev,int base,int count,char *n) { (void)n;assert(!base&&count==1);if(fault())return -EIO;*dev=20;return 0; }
static void unregister_chrdev_region(unsigned dev,int count) { assert(dev==20&&count==1); }
static struct class *class_create(const char *n) { (void)n;void *p=allocate(sizeof(struct class));return p?p:ERR_PTR(-ENOMEM); }
static void class_destroy(struct class *p) { kfree(p); }
static struct cdev *cdev_alloc(void) { struct cdev *p=allocate(sizeof(*p));if(p)p->kobj.refs=1;return p; }
static int cdev_add(struct cdev *p,unsigned dev,int count) { assert(p&&dev==20&&count==1);return fault()?-EIO:0; }
static void cdev_del(struct cdev *p) { kobject_put(&p->kobj); }
static struct device *last_device;
static struct device *device_create(struct class *cl,struct device *parent,unsigned dev,void *data,char *n) { (void)data;(void)n;assert(cl&&parent&&dev==20);void *p=allocate(sizeof(struct device));if(p)last_device=p;return p?p:ERR_PTR(-ENOMEM); }
static void device_destroy(struct class *cl,unsigned dev) { assert(cl&&dev==20);kfree(last_device);last_device=NULL; }
static int device_init_wakeup(struct device *d,bool on) { if(on&&fault())return -EIO;d->wakeup=on;return 0; }
static bool i2c_check_functionality(void *a,int type) { (void)a;assert(type==1);return functionality; }
static void i2c_set_clientdata(struct i2c_client *c,void *p) { c->data=p; }
static struct nfc_dev *i2c_get_clientdata(struct i2c_client *c) { return c->data; }
static int nfc_parse_dt(struct device *d,void *c,int type) { (void)d;assert(type==0);struct platform_gpio *p=c;*p=(struct platform_gpio){55,114,116,115};return fault()?-EIO:0; }
static int i2c_read(struct nfc_dev *d,char *b,size_t n,int t) { (void)d;(void)b;(void)n;(void)t;return 0; }
static int i2c_write(struct nfc_dev *d,const char *b,size_t n,int t) { (void)d;(void)b;(void)t;return n; }
static int i2c_enable_irq(struct nfc_dev *d) { return fail_enable||d->removing?-ENODEV:0; }
static int i2c_disable_irq(struct nfc_dev *d) { (void)d;return 0; }
static void *i2c_irq_handler;
static int request_irq(int irq,void *handler,int flags,const char *name,struct nfc_dev *d) { (void)handler;(void)name;(void)d;assert(irq==17&&flags==3);if(fault())return -EIO;assert(!irq_live);irq_live=1;return 0; }
static void free_irq(int irq,struct nfc_dev *d) { (void)d;assert(irq==17&&irq_live);irq_live=0; }
static int disable_irq_wake(int irq) { assert(irq==17);return 0; }
static void *ipc_log_context_create(int n,const char *name,int flags) { (void)name;assert(n==2&&!flags);return allocate(1); }
static void ipc_log_context_destroy(void *p) { kfree(p); }
static int nfc_ldo_config(struct device *d,struct nfc_dev *n) { (void)d;n->reg=allocate(1);if(!n->reg)return -ENOMEM;n->is_vreg_enabled=true;return 0; }
static int nfc_ldo_unvote(struct nfc_dev *d) { assert(d->is_vreg_enabled);d->is_vreg_enabled=false;return 0; }
static void regulator_put(void *r) { kfree(r); }
static int nfcc_hw_check(struct nfc_dev *d) { if(fault())return -EIO;d->nfc_state=NFC_STATE_NCI;return 0; }
static int nonseekable_open(struct inode *i,struct file *f) { (void)i;(void)f;return fail_open?-EIO:0; }
/* FUNCTIONS */
static void clean(void) { assert(!live&&!irq_live&&!nfc_active);for(unsigned i=0;i<256;i++)assert(!gpio_live[i]); }
int main(void) {
 struct i2c_client c={.name="nfc"};unsigned faults=0;
 functionality=false;assert(nfc_i2c_dev_probe(&c)==-ENODEV);clean();functionality=true;
 fail_step=0;step=0;assert(!nfc_i2c_dev_probe(&c));int steps=step;
 nfc_i2c_dev_remove(&c);assert(srcu_syncs==1);clean();
 for(int f=1;f<=steps;f++) { step=0;fail_step=f;int sync=srcu_syncs;int ret=nfc_i2c_dev_probe(&c);if(!ret)nfc_i2c_dev_remove(&c);else assert(sync==srcu_syncs);assert(!c.data);clean();faults++; }
 fail_step=0;assert(!nfc_i2c_dev_probe(&c));struct nfc_dev *d=c.data;
 struct inode inode={.i_cdev=d->c_dev};struct file a={0},b={0},old={0};
 fail_open=true;assert(nfc_dev_open(&inode,&a)==-EIO&&d->ref.refs==1);fail_open=false;
 fail_enable=true;assert(nfc_dev_open(&inode,&a)==-ENODEV&&!d->dev_ref_count);fail_enable=false;
 assert(!nfc_dev_open(&inode,&a)&&!nfc_dev_open(&inode,&b));assert(d->ref.refs==3&&d->dev_ref_count==2);
 inode.i_cdev->kobj.refs++; /* Simulate chrdev_open retaining old cdev before open. */
 nfc_i2c_dev_remove(&c);assert(d->removing&&d->ref.refs==2&&d->dev_ref_count==2);
 assert(nfc_dev_flush(&a,0)==-ENODEV);assert(nfc_dev_open(&inode,&old)==-ENODEV);
 assert(!nfc_dev_close(&inode,&a));assert(d->ref.refs==1&&d->dev_ref_count==1);
 assert(!nfc_dev_close(&inode,&b));assert(live==1); /* Only retained old cdev survives. */
 assert(!nfc_i2c_dev_probe(&c));assert(nfc_dev_open(&inode,&old)==-ENODEV);
 kobject_put(&inode.i_cdev->kobj);d=c.data;inode.i_cdev=d->c_dev;
 assert(!nfc_dev_open(&inode,&a));assert(!nfc_dev_close(&inode,&a));
 nfc_i2c_dev_remove(&c);clean();
 printf("{\"probe_failure_points\":%u,\"open_failure_paths\":2,\"retained_fd_and_stale_cdev\":true,\"failed_probe_avoids_global_srcu_wait\":true}\n",faults);
}
