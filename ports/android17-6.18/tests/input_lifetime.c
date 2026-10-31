#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define READ_ONCE(p) (p)
#define WRITE_ONCE(p,v) ((p)=(v))
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define THIS_MODULE ((void *)1)
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define MAJOR(n) ((n)>>8)
#define MKDEV(n,m) (((n)<<8)|(m))
#define FP_MINOR_COUNT 2
#define MINOR_NUM_MIFP 0
#define MINOR_NUM_MIFP_ID 1
#define HZ 100
#define FP_CLASS_NAME "xiaomi_fp"
#define FP_DEV_NAME "xiaomi-fp"
#define FP_ID_DEV_NAME "mifp_id"
#define dev_err(...) do {} while(0)
struct kobject { int refs; };
struct cdev { struct kobject kobj; void *owner; const void *ops; bool added; };
struct device { struct kobject kobj; };
struct platform_device { struct device dev; };
struct class { int unused; };
struct list_head { int listed; };
struct workqueue_struct { int unused; };
struct delayed_work { int initialized; };
struct input_dev { int unused; };
struct fp_vreg { bool enable; };
struct fp_device {
 bool input_registered,region_registered,cdev_registered,id_cdev_registered,sysfs_registered,listed;
 struct device *id_device,*device; struct class *class; struct cdev *cdev,*fpid_cdev; struct platform_device *driver_device;
 unsigned devt; struct list_head device_entry; int fp_wait_queue,irq_gpio,irq_num;bool irq_request,irq_enabled;
 struct workqueue_struct *screen_state_wq; struct delayed_work screen_state_dw; struct input_dev *input;
 struct fp_vreg vreg_gpio,vreg_1v8,vreg_3v3;int fp_netlink_enabled,device_available;
};
struct inode { unsigned i_rdev; struct cdev *i_cdev; };
struct file { void *private_data; };
static unsigned iminor(struct inode *i) { return i->i_rdev & 255; }
static struct fp_device fp;
static bool fp_present;
static unsigned fp_users,fp_main_users;
static pthread_mutex_t fp_io_lock=PTHREAD_MUTEX_INITIALIZER,device_list_lock=PTHREAD_MUTEX_INITIALIZER;
static struct list_head device_list;
static void *fp_wakesrc,*cookie,*prim_panel;
static int attribute_group,fp_fops;
static int fail_step,steps,live,net_live,free_irqs,cancelled,work,fail_open;
static bool failure(void) { return ++steps==fail_step; }
static void *allocate(size_t n) { if(failure())return NULL;void *p=calloc(1,n);assert(p);live++;return p; }
static void drop(void *p) { if(p)live--;free(p); }
static int nonseekable_open(struct inode *i,struct file *f) { (void)i;(void)f;return fail_open?-EIO:0; }
static long fp_ioctl_locked(struct file *f,unsigned cmd,unsigned long arg) { assert(f->private_data==&fp);return cmd+arg; }
static void free_irq(int n,struct fp_device *p) { assert(n==17 && p==&fp);free_irqs++; }
static void queue_delayed_work(struct workqueue_struct *w,struct delayed_work *d,int t) { assert(w && d->initialized && t==500);work++; }
static void cancel_delayed_work_sync(struct delayed_work *d) { assert(d->initialized);cancelled++;work=0; }
static void destroy_workqueue(struct workqueue_struct *w) { drop(w); }
static void input_unregister_device(struct input_dev *p) { drop(p); }
static void input_free_device(struct input_dev *p) { drop(p); }
static void sysfs_remove_group(struct kobject *p,int *g) { (void)p;(void)g; }
static void kobject_put(struct kobject *k) { assert(k->refs>0);if(!--k->refs)drop(k); }
static void cdev_del(struct cdev *c) { assert(c->added);c->added=false;kobject_put(&c->kobj); }
static void panel_event_notifier_unregister(void *c) { (void)c; }
static int fp_netlink_destroy(struct fp_device *p) { (void)p;net_live=0;return 0; }
static void wakeup_source_unregister(void *p) { drop(p); }
static void list_del_init(struct list_head *p) { p->listed=0; }
static void device_destroy(struct class *c,unsigned devt) { assert(c);drop((devt&255)?fp.id_device:fp.device); }
static void unregister_chrdev_region(unsigned d,int n) { (void)d;assert(n==2); }
static void class_destroy(struct class *p) { drop(p); }
static int fp_power_off(struct fp_vreg *r) { r->enable=false;return 0; }
#define INIT_LIST_HEAD(p) ((p)->listed=0)
#define init_waitqueue_head(p) (*(p)=1)
static void fp_power_config(struct fp_device *p) { (void)p; }
static int fp_parse_dts(struct fp_device *p) { p->irq_num=17;return failure()?-EIO:0; }
#define INIT_DELAYED_WORK(p,f) ((p)->initialized=1)
static struct workqueue_struct *alloc_ordered_workqueue(const char *n,int flags) { (void)n;(void)flags;return allocate(sizeof(struct workqueue_struct)); }
static int fp_create_input_device(void) { fp.input=allocate(sizeof(struct input_dev));if(!fp.input)return -ENOMEM;fp.input_registered=true;return 0; }
static void *wakeup_source_register(struct device *d,const char *n) { (void)d;(void)n;return allocate(1); }
static int fp_netlink_init(struct fp_device *p) { (void)p;if(failure())return -ENOMEM;net_live=1;return 0; }
static struct class *class_create(const char *n) { (void)n;void *p=allocate(sizeof(struct class));return p?p:ERR_PTR(-ENOMEM); }
static int alloc_chrdev_region(unsigned *dev,int base,int count,const char *n) { (void)n;assert(base==0 && count==2);if(failure())return -EIO;*dev=MKDEV(20,0);return 0; }
static struct cdev *cdev_alloc(void) { struct cdev *p=allocate(sizeof(*p));if(p)p->kobj.refs=1;return p; }
static int cdev_add(struct cdev *p,unsigned dev,int n) { (void)dev;assert(n==1);if(failure())return -EIO;p->added=true;return 0; }
static struct device *device_create(struct class *c,struct device *d,unsigned n,struct fp_device *fp,const char *name) { (void)c;(void)d;(void)n;(void)fp;(void)name;void *p=allocate(sizeof(struct device));return p?p:ERR_PTR(-ENOMEM); }
static void list_add(struct list_head *p,struct list_head *h) { (void)h;p->listed=1; }
static int sysfs_create_group(struct kobject *k,int *g) { (void)k;(void)g;return failure()?-EIO:0; }
/* FUNCTIONS */
static struct inode node(unsigned minor) { return (struct inode){.i_rdev=fp.devt+minor,.i_cdev=minor?fp.fpid_cdev:fp.cdev}; }
int main(void) {
 struct platform_device dev={0};int faults=0;
 for(int f=1;f<=14;f++) { fail_step=f;steps=0;int ret=fp_probe(&dev);assert(ret<0);assert(!fp_present && !live && !net_live && !fp.driver_device);faults++; }
 fail_step=0;steps=0;assert(!fp_probe(&dev));assert(steps==14);assert(fp_probe(&dev)==-EBUSY);
 struct inode main=node(0),id=node(1);struct file a={0},b={0},c={0};
 fail_open=1;assert(fp_open(&main,&a)==-EIO && !fp_users);fail_open=0;
 assert(!fp_open(&main,&a));assert(!fp_open(&main,&b));assert(!fp_open(&id,&c));assert(fp_users==3 && fp_main_users==2);
 assert(fp_ioctl(&a,4,5)==9);fp.irq_request=true;fp.irq_enabled=true;
 assert(!fp_release(&main,&a) && !free_irqs);assert(!fp_release(&main,&b) && free_irqs==1 && fp_users==1 && !fp_main_users);
 main.i_cdev->kobj.refs++; /* Simulate chrdev_open retaining cdev before entering our open. */
 fp_remove(&dev);assert(!fp_present && live==1 && !net_live);assert(fp_ioctl(&c,4,5)==-ENODEV);assert(fp_probe(&dev)==-EBUSY);
 assert(!fp_release(&id,&c) && !fp_users);assert(!fp_probe(&dev));
 struct file stale={0};assert(fp_open(&main,&stale)==-ENODEV);assert(!fp_users);
 kobject_put(&main.i_cdev->kobj);main=node(0);assert(!fp_open(&main,&a));
 fp.irq_request=true;fp.irq_enabled=true;fp_remove(&dev);assert(!live && free_irqs==2);assert(!fp_release(&main,&a) && free_irqs==2);
 assert(!fp_probe(&dev));fp_remove(&dev);assert(!live && !fp_users && !net_live);
 printf("{\"probe_failure_cases\":%d,\"last_close_irq\":true,\"stale_open_rebind\":true,\"removed_ioctl\":true,\"old_files_prevent_reuse\":true}\n",faults);
}
