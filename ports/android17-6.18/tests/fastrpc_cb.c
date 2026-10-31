#include "fastrpc_services.h"
struct mutex {int held;};
static void mutex_init(struct mutex *m){m->held=0;}
static void mutex_lock(struct mutex *m){assert(!m->held);m->held=1;}
static void mutex_unlock(struct mutex *m){assert(m->held);m->held=0;}
struct timespec64 {long a,b;};
typedef u64 dma_addr_t;
struct node {u32 pd,sessions,sid,range[2],gen[2];bool has_pd,has_sessions,has_range,has_gen,secure;};
struct action {void (*fn)(void *);void *arg;};
struct device {struct node *of_node;void *data;struct action actions[16];int na;void *memory;};
struct platform_device {struct device dev;};
struct iommu_domain {int unused;};struct sg_table {void *sgl;int orig_nents;};
struct gen_pool {struct device *dev;};
#include "cb_types.inc"
struct fastrpc_channel_ctx {struct mutex session_mutex;spinlock_t lock;atomic_t teardown;struct fastrpc_pool_ctx session[FASTRPC_MAX_SESSIONS];int sesscount;bool pd_type,smmucb_pool;};
static struct fastrpc_channel_ctx channel;
static int faults,step,live_sg,live_maps,live_pools,unmaps,root_frees,map_mode;
static struct iommu_domain domain;
static bool fault(void){return ++step==faults;}
static void push(struct device *d,void (*fn)(void *),void *arg){assert(d->na<16);d->actions[d->na++]=(struct action){fn,arg};}
static void *devres_open_group(struct device *d,void *p,int g){(void)p;(void)g;assert(!d->na);return fault()?NULL:d;}
static void devres_close_group(struct device *d,void *g){assert(g==d);}
static void devres_release_group(struct device *d,void *g){assert(g==d);while(d->na){struct action a=d->actions[--d->na];a.fn(a.arg);}}
static void *devm_kzalloc(struct device *d,size_t n,int g){if(fault())return NULL;void *p=kzalloc(n,g);push(d,kfree,p);return p;}
static int devm_add_action_or_reset(struct device *d,void (*fn)(void *),void *arg){if(fault()){fn(arg);return -ENOMEM;}push(d,fn,arg);return 0;}
static void *dma_alloc_coherent(struct device *d,size_t n,dma_addr_t *p,int g){(void)g;if(fault())return NULL;*p=0;d->memory=kzalloc(n,0);return d->memory;}
static void dma_free_coherent(struct device *d,size_t n,void *p,dma_addr_t addr){(void)n;assert(addr==0 && d->memory==p && !live_maps && !live_pools);kfree(p);d->memory=NULL;}
static int dma_get_sgtable(struct device *d,struct sg_table *s,void *v,dma_addr_t p,size_t n){(void)d;(void)v;(void)p;(void)n;if(fault())return -EIO;s->sgl=(void *)1;s->orig_nents=1;live_sg++;return 0;}
static void sg_free_table(struct sg_table *s){assert(s->sgl && live_sg);live_sg--;}
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d){(void)d;return fault()?NULL:&domain;}
#define IOMMU_READ 1
#define IOMMU_WRITE 2
#define IOMMU_CACHE 4
#define NUMA_NO_NODE -1
#define PAGE_ALIGNED(x) (!((x)&4095))
#define DMA_BIT_MASK(n) ((1ULL<<(n))-1)
static ssize_t iommu_map_sg(struct iommu_domain *d,unsigned long addr,void *sg,int n,int prot,int g){(void)addr;(void)sg;(void)prot;(void)g;assert(d==&domain && n==1);if(fault() || map_mode==1)return -EIO;if(map_mode==2)return 0;live_maps++;return map_mode==3?4096:8192;}
static size_t iommu_unmap(struct iommu_domain *d,unsigned long iova,size_t size){assert(d==&domain && iova==0x100000 && (size==8192 || size==4096) && live_maps);live_maps--;unmaps++;return size;}
static void pool_release(void *p){struct gen_pool *pool=p;assert(pool->dev->memory && live_pools);live_pools--;kfree(pool);}
static struct gen_pool *devm_gen_pool_create(struct device *d,int order,int numa,void *name){(void)order;(void)numa;(void)name;if(fault())return ERR_PTR(-ENOMEM);struct gen_pool *p=kzalloc(sizeof(*p),0);p->dev=d;live_pools++;push(d,pool_release,p);return p;}
static int gen_pool_add_virt(struct gen_pool *p,unsigned long virt,u64 phys,size_t size,int numa){(void)numa;assert(p->dev->memory==(void *)virt && !phys && size==8192);return fault()?-EIO:0;}
static struct fastrpc_channel_ctx *get_current_channel_ctx(struct device *d){(void)d;return &channel;}
static int dma_set_mask_and_coherent(struct device *d,u64 mask){(void)d;assert(mask==0xffffffff);return fault()?-EIO:0;}
static bool of_property_present(struct node *n,const char *p){if(!strcmp(p,"pd-type"))return n->has_pd;if(!strcmp(p,"qcom,nsessions"))return n->has_sessions;if(!strcmp(p,"alloc-size-range"))return n->has_range;if(!strcmp(p,"qrtr-gen-pool"))return n->has_gen;abort();}
static bool of_property_read_bool(struct node *n,const char *p){assert(!strcmp(p,"qcom,secure-context-bank"));return n->secure;}
static int of_property_read_u32(struct node *n,const char *p,u32 *out){if(!strcmp(p,"pd-type"))*out=n->pd;else if(!strcmp(p,"qcom,nsessions"))*out=n->sessions;else if(!strcmp(p,"reg"))*out=n->sid;else abort();return 0;}
static int of_property_read_u32_array(struct node *n,const char *p,u32 *out,size_t count){assert(count==2);if(!strcmp(p,"alloc-size-range"))memcpy(out,n->range,8);else if(!strcmp(p,"frpc-gen-addr-pool"))memcpy(out,n->gen,8);else abort();return 0;}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static void *dev_get_drvdata(struct device *d){return d->data;}
#define dev_info(...) ((void)0)
static void fastrpc_rootheap_buf_list_free(struct fastrpc_channel_ctx *c){assert(c==&channel);root_frees++;}
#include "cb.inc"
static struct node node(void){return (struct node){.pd=ROOT_PD,.sid=7,.has_pd=true,.has_range=true,.has_gen=true,.range={4096,0x100000},.gen={0x100000,8192}};}
static void clean(void){assert(!live_sg && !live_maps && !live_pools && !atomic_load(&allocations));channel.sesscount=0;channel.pd_type=channel.smmucb_pool=false;memset(channel.session,0,sizeof(channel.session));faults=step=map_mode=unmaps=0;atomic_set(&channel.teardown,0);}
int main(void){spin_lock_init(&channel.lock);mutex_init(&channel.session_mutex);int cases=0;
 for(int f=1;f<=10;f++){clean();struct node n=node();struct platform_device p={.dev.of_node=&n};faults=f;int r=fastrpc_cb_probe(&p);assert(r<0 && !channel.sesscount && !p.dev.data);assert(!p.dev.na);clean();cases++;}
 for(int m=1;m<=3;m++){clean();map_mode=m;struct node n=node();struct platform_device p={.dev.of_node=&n};assert(fastrpc_cb_probe(&p)<0 && !channel.sesscount);assert(unmaps==(m==3));clean();cases++;}
 clean();struct node n=node();struct platform_device p={.dev.of_node=&n};assert(!fastrpc_cb_probe(&p));assert(channel.sesscount==1 && channel.session[0].smmucb[0].minallocsize==4096 && channel.session[0].smmucb[0].maxallocsize==0x100000);fastrpc_cb_remove(&p);assert(!p.dev.na && !p.dev.data && !channel.session[0].smmucb[0].valid);clean();cases++;
 n=node();n.has_gen=false;n.has_sessions=true;n.sessions=FASTRPC_MAX_SESSIONS;p=(struct platform_device){.dev.of_node=&n};assert(!fastrpc_cb_probe(&p));assert(channel.sesscount==FASTRPC_MAX_SESSIONS);for(int i=0;i<channel.sesscount;i++)assert(channel.session[i].smmucb[0].sess==&channel.session[i]);fastrpc_cb_remove(&p);for(int i=0;i<channel.sesscount;i++)assert(!channel.session[i].smmucb[0].valid);clean();cases++;
 struct platform_device devices[FASTRPC_MAX_CB_POOL+1]={0};n=node();n.has_gen=false;n.pd=USER_UNSIGNEDPD_POOL;for(int i=0;i<FASTRPC_MAX_CB_POOL+1;i++){devices[i].dev.of_node=&n;int r=fastrpc_cb_probe(&devices[i]);assert(i==FASTRPC_MAX_CB_POOL?r==-ENOSPC:!r);}assert(channel.sesscount==1 && channel.session[0].smmucount==FASTRPC_MAX_CB_POOL);for(int i=0;i<FASTRPC_MAX_CB_POOL;i++){fastrpc_cb_remove(&devices[i]);for(int j=i+1;j<FASTRPC_MAX_CB_POOL;j++)assert(channel.session[0].smmucb[j].valid);}clean();cases++;
 n=node();n.gen[1]=0;p=(struct platform_device){.dev.of_node=&n};assert(fastrpc_cb_probe(&p)==-EINVAL);clean();cases++;
 n=node();n.has_gen=false;n.sessions=FASTRPC_MAX_SESSIONS+1;n.has_sessions=true;p=(struct platform_device){.dev.of_node=&n};assert(fastrpc_cb_probe(&p)==-ENOSPC);clean();cases++;
 n=node();n.has_gen=false;atomic_set(&channel.teardown,1);p=(struct platform_device){.dev.of_node=&n};assert(fastrpc_cb_probe(&p)==-EPIPE);clean();cases++;
 printf("{\"context_bank_probe_fault_scenarios\":%d,\"zero_dma_address\":true,\"pooled_sibling_removal\":true,\"managed_pool_release_order\":true}\n",cases);
}
