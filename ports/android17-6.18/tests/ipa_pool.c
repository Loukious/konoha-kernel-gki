#include "ipa_mock.h"
#define __init
#define __exit
#define pr_info(...) ((void)0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define POOL_LEN 4
#define POOL_NOTIF 3
#define MAX_STATIC_POOL 4096
#define RMNET_MEM_FAIL -1
#define RMNET_MEM_SUCCESS 0
#define RMNET_MEM_UPGRADE -3
#define RMNET_MEM_DOWNGRADE -2
#define DATARMNETf03ba54970 2
#define DATARMNETbcd0fd349d 3
#define list_add_rcu(n,h) list_add_tail(n,h)
#define WQ_HIGHPRI 1
#define CLOCK_MONOTONIC 1
#define HRTIMER_MODE_REL 0
static int page_live,page_attempt,page_fault,notifies;
struct page{int refs;};struct mem_info{struct page*addr;struct list_head mem_head;u8 order;};
static struct list_head rmnet_mem_pool[POOL_LEN];static int static_pool_size[POOL_LEN],max_pool_size[POOL_LEN],pool_unbound_feature[POOL_LEN],rmnet_mem_id_req[POOL_LEN],rmnet_mem_order_requests[POOL_LEN],rmnet_mem_id_recycled[POOL_LEN],DATARMNETfb2a1a4560[POOL_LEN],DATARMNETaceba3d00e[8];
static spinlock_t rmnet_mem_lock;
static void page_ref_inc(struct page*p){assert(p->refs>0);p->refs++;}
static int page_ref_count(struct page*p){return p->refs;}
static void put_page(struct page*p){assert(p&&p->refs>0);if(!--p->refs){page_live--;free(p);}}
static struct page*__dev_alloc_pages(gfp_t f,unsigned order){if(++page_attempt==page_fault)return NULL;struct page*p=calloc(1,sizeof(*p));assert(p);p->refs=1;page_live++;return p;}
#define list_first_entry_or_null(h,t,m) (list_empty(h)?NULL:list_first_entry(h,t,m))
static void list_rotate_left(struct list_head*h){if(!list_empty(h))list_move_tail(h->next,h);}
static int rmnet_mem_mode_notify(unsigned n){notifies++;return 0;}
static struct workqueue_struct*mem_wq;static struct delayed_work pool_adjust_work;struct hrtimer{bool active;};static struct hrtimer DATARMNET7bbd010c46;static int nl_fail,nl_active,cleanup_step;
static void mem_update_pool_work(struct work_struct*w){}
static int DATARMNET2c79fa7b83(struct hrtimer*t){return 0;}
#define alloc_workqueue(n,f,m) create_singlethread_workqueue(n)
static void hrtimer_setup(struct hrtimer*t,int(*f)(struct hrtimer*),int c,int m){t->active=false;}
static void hrtimer_cancel(struct hrtimer*t){assert(cleanup_step==1);cleanup_step++;t->active=false;}
static int rmnet_mem_nl_register(void){assert(!nl_active);if(nl_fail)return nl_fail;nl_active=1;return 0;}
static void rmnet_mem_nl_unregister(void){assert(nl_active);nl_active=0;cleanup_step=1;}
static void drain_workqueue(struct workqueue_struct*w){assert(cleanup_step==2&&!DATARMNET7bbd010c46.active);cleanup_step++;}
/* FUNCTIONS */
int main(void)
{
 int code,order;unsigned cases=0;
 for(int i=0;i<POOL_LEN;i++){INIT_LIST_HEAD(&rmnet_mem_pool[i]);}
 for(unsigned o=POOL_LEN;o<POOL_LEN+10;o++){assert(!rmnet_mem_get_pages_entry(0,o,&code,&order,1)&&code==RMNET_MEM_FAIL&&order==0);cases++;}
 assert(!rmnet_mem_get_pages_entry(0,2,&code,&order,UINT_MAX));assert(!rmnet_mem_get_pages_entry(0,UINT_MAX,NULL,NULL,0));
 struct page*p=rmnet_mem_get_pages_entry(0,2,&code,&order,1);assert(p&&p->refs==1&&code==0&&order==2);put_page(p);
 assert(!rmnet_mem_get_pages_entry(0,3,&code,&order,1));
 max_pool_size[2]=10;pool_unbound_feature[2]=1;alloc_fault=alloc_step+1;p=rmnet_mem_get_pages_entry(0,2,&code,&order,1);assert(p&&p->refs==1&&!static_pool_size[2]);put_page(p);alloc_fault=0;
 p=rmnet_mem_get_pages_entry(0,2,&code,&order,1);assert(p&&p->refs==2&&static_pool_size[2]==1);put_page(p);p=rmnet_mem_get_pages_entry(0,1,&code,&order,1);assert(p&&order==2&&code==RMNET_MEM_UPGRADE);put_page(p);
 p=rmnet_mem_get_pages_entry(0,2,NULL,NULL,1);assert(p&&p->refs==2);rmnet_mem_free_all();assert(p->refs==1);put_page(p);
 for(int f=1;f<=3;f++){alloc_fault=alloc_step+f;rmnet_mem_adjust(3,2);rmnet_mem_free_all();assert(!live&&!page_live);cases++;}alloc_fault=0;
 page_fault=page_attempt+1;rmnet_mem_adjust(3,2);assert(static_pool_size[2]==2);rmnet_mem_adjust(1,2);assert(static_pool_size[2]==1);rmnet_mem_free_all();
 page_fault=0;rmnet_mem_adjust(2,3);int notifications=notifies;rmnet_mem_adjust(2,3);assert(notifies==notifications);rmnet_mem_free_all();
 nl_fail=-EIO;assert(rmnet_mem_module_init()==-EIO&&!mem_wq&&!queues);nl_fail=0;assert(!rmnet_mem_module_init()&&mem_wq&&nl_active);DATARMNET7bbd010c46.active=true;rmnet_mem_module_exit();assert(!queues&&!mem_wq&&!live&&!page_live&&cleanup_step==3);
 printf("{\"pool_fault_and_boundary_cases\":%u,\"optional_outputs_safe\":true,\"pool_slot_failures_release_pages\":true,\"held_pages_survive_pool_removal\":true,\"timer_stopped_before_queue_destroy\":true}\n",cases);
}
