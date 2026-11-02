#!/usr/bin/env python3
"""Run exact proposed display/EVA callers with host kernel-service adapters."""
from pathlib import Path
import re, subprocess, json, sys, hashlib

ROOT=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
PATCHED=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/tmp/onyx-iommu-patched')

def function(path,name):
    text=path.read_text()
    start=re.search(r'^(?:static )?(?:int|void|bool) '+name+r'\(',text,re.M).start()
    brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

dispatch=Path('/tmp/onyx-iommu-fault-dispatch-test.c').read_text().split('struct token {')[0]
dispatch=dispatch.replace('struct device { int id; };', '''
struct device_node { const char *label, *compat; int buffer_error; };
struct allocation { void *ptr; struct allocation *next; };
struct device { int id; struct device *parent; struct device_node *of_node; const void *bus;
    void *dma_parms, *drvdata; struct arm_smmu_domain *smmu; struct allocation *allocations; int kobj;
};''')
dispatch=dispatch.replace('#include <unistd.h>','#include <unistd.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stddef.h>')
adapter=r'''
#define GFP_KERNEL 0
#define DMA_BIT_MASK(n) (~0ULL >> (64-(n)))
#define ERR_PTR(err) ((void *)(intptr_t)(err))
#define PTR_ERR(ptr) ((int)(intptr_t)(ptr))
#define IS_ERR(ptr) ((uintptr_t)(ptr) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))
#define dprintk(...) ((void)0)
#define DRM_INFO(...) ((void)0)
#define pr_err(...) ((void)0)
#define DISP_DEV_ERR(...) ((void)0)
#define CVP_ERR 1
#define CVP_CORE 2
#define mutex_destroy pthread_mutex_destroy
#define DEFINE_MUTEX(name) pthread_mutex_t name=PTHREAD_MUTEX_INITIALIZER
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(name) struct list_head name={ &name, &name }
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
static void INIT_LIST_HEAD(struct list_head *l) { l->next=l->prev=l; }
static bool list_empty(struct list_head *l) { return l->next==l; }
static void insert(struct list_head *e,struct list_head *p,struct list_head *n) { e->prev=p;e->next=n;p->next=e;n->prev=e; }
static void list_add(struct list_head *e,struct list_head *l) { insert(e,l,l->next); }
static void list_add_tail(struct list_head *e,struct list_head *l) { insert(e,l->prev,l); }
static void list_del(struct list_head *e) { assert(e->next && e->prev);e->next->prev=e->prev;e->prev->next=e->next;e->next=e->prev=NULL; }
#define list_entry(ptr,type,member) container_of(ptr,type,member)
#define list_for_each_entry(pos,head,member) \
    for(pos=list_entry((head)->next,__typeof__(*pos),member); &pos->member!=(head); pos=list_entry(pos->member.next,__typeof__(*pos),member))
#define list_for_each_entry_safe(pos,nextpos,head,member) \
    for(pos=list_entry((head)->next,__typeof__(*pos),member), nextpos=list_entry(pos->member.next,__typeof__(*pos),member); \
        &pos->member!=(head); pos=nextpos,nextpos=list_entry(nextpos->member.next,__typeof__(*nextpos),member))
struct platform_device { struct device dev; bool component; };
#define to_platform_device(dev_ptr) container_of(dev_ptr,struct platform_device,dev)
struct msm_mmu { int unused; };
struct msm_smmu { struct msm_mmu base; struct device *client_dev; };
#define to_msm_smmu(mmu) container_of(mmu,struct msm_smmu,base)
struct msm_smmu_domain { const char *label; bool secure; };
struct msm_smmu_client { struct device *dev;struct iommu_domain *domain;const char *compat;bool secure,domain_attached;struct list_head smmu_list; };
struct of_device_id { const char *compatible;const void *data; };
static const struct msm_smmu_domain display_domain={ .label="display", .secure=false };
static const struct of_device_id msm_smmu_dt_match[]={ { .compatible="qcom,smmu_sde_unsec", .data=&display_domain } };
static int msm_smmu_comp_ops;
static DEFINE_MUTEX(smmu_list_lock);
static LIST_HEAD(sde_smmu_list);
struct addr_range { u32 start,size; };
struct context_bank_info { struct list_head list;const char *name;u32 buffer_type;bool is_secure;struct addr_range addr_range;struct device *dev;struct iommu_domain *domain; };
struct msm_cvp_platform_resources { struct list_head context_banks; };
struct completion { bool done;pthread_mutex_t lock;pthread_cond_t cond; };
struct work_struct { int unused; };
struct msm_cvp_core { bool ready,dsp_initialized;int users,hfi_type,dev_num;struct msm_cvp_platform_resources resources;
    struct completion users_zero;void *dev_ops,*capabilities,*cdev,*class;struct device *dev;struct work_struct ssr_work;
    int sess_idr;pthread_mutex_t idr_mtx,clk_lock,lock; };
struct msm_cvp_drv { pthread_mutex_t lock;struct msm_cvp_core *cvp_core;void *debugfs_root; };
static struct msm_cvp_drv cvp_drv={ .lock=PTHREAD_MUTEX_INITIALIZER };
static struct msm_cvp_drv *cvp_driver=&cvp_drv;
static int msm_cvp_core_attr_group;
static struct arm_smmu_domain domains[4];
static struct platform_device *children[4];static int child_count;
static struct msm_cvp_core *live_core;
static int component_add_error,component_adds,component_dels,platform_unregisters;
static int allocation_count,allocation_fail_at;
static bool inside_component_del,core_freed;
static struct msm_smmu *master_wrapper;
static pthread_mutex_t callback_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_entered_cond=PTHREAD_COND_INITIALIZER,callback_release_cond=PTHREAD_COND_INITIALIZER;
static bool callback_wait,callback_entered,callback_released;
static atomic_bool removal_done;
static bool cvp_get_core(struct msm_cvp_core *);
static void cvp_put_core(struct msm_cvp_core *);
static void msm_smmu_remove(struct platform_device *);
static void msm_smmu_destroy(struct msm_mmu *);
static void msm_cvp_remove(struct platform_device *);
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d) { return d->smmu ? &d->smmu->domain : NULL; }
static int of_property_read_string(struct device_node *np,const char *key,const char **out) { assert(!strcmp(key,"label"));*out=np->label;return np->label?0:-ENOENT; }
static int of_property_read_u32(struct device_node *np,const char *key,u32 *out) { assert(!strcmp(key,"buffer-types"));*out=7;return np->buffer_error; }
static bool of_property_read_bool(struct device_node *np,const char *key) { (void)np;(void)key;return false; }
static bool of_property_present(struct device_node *np,const char *key) { (void)np;(void)key;return false; }
static bool of_device_is_compatible(struct device_node *np,const char *compat) { return !strcmp(np->compat,compat); }
static const struct of_device_id *of_match_device(const struct of_device_id *match,struct device *d) { (void)d;return match; }
static void *dev_get_drvdata(struct device *d) { return d->drvdata; }
static void dev_set_drvdata(struct device *d,void *data) { d->drvdata=data; }
static void *platform_get_drvdata(struct platform_device *p) { return p->dev.drvdata; }
static void platform_set_drvdata(struct platform_device *p,void *data) { p->dev.drvdata=data; }
static void *devm_kzalloc(struct device *d,size_t size,int flags) {
    (void)flags;if(++allocation_count==allocation_fail_at)return NULL;
    void *p=calloc(1,size);assert(p);struct allocation *a=malloc(sizeof(*a));assert(a);a->ptr=p;a->next=d->allocations;d->allocations=a;return p;
}
static void devres_release(struct device *d) {
    while(d->allocations) { struct allocation *a=d->allocations;d->allocations=a->next;
        for(int i=0;i<4;i++)assert(domains[i].fault_token!=a->ptr);
        free(a->ptr);free(a);
    } d->dma_parms=NULL;
}
static void host_kfree(void *p) {
    if(p==live_core) { assert(child_count==0 && list_empty(&live_core->resources.context_banks));
        for(int i=0;i<4;i++) { assert(domains[i].fault_token!=p); }
        core_freed=true;live_core=NULL;
    } free(p);
}
#define kfree host_kfree
static int dma_set_max_seg_size(struct device *d,unsigned long mask) { (void)d;(void)mask;return 0; }
static int dma_set_seg_boundary(struct device *d,unsigned long mask) { (void)d;(void)mask;return 0; }
static int dev_err_probe(struct device *d,int error,const char *message) { (void)d;(void)message;return error; }
static int component_add(struct device *d,int *ops) { (void)ops;if(component_add_error)return component_add_error;to_platform_device(d)->component=true;component_adds++;return 0; }
static void component_del(struct device *d,int *ops) {
    (void)ops;assert(!inside_component_del);inside_component_del=true;
    struct platform_device *p=to_platform_device(d);assert(p->component);p->component=false;component_dels++;
    if(master_wrapper) { struct msm_smmu *s=master_wrapper;master_wrapper=NULL;msm_smmu_destroy(&s->base); }
    inside_component_del=false;
}
static void platform_device_unregister(struct platform_device *p) {
    assert(!inside_component_del);platform_unregisters++;msm_smmu_remove(p);devres_release(&p->dev);
}
static int msm_smmu_fault_handler(struct iommu_domain *domain,struct device *dev,unsigned long iova,int flags,void *token) {
    (void)domain;(void)dev;(void)iova;(void)flags;assert(token);
    if(callback_wait) {
        pthread_mutex_lock(&callback_gate);callback_entered=true;pthread_cond_broadcast(&callback_entered_cond);
        while(!callback_released)pthread_cond_wait(&callback_release_cond,&callback_gate);
        pthread_mutex_unlock(&callback_gate);
    }
    assert(((struct msm_smmu_client *)token)->dev);return -EINVAL;
}
#define refcount_inc(ptr) (++*(ptr))
#define refcount_dec_and_test(ptr) (--*(ptr)==0)
static void complete(struct completion *c) { pthread_mutex_lock(&c->lock);c->done=true;pthread_cond_broadcast(&c->cond);pthread_mutex_unlock(&c->lock); }
static void wait_for_completion(struct completion *c) { pthread_mutex_lock(&c->lock);while(!c->done)pthread_cond_wait(&c->cond,&c->lock);pthread_mutex_unlock(&c->lock); }
static int msm_cvp_smmu_fault_handler(struct iommu_domain *domain,struct device *dev,unsigned long iova,int flags,void *token) {
    (void)domain;(void)dev;(void)iova;(void)flags;assert(token==live_core);
    struct msm_cvp_core *core=token;if(!cvp_get_core(core))return -EINVAL;
    if(callback_wait) {
        pthread_mutex_lock(&callback_gate);callback_entered=true;pthread_cond_broadcast(&callback_entered_cond);
        while(!callback_released)pthread_cond_wait(&callback_release_cond,&callback_gate);
        pthread_mutex_unlock(&callback_gate);
    }
    assert(core==live_core);cvp_put_core(core);return -ENOSYS;
}
static void of_platform_depopulate(struct device *parent) {
    while(child_count) { struct platform_device *p=children[--child_count];assert(p->dev.parent==parent);msm_cvp_remove(p);devres_release(&p->dev); }
}
#define cvp_dsp_device_exit() ((void)0)
#define cvp_dsp_device_deinit() ((void)0)
#define debugfs_remove_recursive(ptr) ((void)0)
#define cancel_work_sync(ptr) ((void)0)
static void cvp_hfi_deinitialize(int hfi_type,void *ops) { (void)hfi_type;(void)ops;assert(live_core && !live_core->ready && live_core->users==0); }
static void msm_cvp_free_platform_resources(struct msm_cvp_platform_resources *r) { assert(list_empty(&r->context_banks)); }
#define idr_destroy(ptr) ((void)0)
#define sysfs_remove_group(ptr,group) ((void)(group))
#define cdev_del(ptr) ((void)0)
#define device_destroy(ptr,dev_num) ((void)0)
#define class_destroy(ptr) ((void)0)
#define unregister_chrdev_region(dev_num,count) ((void)0)
'''
display=PATCHED/'qcom/opensource/display-drivers/msm/msm_smmu.c'
eva=PATCHED/'qcom/opensource/eva-kernel/msm/eva/msm_cvp_res_parse.c'
cvp=PATCHED/'qcom/opensource/eva-kernel/msm/eva/cvp.c'
actual='\n\n'.join([
    function(display,'msm_smmu_destroy'),function(display,'msm_smmu_probe'),function(display,'msm_smmu_remove'),
    function(eva,'msm_cvp_setup_context_bank'),function(eva,'msm_cvp_populate_context_bank'),
    function(ROOT/'onyx/qcom/opensource/eva-kernel/msm/eva/msm_cvp_core.c','cvp_get_core'),
    function(ROOT/'onyx/qcom/opensource/eva-kernel/msm/eva/msm_cvp_core.c','cvp_put_core'),
    function(cvp,'cvp_release_core'),function(cvp,'msm_cvp_remove'),
])
tests=r'''
static struct device_node display_node={ .label="display",.compat="qcom,smmu_sde_unsec" };
static struct device_node core_node={ .label="core",.compat="qcom,msm-cvp" };
static struct device_node child_node={ .label="cvp_hlos",.compat="qcom,msm-cvp,context-bank" };
static void init_device(struct platform_device *p,int domain,struct device_node *node) {
    memset(p,0,sizeof(*p));p->dev.of_node=node;p->dev.bus=&domain_ops;p->dev.smmu=&domains[domain];
}
static struct msm_cvp_core *new_core(struct platform_device *parent) {
    struct msm_cvp_core *c=calloc(1,sizeof(*c));assert(c);c->ready=true;c->users=1;c->dev=&parent->dev;c->dev_ops=(void *)1;
    INIT_LIST_HEAD(&c->resources.context_banks);pthread_mutex_init(&c->lock,NULL);pthread_mutex_init(&c->clk_lock,NULL);pthread_mutex_init(&c->idr_mtx,NULL);
    pthread_mutex_init(&c->users_zero.lock,NULL);pthread_cond_init(&c->users_zero.cond,NULL);
    parent->dev.drvdata=c;live_core=cvp_driver->cvp_core=c;core_freed=false;return c;
}
static void finish_core(struct platform_device *parent) { msm_cvp_remove(parent);assert(core_freed && !parent->dev.drvdata && !cvp_driver->cvp_core); }
static void reset_gate(void) { callback_wait=true;callback_entered=callback_released=false;atomic_store(&removal_done,false); }
static void await_callback(void) { pthread_mutex_lock(&callback_gate);while(!callback_entered)pthread_cond_wait(&callback_entered_cond,&callback_gate);pthread_mutex_unlock(&callback_gate); }
static void release_callback(void) { pthread_mutex_lock(&callback_gate);callback_released=true;pthread_cond_broadcast(&callback_release_cond);pthread_mutex_unlock(&callback_gate); }
static void *display_fault_thread(void *dev_ptr) { struct device *dev=dev_ptr;struct arm_smmu_device hw={ .dev=dev };assert(report_iommu_fault_helper(dev->smmu,&hw,1)==-EINVAL);return NULL; }
static void *display_remove_thread(void *p_ptr) { struct platform_device *p=p_ptr;msm_smmu_remove(p);devres_release(&p->dev);atomic_store(&removal_done,true);return NULL; }
static void *cvp_fault_thread(void *dev_ptr) { struct device *dev=dev_ptr;struct arm_smmu_device hw={ .dev=dev };assert(report_iommu_fault_helper(dev->smmu,&hw,1)==-ENOSYS);return NULL; }
static void *cvp_remove_thread(void *p_ptr) { finish_core(p_ptr);atomic_store(&removal_done,true);return NULL; }
int main(void) {
    int markers[4];
    registered_ops.set_fault_handler=arm_smmu_set_fault_handler;
    for(int i=0;i<4;i++) { domains[i].domain.ops=&domain_ops;domains[i].domain.cookie_type=IOMMU_COOKIE_DMA_IOVA;domains[i].domain.iova_cookie=&markers[i];pthread_mutex_init(&domains[i].fault_handler_lock,NULL); }
    struct platform_device display_dev;
    init_device(&display_dev,0,&display_node);
    component_add_error=-EIO;
    assert(msm_smmu_probe(&display_dev)==-EIO);
    assert(list_empty(&sde_smmu_list) && !display_dev.dev.drvdata && !domains[0].fault_handler && !domains[0].fault_token);
    devres_release(&display_dev.dev);
    component_add_error=0;
    domains[0].domain.ops=NULL;
    assert(msm_smmu_probe(&display_dev)==-EINVAL && list_empty(&sde_smmu_list) && !display_dev.dev.drvdata);
    devres_release(&display_dev.dev);domains[0].domain.ops=&domain_ops;
    assert(msm_smmu_probe(&display_dev)==0 && display_dev.component && !list_empty(&sde_smmu_list));
    assert(domains[0].fault_token==display_dev.dev.drvdata);
    struct msm_smmu *wrapper=calloc(1,sizeof(*wrapper));assert(wrapper);wrapper->client_dev=&display_dev.dev;
    msm_smmu_destroy(&wrapper->base);
    assert(platform_unregisters==0 && display_dev.component && domains[0].fault_handler);
    /* Model component_del's aggregate unbind calling MMU wrapper destruction. */
    master_wrapper=calloc(1,sizeof(*master_wrapper));assert(master_wrapper);master_wrapper->client_dev=&display_dev.dev;
    pthread_t fault,remove;
    reset_gate();pthread_create(&fault,NULL,display_fault_thread,&display_dev.dev);await_callback();
    pthread_create(&remove,NULL,display_remove_thread,&display_dev);usleep(20000);assert(!atomic_load(&removal_done));
    release_callback();pthread_join(fault,NULL);pthread_join(remove,NULL);callback_wait=false;
    assert(!display_dev.component && list_empty(&sde_smmu_list) && !domains[0].fault_handler && !master_wrapper);
    assert(component_adds==1 && component_dels==1 && platform_unregisters==0);

    struct platform_device parent,child_a,child_b;
    init_device(&parent,1,&core_node);init_device(&child_a,1,&child_node);init_device(&child_b,1,&child_node);
    child_a.dev.parent=child_b.dev.parent=&parent.dev;
    struct msm_cvp_core *c=new_core(&parent);
    child_node.buffer_error=-EINVAL;
    assert(msm_cvp_populate_context_bank(&child_a.dev,c)==-ENOENT && list_empty(&c->resources.context_banks));devres_release(&child_a.dev);
    child_node.buffer_error=0;child_a.dev.smmu=NULL;
    assert(msm_cvp_populate_context_bank(&child_a.dev,c)==-ENODEV && list_empty(&c->resources.context_banks));devres_release(&child_a.dev);child_a.dev.smmu=&domains[1];
    child_a.dev.bus=NULL;
    assert(msm_cvp_populate_context_bank(&child_a.dev,c)==-ENODEV && list_empty(&c->resources.context_banks));devres_release(&child_a.dev);child_a.dev.bus=&domain_ops;
    allocation_fail_at=allocation_count+2;
    assert(msm_cvp_populate_context_bank(&child_a.dev,c)==-ENOMEM && list_empty(&c->resources.context_banks));devres_release(&child_a.dev);allocation_fail_at=0;
    domains[1].domain.ops=NULL;
    assert(msm_cvp_populate_context_bank(&child_a.dev,c)==-EINVAL && list_empty(&c->resources.context_banks) && !domains[1].fault_handler);devres_release(&child_a.dev);domains[1].domain.ops=&domain_ops;
    assert(msm_cvp_populate_context_bank(&child_a.dev,c)==0);
    assert(msm_cvp_populate_context_bank(&child_b.dev,c)==0);
    assert(domains[1].fault_token==c);
    msm_cvp_remove(&child_a);devres_release(&child_a.dev);
    assert(domains[1].fault_token==c && domains[1].fault_handler && !list_empty(&c->resources.context_banks));
    children[child_count++]=&child_b;
    reset_gate();pthread_create(&fault,NULL,cvp_fault_thread,&child_b.dev);await_callback();
    pthread_create(&remove,NULL,cvp_remove_thread,&parent);usleep(20000);assert(!atomic_load(&removal_done));
    release_callback();pthread_join(fault,NULL);pthread_join(remove,NULL);callback_wait=false;
    assert(!domains[1].fault_handler && !domains[1].fault_token);
    for(int i=0;i<4;i++) { assert(domains[i].domain.cookie_type==IOMMU_COOKIE_DMA_IOVA && domains[i].domain.iova_cookie==&markers[i]);pthread_mutex_destroy(&domains[i].fault_handler_lock); }
    puts("PASS: exact display probe failure/unwind/remove/MMU-wrapper ownership; exact EVA setup errors/shared-domain child removal/parent depopulation before core free");
    return 0;
}
'''
source=dispatch+'\n'+adapter+'\n'+actual+'\n'+tests
path=Path('/tmp/onyx-iommu-callers-test.c');path.write_text(source)
cmd=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-O1','-g','-pthread','-fsanitize=address,undefined',str(path),'-o','/tmp/onyx-iommu-callers-test']
subprocess.run(cmd,check=True)
run=subprocess.run(['/tmp/onyx-iommu-callers-test'],check=True,text=True,capture_output=True)
print(run.stdout,end='')
Path('/tmp/onyx-iommu-callers-validation.json').write_text(json.dumps({'status':'PASS','exact_proposed_functions':['msm_smmu_destroy','msm_smmu_probe','msm_smmu_remove','msm_cvp_setup_context_bank','msm_cvp_populate_context_bank','cvp_release_core','msm_cvp_remove'],'source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [display,eva,cvp]},'output':run.stdout,'compiler_flags':cmd[1:-3],'scope':'real extracted proposed callers and real private dispatch/setter; host device, devres, component, DT and completion adapters; not an in-kernel runtime test'},indent=2)+'\n')
