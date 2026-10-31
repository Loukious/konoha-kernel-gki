#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define ENOTCONN 107
#define ERESTARTSYS 512
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define MAX_INSTANCES 4
#define CCID_CTRL_DEV_NAME "ccid_ctrl"
#define CCID_BULK_DEV_NAME "ccid_bulk"
#define CCID_NOTIFY_CARD 0x40044301
#define CCID_NOTIFY_HWERROR 0x40044302
#define CCID_READ_DTR 0x80044303
#define CCID_NOTIFY_MAXPACKET 4
#define CTRL_BUF_SIZE 4
#define CCIDGENERICREQ_ABORT 1
#define CCIDGENERICREQ_GET_CLOCK_FREQUENCIES 2
#define CCIDGENERICREQ_GET_DATA_RATES 3
#define USB_DIR_IN 128
#define USB_DIR_OUT 0
#define USB_TYPE_CLASS 32
#define USB_RECIP_INTERFACE 1
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define THIS_MODULE ((void *)1)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define MKDEV(ma,mi) (((ma)<<8)|(mi))
#define MINOR(x) ((x)&255)
#define MAJOR(x) ((x)>>8)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define cpu_to_le32(x) (x)
#define le16_to_cpu(x) (x)
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define spin_lock_irqsave(p,f) ((void)(p),(f)=0)
#define spin_unlock_irqrestore(p,f) ((void)(p),(void)(f))
#define mutex_lock_interruptible(p) ((void)(p),0)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define mutex_init(p) ((void)(p))
#define spin_lock_init(p) ((void)(p))
#define init_waitqueue_head(p) ((void)(p))
#define wake_up(p) ((p)->wakes++)
#define __user
#define INIT_LIST_HEAD(p) ((p)->next=(p))
typedef int gfp_t;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
struct list_head {struct list_head *next;};
struct kref {int refs;};
struct class {int unused;};
struct f_ccid;
struct device {int refs;struct class *class;unsigned devt;void *data;void (*release)(struct device *);};
struct file_operations {int unused;};
struct cdev {void *owner;struct device *parent;bool published;};
struct inode {struct cdev *i_cdev;};
struct file {void *private_data;};
struct atomic_t {int value;};
struct wait_queue {int wakes;};
struct usb_request {void *buf,*context;unsigned length,actual;int status;};
struct usb_ep {const char *name;void *desc,*driver_data;bool enabled;};
struct usb_gadget {int dev;struct usb_ep *ep0;};
struct usb_composite_dev {struct usb_gadget *gadget;struct usb_request *req;};
struct usb_configuration {struct usb_composite_dev *cdev;};
struct usb_function {struct usb_configuration *config;};
struct usb_ctrlrequest {uint8_t bRequestType,bRequest;u16 wIndex,wValue,wLength;};
struct ccid_ctrl_dev {struct cdev cdev;struct device dev;struct atomic_t opened;struct list_head tx_q;struct wait_queue tx_wait_q;u8 buf[4];int tx_ctrl_done;};
struct ccid_bulk_dev {struct cdev cdev;struct device dev;struct atomic_t opened,error;struct list_head tx_idle;struct wait_queue read_wq,write_wq;struct usb_request *rx_req;};
struct f_ccid {struct usb_function function;struct kref ref;int read_lock,write_lock,ctrl_read_lock,ioctl_lock,lock,ifc_id,dtr_state;struct atomic_t online,notify_busy;struct usb_ep *notify,*in,*out;struct usb_request *notify_req;struct ccid_ctrl_dev ctrl_dev;struct ccid_bulk_dev bulk_dev;};
struct usb_ccid_notification {u8 buf[4];};
static struct {u32 dwDefaultClock,dwDataRate;} ccid_class_desc={3580,9600};
static int major,ccid_ida,ccid_ida_lock,live,classes,regions,step,fail_step,free_reqs,alloc_fail,alloc_step,queue_fault,queued,copy_fault,enable_step,enable_fail;
static struct class *ccid_class;
static const struct file_operations ccid_ctrl_fops={0},ccid_bulk_fops={0};
static int failure(void) {return ++step==fail_step;}
static void *kzalloc(size_t n,int flags) {(void)flags;if(failure())return NULL;void *p=calloc(1,n);if(p)live++;return p;}
static void kfree(void *p) {if(p){assert(live>0);live--;free(p);}}
static void kref_init(struct kref *r) {r->refs=1;}
static void kref_get(struct kref *r) {assert(r->refs>0);r->refs++;}
static void kref_put(struct kref *r,void(*f)(struct kref*)) {assert(r->refs>0);if(!--r->refs)f(r);}
static void device_initialize(struct device *d) {d->refs=1;}
static void get_device(struct device *d) {assert(d->refs>0);d->refs++;}
static void put_device(struct device *d) {assert(d->refs>0);if(!--d->refs)d->release(d);}
static void dev_set_drvdata(struct device *d,void *p) {d->data=p;}
static void *dev_get_drvdata(struct device *d) {return d->data;}
static int dev_set_name(struct device *d,const char *fmt,const char *name) {(void)d;(void)fmt;(void)name;return failure()?-ENOMEM:0;}
static int ida_alloc_range(int *ida,int lo,int hi,int flags) {(void)lo;(void)flags;if(failure())return -ENOMEM;for(int i=0;i<=hi;i++)if(!(*ida&(1<<i))){*ida|=1<<i;return i;}return -ENOSPC;}
static void ida_free(int *ida,int i) {assert(*ida&(1<<i));*ida&=~(1<<i);}
static bool ida_is_empty(int *ida) {return !*ida;}
static struct class *class_create(const char *name) {(void)name;if(failure())return ERR_PTR(-ENOMEM);classes++;return calloc(1,sizeof(struct class));}
static void class_destroy(struct class *c) {assert(c&&classes>0);classes--;free(c);}
static int alloc_chrdev_region(dev_t *d,int lo,int n,const char *name) {(void)lo;(void)n;(void)name;if(failure())return -ENOMEM;regions++;*d=MKDEV(9,0);return 0;}
static void unregister_chrdev_region(unsigned d,int n) {(void)d;(void)n;assert(regions>0);regions--;}
static void cdev_init(struct cdev *c,const struct file_operations *f) {(void)f;c->published=false;}
static int cdev_device_add(struct cdev *c,struct device *d) {if(failure())return -EIO;c->parent=d;c->published=true;get_device(d);return 0;}
static void cdev_device_del(struct cdev *c,struct device *d) {(void)d;assert(c->published);c->published=false;put_device(c->parent);}
static void atomic_set(struct atomic_t *a,int n) {a->value=n;}
static int atomic_read(struct atomic_t *a) {return a->value;}
static int atomic_cmpxchg(struct atomic_t *a,int old,int n) {int prev=a->value;if(prev==old)a->value=n;return prev;}
static int config_ep_by_speed(struct usb_gadget *g,struct usb_function *f,struct usb_ep *ep) {(void)g;(void)f;if(++enable_step==enable_fail)return -EIO;ep->desc=(void*)1;return 0;}
static int usb_ep_enable(struct usb_ep *ep) {if(++enable_step==enable_fail)return -EIO;assert(!ep->enabled);ep->enabled=true;return 0;}
static void usb_ep_disable(struct usb_ep *ep) {ep->enabled=false;}
static struct f_ccid *func_to_ccid(struct usb_function *f) {return container_of(f,struct f_ccid,function);}
static struct usb_request *usb_ep_alloc_request(struct usb_ep *ep,int flags) {(void)ep;(void)flags;if(++alloc_step==alloc_fail)return NULL;return calloc(1,sizeof(struct usb_request));}
static void *kmalloc(size_t n,int flags) {(void)flags;if(++alloc_step==alloc_fail)return NULL;return malloc(n);}
static void usb_ep_free_request(struct usb_ep *ep,struct usb_request *r) {(void)ep;free_reqs++;free(r);}
static int usb_ep_queue(struct usb_ep *ep,struct usb_request *r,int flags) {(void)ep;(void)r;(void)flags;if(queue_fault)return -EIO;queued++;return 0;}
#define copy_from_user(d,s,n) (copy_fault?1:(memcpy((d),(s),(n)),0))
#define copy_to_user(d,s,n) (copy_fault?1:(memcpy((d),(s),(n)),0))
/* FUNCTIONS */
int main(void) {
 int faults=0,cases=0;
 for(int f=1;f<=9;f++){step=0;fail_step=f;struct f_ccid *c=ccid_setup();assert(IS_ERR(c));assert(!live&&!classes&&!regions&&!ccid_ida);faults++;}
 fail_step=0;step=0;struct f_ccid *c=ccid_setup();assert(!IS_ERR(c)&&step==9&&live==1);struct device *held=&c->bulk_dev.dev;get_device(held);ccid_cdev_free(&c->ctrl_dev.cdev,&c->ctrl_dev.dev);ccid_cdev_free(&c->bulk_dev.cdev,&c->bulk_dev.dev);ccid_free_chrdev_region();kref_put(&c->ref,ccid_release);assert(live==1);put_device(held);assert(!live&&!classes&&!regions&&!ccid_ida);
 struct f_ccid d={0};struct usb_ep in={.name="in"},out={.name="out"},notify={.name="notify"},ep0={.name="ep0"};struct usb_gadget gadget={.ep0=&ep0};char buffer[8]={0};struct usb_request req={.buf=buffer};struct usb_composite_dev cdev={.gadget=&gadget,.req=&req};struct usb_configuration config={.cdev=&cdev};d.function.config=&config;d.ifc_id=2;d.in=&in;d.out=&out;d.notify=&notify;d.notify_req=&req;
 for(int f=1;f<=6;f++){enable_step=0;enable_fail=f;assert(ccid_function_set_alt(&d.function,2,0)==-EIO);assert(!in.enabled&&!out.enabled&&!notify.enabled&&!atomic_read(&d.online));cases++;}
 enable_fail=0;enable_step=0;assert(!ccid_function_set_alt(&d.function,2,0));assert(in.enabled&&out.enabled&&notify.enabled&&atomic_read(&d.online));assert(ccid_function_set_alt(&d.function,3,0)==-EINVAL);assert(ccid_function_set_alt(&d.function,2,1)==-EINVAL);
 struct usb_ccid_notification n={{0x50,0x91,0x22,0x33}};struct file fp={.private_data=&d};queued=0;
 for(int fault=0;fault<2;fault++)for(int q=0;q<2;q++)for(int cmd=0;cmd<4;cmd++){copy_fault=fault;queue_fault=q;atomic_set(&d.notify_busy,0);unsigned command=cmd==0?CCID_NOTIFY_CARD:cmd==1?CCID_NOTIFY_HWERROR:cmd==2?CCID_READ_DTR:99;int ret=ccid_ctrl_ioctl(&fp,command,(unsigned long)&n);assert(ret==(cmd==3?-ENOTTY:fault?-EFAULT:cmd==2?0:q?-EIO:0));if(cmd<2&&!fault&&!q){assert(req.length==(cmd?4:2));assert(!memcmp(req.buf,n.buf,4));assert(ccid_ctrl_ioctl(&fp,command,(unsigned long)&n)==-EBUSY);}if(q&&cmd<2&&!fault)assert(!atomic_read(&d.notify_busy));cases++;}
 copy_fault=queue_fault=0;atomic_set(&d.notify_busy,0);ccid_function_disable(&d.function);assert(ccid_ctrl_ioctl(&fp,CCID_NOTIFY_CARD,(unsigned long)&n)==-ENODEV);assert(!in.enabled&&!out.enabled&&!notify.enabled);
 for(int mode=0;mode<3;mode++){alloc_fail=mode==2?0:mode+1;alloc_step=0;free_reqs=0;struct usb_request *r=ccid_request_alloc(&in,16,0);if(mode<2){assert(IS_ERR(r));assert(free_reqs==(mode==1));}else {assert(!IS_ERR(r));free(r->buf);usb_ep_free_request(&in,r);}cases++;}
 printf("{\"allocation_rollback_faults\":%d,\"endpoint_and_ioctl_cases\":%d,\"held_device_survives_removal\":true}\n",faults,cases);
}
