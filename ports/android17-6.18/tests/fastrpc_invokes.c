#include "fastrpc_services.h"
struct mutex {pthread_mutex_t lock;};
static void mutex_init(struct mutex *m){pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_ERRORCHECK);assert(!pthread_mutex_init(&m->lock,&a));pthread_mutexattr_destroy(&a);}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
#define atomic_cmpxchg(p,old,new) ({int expected=(old);atomic_compare_exchange_strong(p,&expected,new);expected;})
#define pr_err(...) ((void)0)
struct device {int unused;};struct device_driver {int unused;};struct dma_buf {int id;};
#include "invoke_types.inc"
struct fastrpc_channel_ctx {spinlock_t lock;atomic_t refs,teardown;u32 invoke_cnt;int ssr_wait_queue;};
struct fastrpc_user {struct fastrpc_channel_ctx *cctx;atomic_t dma_invokes;int dma_wait;struct mutex remote_map_mutex,map_mutex;};
struct fastrpc_map {atomic_t state;u32 flags;u64 phys,size,raddr;};
static struct fastrpc_map maps[2];
struct {spinlock_t glock;} g_frpc;
static int map_error,remote_error,map_puts,wakes;static bool block_rpc;
static struct completion entered[2],proceed[2];
static void fastrpc_channel_ctx_get(struct fastrpc_channel_ctx *c){atomic_inc(&c->refs);}
static void fastrpc_channel_ctx_put(struct fastrpc_channel_ctx *c){assert(atomic_read(&c->refs)>1);atomic_dec(&c->refs);}
static void wake_up_all(int *q){(void)q;wakes++;}
static void fastrpc_map_put(struct fastrpc_map *m){assert(m>=maps && m<maps+2);map_puts++;}
static int fastrpc_map_create(struct fastrpc_user *f,int fd,u64 va,struct dma_buf *b,u64 len,u32 attrs,int flags,struct fastrpc_map **out,bool ref){(void)f;(void)fd;(void)va;(void)len;(void)attrs;(void)flags;(void)ref;if(map_error)return map_error;*out=&maps[b->id];return 0;}
static int fastrpc_map_lookup(struct fastrpc_user *f,int fd,u64 va,u64 len,struct dma_buf *b,int flags,struct fastrpc_map **out,bool ref){return fastrpc_map_create(f,fd,va,b,len,0,flags,out,ref);}
static int fastrpc_mem_map_to_dsp(struct fastrpc_user *f,int fd,int off,u32 flags,u64 va,u64 phys,u64 size,uintptr_t *addr){(void)f;(void)fd;(void)off;(void)flags;(void)va;(void)size;if(block_rpc){assert(phys<2);complete_all(&entered[phys]);wait_for_completion(&proceed[phys]);}*addr=123;return remote_error;}
static int fastrpc_req_munmap_dsp(struct fastrpc_user *f,uintptr_t a,u64 size){(void)f;(void)a;(void)size;return remote_error;}
#include "invokes.inc"
static struct fastrpc_channel_ctx c;static struct fastrpc_user u;static struct fastrpc_device dev;
static void reset(void){assert(atomic_read(&c.refs)==1 && !c.invoke_cnt && !atomic_read(&u.dma_invokes));map_error=remote_error=map_puts=wakes=0;block_rpc=false;memset(maps,0,sizeof(maps));maps[0].phys=0;maps[1].phys=1;dev.dev_close=false;atomic_set(&c.teardown,0);}
static void *map_thread(void *p){assert(!fastrpc_dev_map_dma(&dev,(unsigned long)p));return NULL;}
int main(void){spin_lock_init(&g_frpc.glock);spin_lock_init(&c.lock);atomic_set(&c.refs,1);u.cctx=&c;mutex_init(&u.remote_map_mutex);mutex_init(&u.map_mutex);dev.fl=&u;struct dma_buf b[2]={{0},{1}};struct fastrpc_dev_map_dma p[2]={{.buf=&b[0],.size=4096},{.buf=&b[1],.size=4096}};struct fastrpc_dev_unmap_dma un={.buf=&b[0],.size=4096};int cases=0;
 for(int op=0;op<2;op++)for(int mode=0;mode<5;mode++){reset();unsigned long req=op?(unsigned long)&un:(unsigned long)&p[0];if(op)atomic_set(&maps[0].state,FD_DSP_MAP_COMPLETE);if(mode==0)atomic_set(&c.teardown,1);if(mode==1)map_error=-ENOMEM;if(mode==2)remote_error=-EIO;if(mode==3)dev.dev_close=true;if(mode==4)req=0;int r=op?fastrpc_dev_unmap_dma(&dev,req):fastrpc_dev_map_dma(&dev,req);assert(r<0);assert(!atomic_read(&u.dma_invokes) && !c.invoke_cnt && atomic_read(&c.refs)==1);mutex_lock(&u.remote_map_mutex);mutex_unlock(&u.remote_map_mutex);cases++;}
 reset();assert(!fastrpc_dev_map_dma(&dev,(unsigned long)&p[0]) && p[0].v_dsp_addr==123);assert(fastrpc_dev_map_dma(&dev,(unsigned long)&p[0])==-EALREADY);assert(!fastrpc_dev_unmap_dma(&dev,(unsigned long)&un));cases+=3;
 for(int i=0;i<2;i++){init_completion(&entered[i]);init_completion(&proceed[i]);}
 for(int trial=0;trial<100;trial++){reset();block_rpc=true;for(int i=0;i<2;i++){reinit_completion(&entered[i]);reinit_completion(&proceed[i]);}pthread_t t1,t2;pthread_create(&t1,NULL,map_thread,&p[0]);wait_for_completion(&entered[0]);pthread_create(&t2,NULL,map_thread,&p[1]);while(atomic_read(&u.dma_invokes)!=2)usleep(100);complete_all(&proceed[0]);wait_for_completion(&entered[1]);pthread_join(t1,NULL);assert(atomic_read(&u.dma_invokes)==1 && c.invoke_cnt==1);complete_all(&proceed[1]);pthread_join(t2,NULL);assert(!atomic_read(&u.dma_invokes) && !c.invoke_cnt && atomic_read(&c.refs)==1);}
 printf("{\"kernel_dma_error_scenarios\":%d,\"concurrent_dma_invocations\":100}\n",cases);
}
