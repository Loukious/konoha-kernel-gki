#include "btconsumer_mock.h"
#undef BTFMSWR_ERR
#undef BTFMSWR_INFO
#undef BTFMSWR_DBG
struct swr_device{struct device dev;u8 dev_num;};
/* SWR HEADER */
static DEFINE_MUTEX(btfm_swr_lock);
struct btfmswr*pbtfmswr;
struct class{int alive;};static struct class*btfm_swr_class;
static int btfm_swr_major,btfm_num_ports_open,bt_dev_fops;
static int regions,classes,devices,endpoints,unregistered,cleanups;
static struct file early_file;
#define MKDEV(m,n) ((m)*256+(n))
#define BT_CMD_SWR_TEST 0xbfac
static void swr_set_dev_data(struct swr_device*s,void*p){s->dev.data=p;}
static void*swr_get_dev_data(struct swr_device*s){return s->dev.data;}
static int register_chrdev(int a,const char*n,int*f){if(allocation_fault())return -EIO;regions++;return 33;}
static void unregister_chrdev(int a,const char*n){assert(regions==1);regions--;}
static struct class*class_create(const char*n){if(allocation_fault())return ERR_PTR(-ENOMEM);classes++;return (void*)1;}
static void class_destroy(struct class*c){assert(classes==1);classes--;}
static int btfm_swr_open(struct inode*,struct file*);
static struct device*device_create(struct class*c,void*p,unsigned d,void*v,const char*n){assert(btfm_swr_open(NULL,&early_file)==-ENODEV);if(allocation_fault())return ERR_PTR(-ENOMEM);devices++;return (void*)2;}
static void device_destroy(struct class*c,unsigned d){assert(devices==1);devices--;}
static int btfm_swr_register_hw_ep(struct btfmswr*p){assert(p->stopping&&regions&&classes&&devices&&!btfm_swr_lock.held);if(allocation_fault())return -EIO;endpoints++;return 0;}
static void btfm_swr_unregister_hwep(void){assert(!btfm_swr_lock.held&&pbtfmswr->stopping&&!devices&&!regions&&endpoints==1);endpoints--;unregistered++;}
static int btfm_swr_disable_port_locked(u8 port,u8 channels,u8 usecase){assert(btfm_swr_lock.held&&unregistered&&port==2&&channels==2&&usecase==7);cleanups++;return -EIO;}
static int btfm_swr_hw_init_locked(void){assert(btfm_swr_lock.held&&pbtfmswr&&!pbtfmswr->stopping);return 17;}
/* FUNCTIONS */
int main(void){struct swr_device dev={0};unsigned failures=0;for(int fail=1;fail<=5;fail++){alloc_step=0;alloc_fault=fail;assert(btfm_swr_probe(&dev)<0);assert(!pbtfmswr&&!dev.dev.data&&!live&&!regions&&!classes&&!devices&&!endpoints);failures++;}alloc_fault=0;assert(!btfm_swr_probe(&dev)&&live==1&&regions==1&&classes==1&&devices==1&&endpoints==1);struct btfmswr*old=pbtfmswr;assert(btfm_swr_probe(&dev)==-EBUSY&&pbtfmswr==old&&live==1);struct file f={0},newfile={0};assert(!btfm_swr_open(NULL,&f)&&old->ref.refs==2);assert(btfm_swr_ioctl(&f,0,0)==-ENOTTY&&btfm_swr_ioctl(&f,BT_CMD_SWR_TEST,0)==17);old->ports[2].connected=true;old->ports[2].channels=2;old->ports[2].usecase=7;assert(!btfm_swr_remove(&dev));assert(!pbtfmswr&&!dev.dev.data&&!regions&&!classes&&!devices&&!endpoints&&live==1&&cleanups==1);assert(btfm_swr_ioctl(&f,BT_CMD_SWR_TEST,0)==-ENODEV);assert(btfm_swr_open(NULL,&newfile)==-ENODEV);assert(!btfm_swr_probe(&dev)&&pbtfmswr!=old&&live==2);assert(btfm_swr_ioctl(&f,BT_CMD_SWR_TEST,0)==-ENODEV);assert(!btfm_swr_release(NULL,&f)&&live==1);assert(!btfm_swr_open(NULL,&newfile)&&btfm_swr_ioctl(&newfile,BT_CMD_SWR_TEST,0)==17);assert(!btfm_swr_release(NULL,&newfile));assert(!btfm_swr_remove(&dev)&&!live);printf("{\"probe_failure_steps\":%u,\"final_endpoint_publication\":true,\"drain_without_hardware_lock\":true,\"stale_file_rejects_reprobe\":true,\"failed_remove_cleanup_reported\":true}\n",failures);}
