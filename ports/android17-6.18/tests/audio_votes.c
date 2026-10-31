#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GFP_KERNEL 0
#define U32_MAX UINT32_MAX
#define IS_ERR(p) ((intptr_t)(p)<0&&(intptr_t)(p)>-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define pr_err_ratelimited(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_dbg(...) ((void)0)
typedef uint32_t u32;
struct device {int dummy;};
struct clk {int owned,external;};
struct mutex {pthread_mutex_t lock;bool initialized;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER,true}
static void mutex_init(struct mutex *m){assert(!m->initialized);assert(!pthread_mutex_init(&m->lock,NULL));m->initialized=true;}
static void mutex_destroy(struct mutex *m){assert(m->initialized);assert(!pthread_mutex_destroy(&m->lock));m->initialized=false;}
static void mutex_lock(struct mutex *m){assert(m->initialized);assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&(n),&(n)}
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) for(p=container_of((h)->next,__typeof__(*p),m),n=container_of((p)->m.next,__typeof__(*p),m);&(p)->m!=(h);p=n,n=container_of((n)->m.next,__typeof__(*n),m))
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->next->prev=n->prev;n->prev->next=n->next;}
static int live;
static bool fail_alloc;
static void *kzalloc(size_t s,int f){(void)f;if(fail_alloc)return NULL;void *p=calloc(1,s);assert(p);live++;return p;}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
static int prepare_calls,disable_calls,fail_prepare;
static int clk_prepare_enable(struct clk *c){assert(c&&!IS_ERR(c));if(++prepare_calls==fail_prepare)return -EIO;c->owned++;return 0;}
static void clk_disable_unprepare(struct clk *c){assert(c&&!IS_ERR(c)&&c->owned>0);c->owned--;disable_calls++;}
static bool __clk_is_enabled(struct clk *c){assert(c&&!IS_ERR(c));return c->owned+c->external>0;}
/* SOURCE */
static struct device dev;
static struct clk clock_handle;
static unsigned votes(void){unsigned cases=0;for(int external=0;external<5;external++)for(int count=1;count<=8;count++){struct clk c={.external=external};digital_cdc_rsc_mgr_init();for(int i=0;i<count;i++)assert(!digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev));int before=disable_calls;digital_cdc_rsc_mgr_hw_vote_reset(&c);assert(c.owned==count&&c.external==external&&disable_calls-before==count);for(int i=0;i<count;i++)digital_cdc_rsc_mgr_hw_vote_disable(&c,&dev);assert(!c.owned);digital_cdc_rsc_mgr_hw_vote_disable(&c,&dev);digital_cdc_rsc_mgr_exit();assert(!live);cases++;}return cases;}
static unsigned restoration(void){unsigned cases=0;for(int fail=1;fail<=4;fail++)for(int retry=0;retry<2;retry++){struct clk c={.external=2};digital_cdc_rsc_mgr_init();for(int i=0;i<4;i++)assert(!digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev));prepare_calls=0;fail_prepare=fail;digital_cdc_rsc_mgr_hw_vote_reset(&c);fail_prepare=0;assert(c.owned==fail-1&&c.external==2);if(retry){digital_cdc_rsc_mgr_hw_vote_reset(&c);assert(c.owned==4);}for(int i=0;i<4;i++)digital_cdc_rsc_mgr_hw_vote_disable(&c,&dev);assert(!c.owned&&c.external==2);digital_cdc_rsc_mgr_exit();assert(!live);cases++;}return cases;}
static unsigned faults(void){struct clk c={0};digital_cdc_rsc_mgr_init();fail_alloc=true;assert(digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev)==-ENOMEM&&!c.owned);fail_alloc=false;prepare_calls=0;fail_prepare=1;assert(digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev)==-EIO&&!c.owned&&!live);fail_prepare=0;assert(digital_cdc_rsc_mgr_hw_vote_enable(ERR_PTR(-EIO),&dev)==-EINVAL);assert(digital_cdc_rsc_mgr_hw_vote_enable(&c,NULL)==-EINVAL);digital_cdc_rsc_mgr_hw_vote_disable(ERR_PTR(-EIO),&dev);digital_cdc_rsc_mgr_hw_vote_reset(ERR_PTR(-EIO));digital_cdc_rsc_mgr_exit();assert(digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev)==-ENODEV);digital_cdc_rsc_mgr_hw_vote_disable(&c,&dev);digital_cdc_rsc_mgr_hw_vote_reset(&c);return 7;}
static unsigned shutdown(void){struct clk c={.external=3};digital_cdc_rsc_mgr_init();for(int i=0;i<4;i++)assert(!digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev));digital_cdc_rsc_mgr_exit();assert(!c.owned&&c.external==3&&!live);digital_cdc_rsc_mgr_hw_vote_disable(&c,&dev);digital_cdc_rsc_mgr_init();assert(!digital_cdc_rsc_mgr_hw_vote_enable(&c,&dev));digital_cdc_rsc_mgr_hw_vote_disable(&c,&dev);digital_cdc_rsc_mgr_exit();return 2;}
static void *vote_thread(void *unused){(void)unused;for(int i=0;i<1000;i++){assert(!digital_cdc_rsc_mgr_hw_vote_enable(&clock_handle,&dev));digital_cdc_rsc_mgr_hw_vote_reset(&clock_handle);digital_cdc_rsc_mgr_hw_vote_disable(&clock_handle,&dev);}return NULL;}
static unsigned concurrent(void){clock_handle.external=1;digital_cdc_rsc_mgr_init();pthread_t t[4];for(int i=0;i<4;i++)assert(!pthread_create(&t[i],NULL,vote_thread,NULL));for(int i=0;i<4;i++)assert(!pthread_join(t[i],NULL));digital_cdc_rsc_mgr_exit();assert(!clock_handle.owned&&clock_handle.external==1&&!live);return 4000;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;if(!strcmp(argv[1],"votes"))n=votes();else if(!strcmp(argv[1],"restoration"))n=restoration();else if(!strcmp(argv[1],"faults"))n=faults();else if(!strcmp(argv[1],"shutdown"))n=shutdown();else if(!strcmp(argv[1],"concurrent"))n=concurrent();else abort();printf("{\"digital_votes_%s_cases\":%u}\n",argv[1],n);}
