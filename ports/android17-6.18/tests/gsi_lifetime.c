#include "gsi_mock.h"
#define ETH_ALEN 6
struct sg_table{int unused;};
/* IPA_TYPES */
#define GSI_CTRL_NAME_LEN 12
#define GSI_RMNET_CTRL_NAME "rmnet_ctrl"
#define GSI_MBIM_CTRL_NAME "mbim_ctrl"
#define GSI_DPL_CTRL_NAME "dpl_ctrl"
#define MAX_CDEV_INSTANCES 3
#define GSI_MAX_CTRL_PKT_SIZE 8192
#define DEFAULT_RW_TIMER_INTERVAL 500
#define MKDEV(a,b) (((a)<<8)|(b))
#define MINOR(d) ((d)&255)
#define KERN_DEBUG 0
#define DUMP_PREFIX_OFFSET 0
#define print_hex_dump(...) ((void)0)
static int strscpy(char*d,const char*s,size_t n){return snprintf(d,n,"%s",s);}
struct class{int unused;};struct device{int refs;dev_t devt;struct class*class;void*data;void(*release)(struct device*);};struct cdev{struct module*owner;struct device*parent;bool published;};struct inode{struct cdev*i_cdev;};struct file{void*private_data;int f_flags;};struct file_operations{int unused;};struct usb_function_instance{int unused;};
struct gsi_ctrl_pkt{void*buf;int len,type;struct list_head list;};
struct gsi_ctrl_port{char name[GSI_CTRL_NAME_LEN];struct cdev cdev;struct device dev;spinlock_t lock;bool is_open;wait_queue_head_t read_wq;struct list_head cpkt_req_q,cpkt_resp_q;unsigned copied_to_modem;};
struct gsi_data_port{spinlock_t lock;struct{spinlock_t q_lock;}evt_q;struct delayed_work usb_ipa_w;struct workqueue_struct*ipa_usb_wq;int in_channel_handle,out_channel_handle;};
struct f_gsi{struct kref ref;struct mutex ctrl_mutex;struct gsi_ctrl_port c_port;struct gsi_data_port d_port;enum ipa_usb_teth_prot prot_id;bool dying,cdev_registered;void*ipc_log_ctxt;struct timer_list gsi_rw_timer;u16 gsi_rw_timer_interval;};
struct gsi_opts{struct usb_function_instance func_inst;struct f_gsi*gsi;};
static struct{struct mutex gsi_lock;bool inst_exist;struct gsi_opts*opts;}inst_status[IPA_USB_MAX_TETH_PROT_SIZE];
static struct{bool qti_packet_debug;}debugfs;
static int major=9,gsi_ida;static struct class klass,*gsi_class=&klass;static struct workqueue_struct wq={1},*ipa_usb_wq=&wq;static const struct file_operations gsi_ctrl_dev_fops={0};
static int ida_alloc_range(int*ida,int low,int high,int f){if(allocation_fault())return -ENOMEM;for(int i=low;i<=high;i++)if(!(*ida&(1<<i))){*ida|=1<<i;return i;}return -ENOSPC;}
static void ida_free(int*ida,int i){assert(*ida&(1<<i));*ida&=~(1<<i);}
static void device_initialize(struct device*d){d->refs=1;}
static void get_device(struct device*d){assert(d->refs>0);d->refs++;}
static void put_device(struct device*d){assert(d->refs>0);if(!--d->refs)d->release(d);}
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define dev_get_drvdata(d) ((d)->data)
static int dev_set_name(struct device*d,const char*f,const char*n){return allocation_fault()?-ENOMEM:0;}
#define cdev_init(c,f) ((c)->published=false)
static int cdev_device_add(struct cdev*c,struct device*d){if(allocation_fault())return -EIO;c->parent=d;c->published=true;get_device(d);return 0;}
static void cdev_device_del(struct cdev*c,struct device*d){assert(c->published);c->published=false;put_device(c->parent);}
static void ipc_log_context_destroy(void*p){kfree(p);}
static int copy_to_user(void*d,const void*s,size_t n){if(n)memcpy(d,s,n);return 0;}
static struct gsi_opts *sleep_remove;
static void gsi_free_inst(struct usb_function_instance*f);
static void mock_sleep(wait_queue_head_t*w){if(sleep_remove){struct gsi_opts*o=sleep_remove;sleep_remove=NULL;gsi_free_inst(&o->func_inst);}}
#define wait_event_interruptible(w,p) (mock_sleep(&(w)),(p)?0:-ERESTARTSYS)
static void gsi_put(struct f_gsi*gsi);
/* FUNCTIONS */
static struct gsi_opts*instance(void){struct gsi_opts*o=kzalloc(sizeof(*o),0);o->gsi=gsi_function_init(IPA_USB_RMNET);assert(!IS_ERR(o->gsi));inst_status[IPA_USB_RMNET].opts=o;inst_status[IPA_USB_RMNET].inst_exist=true;return o;}
int main(void){unsigned cases=0;for(int fault=1;fault<=4;fault++){alloc_step=0;alloc_fault=fault;struct f_gsi*g=gsi_function_init(IPA_USB_RMNET);assert(IS_ERR(g));assert(!gsi_ida&&!live);cases++;}alloc_fault=0;struct gsi_opts*o=instance();struct f_gsi*g=o->gsi;struct inode inode={&g->c_port.cdev};struct file fp={0};get_device(inode.i_cdev->parent);assert(!gsi_ctrl_dev_open(&inode,&fp));put_device(inode.i_cdev->parent);assert(g->ref.refs==3);struct file dup={0};assert(gsi_ctrl_dev_open(&inode,&dup)==-EBUSY);char buf[8];fp.f_flags=O_NONBLOCK;assert(gsi_ctrl_dev_read(&fp,buf,8,NULL)==-EAGAIN);assert(!gsi_ctrl_dev_read(&fp,buf,0,NULL));fp.f_flags=0;sleep_remove=o;assert(gsi_ctrl_dev_read(&fp,buf,8,NULL)==-ENODEV);assert(live==1&&g->dying&&g->ref.refs==1&&!gsi_ida);assert(!gsi_ctrl_dev_release(&inode,&fp)&&!live);cases++;
o=instance();g=o->gsi;inode.i_cdev=&g->c_port.cdev;get_device(inode.i_cdev->parent);gsi_free_inst(&o->func_inst);assert(live==1);assert(gsi_ctrl_dev_open(&inode,&fp)==-ENODEV);put_device(inode.i_cdev->parent);assert(!live&&!gsi_ida);cases++;printf("{\"cdev_failure_and_removal_cases\":%u,\"blocked_read_woken_on_removal\":true,\"open_file_outlives_instance\":true,\"racing_cdev_open_has_device_parent\":true}\n",cases);}
