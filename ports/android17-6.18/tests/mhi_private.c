#include "mock.h"
#define MHI_IPC_LOG_PAGES 100
#define REG_WRITE_QUEUE_LEN 512
#define MHI_NUMERIC_DEVICE_ID(d,a,b,c) (d)
#define PCI_SLOT(n) ((n)>>3)
struct device{void *data;char name[32];};struct pci_bus{int number;};struct pci_dev{struct device dev;unsigned device,devfn;struct pci_bus *bus;};struct mhi_device{struct device dev;const char *name;};struct mhi_controller{struct device *cntrl_dev;struct mhi_device *mhi_dev;};struct work_struct{int initialized;};struct workqueue_struct{int x;};struct reg_write_info{void *reg_addr;u32 val;bool valid;};struct mhi_sfr_info{void *str;};struct mhi_private{struct list_head node;struct mhi_controller *mhi_cntrl;u32 numeric_id;void *log_buf;struct workqueue_struct *offload_wq;struct work_struct reg_write_work;struct reg_write_info *reg_write_q;int write_idx;struct mhi_sfr_info *sfr_info;void *timesync;};static struct{int lock;struct list_head controller_list;}mhi_bus;
#define to_pci_dev(p) container_of(p,struct pci_dev,dev)
#define WQ_HIGHPRI 1
#define INIT_WORK(p,f) ((p)->initialized=1)
#define atomic_set(p,n) (*(p)=(n))
static unsigned drains,destroyed,published;static int sysfs_step,sysfs_fault,sysfs_groups;
static void mhi_reg_write_work(void){}
static void *dev_get_drvdata(struct device *d){return d->data;}
static void dev_set_drvdata(struct device *d,void *p){if(p){struct mhi_private *v=p;assert(v->offload_wq&&v->reg_write_q&&v->reg_write_work.initialized);published++;}else assert(destroyed);d->data=p;}
static void dev_set_name(struct device *d,const char *fmt,...){strcpy(d->name,"mhi_test");}
static const char *dev_name(struct device *d){return d->name;}
static int pci_domain_nr(struct pci_bus *b){return 0;}
static void *ipc_log_context_create(int n,const char *name,int f){return alloc_mem(1);}
static void ipc_log_context_destroy(void *p){free_mem(p);}
static struct workqueue_struct *alloc_ordered_workqueue(const char *name,int f){return alloc_mem(sizeof(struct workqueue_struct));}
static void cancel_work_sync(struct work_struct *p){assert(p->initialized);drains++;}
static void destroy_workqueue(struct workqueue_struct *p){destroyed++;free_mem(p);}
static int mhi_misc_group,mhi_tsync_group;
static int sysfs_create_group(void *k,int *g){if(++sysfs_step==sysfs_fault)return -EIO;sysfs_groups++;return 0;}
static void sysfs_remove_group(void *k,int *g){assert(sysfs_groups>0);sysfs_groups--;}
#define kobj data
/* FUNCTIONS */
int main(void){struct pci_bus b={0};struct pci_dev p={.device=0x1107,.bus=&b};struct mhi_device d={.name="mhi_test"};struct mhi_controller c={&p.dev,&d};unsigned cases=0;for(int fault=0;fault<=4;fault++){INIT_LIST_HEAD(&mhi_bus.controller_list);allocation_call=0;fail_alloc=fault;published=drains=destroyed=0;int ret=mhi_misc_register_controller(&c);if(!ret){assert(published==1&&d.dev.data);mhi_misc_unregister_controller(&c);assert(drains==1&&destroyed==1&&!d.dev.data);}else assert(ret==-ENOMEM&&!d.dev.data&&published==0);assert(!allocations&&mhi_bus.controller_list.next==&mhi_bus.controller_list);cases++;}fail_alloc=0;for(sysfs_fault=0;sysfs_fault<=2;sysfs_fault++){sysfs_step=sysfs_groups=0;int ret=mhi_misc_sysfs_create(&c);if(!ret)mhi_misc_sysfs_destroy(&c);else assert(ret==-EIO);assert(!sysfs_groups);cases++;}printf("{\"private_state_and_sysfs_fault_cases\":%u,\"publication_after_queue_setup\":true,\"workqueue_freed_before_private_storage\":true}\n",cases);}
