#include "slim_mock.h"
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{int unused;};struct idr{void*slot[256];};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define IS_ENABLED(x) 0
#define SLIMBUS_NAME_SIZE 32
#define BTFMSLIM_INFO(...) ((void)0)
#define BTFMSLIM_ERR(...) ((void)0)
#define BTFMSLIM_DBG(...) ((void)0)
/* PUBLIC HEADER */
/* BT HEADER */
struct class{int live;};
static struct btfmslim*btfm_slim_drv_data;static struct class*btfm_slim_class;static int btfm_num_ports_open;static DEFINE_MUTEX(btfm_slim_lifecycle_lock);
static struct btfmslim_ch slave_rxport[3],slave_txport[4];
#define SLIM_SLAVE_INIT NULL
#define SLIM_SLAVE_PORT_EN NULL
#define BT_CMD_SLIM_TEST 0xbfac
static int steps,fail_at,region_live,device_live,endpoint_live,init_calls;static struct device cdevice;
static bool fail(void){return ++steps==fail_at;}
int btfm_slim_hw_init(struct btfmslim*p){init_calls++;return 0;}
static void btfm_slim_get_hwep_details(struct slim_device*d,struct btfmslim*p){}
static int alloc_chrdev_region(dev_t*p,int first,int count,const char*n){if(fail())return -EIO;*p=123;region_live++;return 0;}
static void unregister_chrdev_region(dev_t p,int count){assert(region_live);region_live--;}
static struct cdev*cdev_alloc(void){if(fail())return NULL;struct cdev*c=kzalloc(sizeof(*c),0);c->kobj.refs=1;return c;}
static void kobject_put(struct kobject*k){assert(k->refs);if(!--k->refs)kfree(container_of(k,struct cdev,kobj));}
static int cdev_add(struct cdev*c,dev_t p,int count){if(fail())return -EIO;c->alive=1;return 0;}
static void cdev_del(struct cdev*c){assert(c->alive);c->alive=0;kobject_put(&c->kobj);}
static struct class*class_create(const char*n){if(fail())return ERR_PTR(-EIO);return kzalloc(sizeof(struct class),0);}
static void class_destroy(struct class*c){kfree(c);}
static struct device*device_create(struct class*c,struct device*d,dev_t p,void*data,const char*n){if(fail())return ERR_PTR(-EIO);device_live=1;return &cdevice;}
static void device_destroy(struct class*c,dev_t n){assert(device_live);device_live=0;}
static int btfm_slim_register_hw_ep(struct btfmslim*p){if(fail())return -EIO;endpoint_live=1;return 0;}
static void btfm_slim_unregister_hwep(void){assert(endpoint_live);endpoint_live=0;}
int btfm_slim_disable_ch(struct btfmslim*p,struct btfmslim_ch*c,u8 r,u8 n){return -EIO;}
int slim_stream_free(struct slim_stream_runtime*r){kfree(r);return 0;}
static void*bt_dev_fops;
/* FUNCTIONS */
int main(void){struct slim_controller*ctrl=(void*)1;struct slim_device dev={.ctrl=ctrl};for(int f=1;f<=3;f++){alloc_fault=alloc_step+f;assert(btfm_slim_probe(&dev)==-ENOMEM&&!live&&!btfm_slim_drv_data);alloc_fault=0;}for(int f=1;f<=6;f++){fail_at=f;steps=0;assert(btfm_slim_probe(&dev)<0);assert(!live&&!btfm_slim_drv_data&&!region_live&&!device_live&&!endpoint_live);}fail_at=steps=0;assert(!btfm_slim_probe(&dev));struct btfmslim*old=btfm_slim_drv_data;struct inode ino={old->cdev};struct file file={0};assert(!btfm_slim_open(&ino,&file)&&old->ref.refs==2);assert(btfm_slim_ioctl(&file,0,0)==-ENOTTY);assert(!btfm_slim_ioctl(&file,BT_CMD_SLIM_TEST,0)&&init_calls==1);old->cdev->kobj.refs++;btfm_slim_remove(&dev);assert(!region_live&&!device_live&&!endpoint_live&&!btfm_slim_drv_data);assert(btfm_slim_ioctl(&file,BT_CMD_SLIM_TEST,0)==-ENODEV&&init_calls==1);assert(!btfm_slim_probe(&dev));struct file stale={0};assert(btfm_slim_open(&ino,&stale)==-ENODEV);assert(!btfm_slim_release(&ino,&file));kobject_put(&ino.i_cdev->kobj);btfm_slim_remove(&dev);assert(!live&&!region_live&&!device_live&&!endpoint_live);printf("{\"probe_failures\":9,\"open_survives_remove\":true,\"stale_file_ioctl_rejected\":true,\"stale_inode_rebind_rejected\":true}\n");}
