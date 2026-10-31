#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define __GFP_ZERO 0
#define WQ_HIGHPRI 0
#define BIT(n) (1U<<(n))
#define IS_TOUCH_ID_INVALID(n) ((n)<0||(n)>=2)
#define LOG_ERROR(...)
#define IS_ERR(p) false
typedef int8_t s8;typedef uint32_t u32;
struct device {int unused;};struct task_struct {int unused;};struct rw_semaphore {int lock;};
static struct rw_semaphore xiaomi_touch_access_lock;
static bool touch_ready=true;
static struct task_struct *xiaomi_touch_temp_thread;
typedef struct {int frame_data_page_size,frame_data_buf_size,raw_data_page_size,raw_data_buf_size;} hardware_param_t;
typedef struct {int (*set_thermal_temp)(int,bool);} hardware_operation_t;
typedef struct {s8 touch_id;hardware_param_t hardware_param;hardware_operation_t hardware_operation;} xiaomi_touch_driver_param_t;
typedef struct {s8 touch_id;bool initialized;int private_data_lock,common_data_buf_lock,private_data_list,temp_detect_wait_queue,temp_detect_ready;u32 frame_data_size,raw_data_size;int frame_data_buf_size,raw_data_buf_size;void *frame_data_mmap_base,*raw_data_mmap_base,*event_wq,*poll_data;uintptr_t frame_data_mmap_phy_base,raw_data_mmap_phy_base;} xiaomi_touch_data_t;
typedef struct {int unused;} htc_ic_polldata_t;
static struct {unsigned panel_register_mask;xiaomi_touch_data_t xiaomi_touch_data[2];xiaomi_touch_driver_param_t xiaomi_touch_driver_param[2];} xiaomi_touch;
static unsigned steps,fail_step,live,proc_live;static void *mode_queue;
static bool fault(void){return ++steps==fail_step;}
static void *allocate(size_t n){if(fault())return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void drop(void *p){if(p)live--;free(p);}
static void down_write(struct rw_semaphore *s){assert(!s->lock);s->lock=1;}
static void up_write(struct rw_semaphore *s){assert(s->lock);s->lock=0;}
#define spin_lock_init(p) (*(p)=0)
#define mutex_init(p) (*(p)=0)
#define INIT_LIST_HEAD(p) (*(p)=1)
#define init_waitqueue_head(p) (*(p)=1)
#define atomic_set(p,n) (*(p)=(n))
#define virt_to_phys(p) ((uintptr_t)(p))
static void *alloc_pages_exact(size_t n,int f){return allocate(n);}
static void free_pages_exact(void *p,size_t n){drop(p);}
static void *alloc_ordered_workqueue(const char *n,int flags){return allocate(1);}
static void destroy_workqueue(void *p){drop(p);}
#define kzalloc(n,f) allocate(n)
#define kfree(p) drop(p)
static int xiaomi_touch_init_touch_mode(s8 id,struct device *dev){mode_queue=allocate(1);return mode_queue?0:-ENOMEM;}
static void xiaomi_touch_mode_remove(s8 id){drop(mode_queue);mode_queue=NULL;}
static int xiaomi_touch_create_proc(xiaomi_touch_driver_param_t *p){if(fault()){proc_live=1;return -ENOMEM;}proc_live=4;return 0;}
static int xiaomi_touch_remove_proc(s8 id){proc_live=0;return 0;}
static int xiaomi_touch_temp_thread_func(void *d){return 0;}
static struct task_struct *kthread_run(int(*f)(void *),void *d,const char *n){return NULL;}
/* FUNCTIONS */
int main(void){struct device dev;hardware_param_t p={.frame_data_page_size=2,.frame_data_buf_size=5,.raw_data_page_size=1,.raw_data_buf_size=5};unsigned faults=0;
for(unsigned n=1;n<=6;n++){memset(&xiaomi_touch,0,sizeof(xiaomi_touch));steps=0;fail_step=n;assert(register_touch_panel(&dev,0,&p,NULL)<0);assert(!live&&!proc_live&&!mode_queue&&!xiaomi_touch.panel_register_mask);assert(!xiaomi_touch.xiaomi_touch_data[0].initialized);faults++;}
steps=0;fail_step=0;assert(!register_touch_panel(&dev,0,&p,NULL));assert(live==5&&proc_live==4&&xiaomi_touch.panel_register_mask==1);assert(register_touch_panel(&dev,0,&p,NULL)==-EBUSY);xiaomi_touch_data_t *d=&xiaomi_touch.xiaomi_touch_data[0];assert(d->private_data_list&&d->temp_detect_wait_queue);xiaomi_touch_remove_proc(0);xiaomi_touch_mode_remove(0);drop(d->poll_data);destroy_workqueue(d->event_wq);free_pages_exact(d->raw_data_mmap_base,5*PAGE_SIZE);free_pages_exact(d->frame_data_mmap_base,10*PAGE_SIZE);assert(!live);printf("{\"panel_registration_fault_cases\":%u,\"publication_after_initialization\":true,\"partial_proc_cleanup\":true,\"duplicate_registration_rejected\":true}\n",faults);}
