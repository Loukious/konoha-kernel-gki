#include "btconsumer_mock.h"
/* CODEC HEADER */
/* PACKET HEADER */
static struct btfmcodec_data *btfmcodec;
struct srcu_struct btfmcodec_endpoint_srcu;
struct class{int live;};struct device_driver{const char*name;};
static struct class*dev_class;static dev_t dev_major;
static struct device_driver driver={"btfmcodec"};static int btfmcodec_groups[1],btfmcodec_fops;
static int regions,classes,published,open_checks,held_cdev;
static struct device*partial_ref;
#define dev_to_btfmcodec(p) container_of(p,struct btfmcodec_data,dev)
#define cdev_to_btfmchardev(p) container_of(p,struct btfmcodec_char_device,cdev)
#define alloc_ordered_workqueue(n,f) create_singlethread_workqueue(n)
#define dev_set_drvdata(d,p) ((d)->data=(p))
static void device_initialize(struct device*d){d->refs=1;}
static int alloc_chrdev_region(dev_t*d,int base,int n,const char*name){if(allocation_fault())return -EIO;*d=30;regions++;return 0;}
static void unregister_chrdev_region(dev_t d,int n){assert(regions==1);regions--;}
static struct class*class_create(const char*n){if(allocation_fault())return ERR_PTR(-ENOMEM);struct class*c=calloc(1,sizeof(*c));classes++;return c;}
static void class_destroy(struct class*c){assert(classes==1);classes--;free(c);}
static int dev_set_name(struct device*d,const char*f,const char*n){return allocation_fault()?-ENOMEM:0;}
static void cdev_init(struct cdev*c,int*f){c->alive=0;}
static int btfmcodec_dev_open(struct inode*,struct file*);
static int cdev_device_add(struct cdev*c,struct device*d){struct inode i={c};struct file f={0};assert(btfmcodec&&btfmcodec->btfmcodec_dev->workqueue&&regions&&classes);assert(btfmcodec_dev_open(&i,&f)==-ENODEV&&!f.private_data);assert(!btfm_get_btfmcodec());open_checks++;if(allocation_fault()){partial_ref=get_device(d);held_cdev++;return -EIO;}c->alive=1;published++;return 0;}
static void cdev_device_del(struct cdev*c,struct device*d){assert(c->alive&&published==1);c->alive=0;published--;}
static int during_drain;
static void checked_cancel(struct work_struct*w){w->pending=0;if(during_drain){struct inode i={&btfmcodec->btfmcodec_dev->cdev};struct file f={0};assert(btfmcodec_dev_open(&i,&f)==-EACCES);}}
#undef cancel_work_sync
#define cancel_work_sync(w) checked_cancel(w)
/* FUNCTIONS */
int main(void){unsigned failures=0;for(int fail=1;fail<=7;fail++){alloc_step=0;alloc_fault=fail;assert(btfmcodec_init()<0&&!btfmcodec&&!regions&&!classes&&!queues&&!published);if(partial_ref){assert(live==2);put_device(partial_ref);partial_ref=NULL;held_cdev--;}assert(!live&&!held_cdev);failures++;}alloc_fault=alloc_step=0;assert(!btfmcodec_init()&&alloc_step==7&&btfmcodec==btfm_get_btfmcodec()&&regions==1&&classes==1&&queues==1&&live==2);struct btfmcodec_data*c=btfmcodec;struct btfmcodec_char_device*ch=c->btfmcodec_dev;assert(ch->active_clients==1&&ch->ready&&c->dev.refs==1);struct inode i={&ch->cdev};struct file f={0},other={0};assert(!btfmcodec_dev_open(&i,&f)&&c->dev.refs==2&&ch->active_clients==2);assert(btfmcodec_dev_open(&i,&other)==-EACCES);during_drain=1;assert(!btfmcodec_dev_release(&i,&f));during_drain=0;assert(c->dev.refs==1&&ch->active_clients==1&&ch->closing);assert(!btfmcodec_dev_open(&i,&f)&&!ch->closing);assert(!btfmcodec_dev_release(&i,&f));get_device(&c->dev);btfmcodec_deinit();assert(!btfmcodec&&!regions&&!classes&&!queues&&!published&&live==2);assert(btfmcodec_dev_open(&i,&f)==-ENODEV);put_device(&c->dev);assert(!live);printf("{\"init_failure_steps\":%u,\"partial_cdev_open_rejected\":true,\"delayed_cdev_reference_release\":true,\"single_client_until_drain\":true,\"deinit_balanced\":true}\n",failures);}
