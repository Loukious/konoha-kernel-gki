#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#define EPROBE_DEFER 517
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define QCOM_IOMMU_FAULT_MODEL_NON_FATAL 1
#define KGSL_MMU_DEVICE(m) ((m)->device)
#define dev_err(...) ((void)0)
typedef int (*iommu_fault_handler_t)(void);
struct device_node { int refs; };
struct device { void *data; int refs; };
struct platform_device { struct device dev; };
struct iommu_domain { int attached; };
struct kgsl_device { struct platform_device *pdev; };
struct kgsl_mmu { struct kgsl_device *device; };
struct kgsl_iommu_context {
 struct platform_device *pdev;
 struct iommu_domain *domain;
 const char *name;
 int cb_num,ratelimit,adreno_smmu;
 struct kgsl_device *kgsldev;
};
static int fault,domains,attachs,detaches,configures;
static struct device_node node;
static struct platform_device pdev;
static struct device_node *of_get_child_by_name(struct device_node *p,const char *n) { if(fault==1)return NULL;node.refs++;return &node; }
static void of_node_put(struct device_node *p) { assert(p&&p->refs);p->refs--; }
static struct platform_device *of_find_device_by_node(struct device_node *n) { if(fault==2)return NULL;pdev.dev.refs++;return &pdev; }
static void put_device(struct device *d) { assert(d->refs>0);d->refs--; }
static int of_dma_configure(struct device *d,struct device_node *n,bool b) {configures++;return fault==3?-EINVAL:0;}
static void ratelimit_default_init(int *p) { *p=1; }
static void dev_set_drvdata(struct device *d,void *p) { d->data=p; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static struct iommu_domain *iommu_paging_domain_alloc(struct device *d) {
 if(fault==4)return ERR_PTR(-ENOMEM);domains++;return calloc(1,sizeof(struct iommu_domain));
}
static void qcom_iommu_set_fault_model(struct iommu_domain *d,int f) { assert(d&&f==1); }
static void _enable_gpuhtw_llc(struct kgsl_mmu *m,struct iommu_domain *d) { assert(d); }
static int iommu_attach_device(struct iommu_domain *d,struct device *v) { if(fault==5)return -EBUSY;assert(!d->attached);d->attached=1;attachs++;return 0; }
static void iommu_detach_device(struct iommu_domain *d,struct device *v) { assert(d->attached);d->attached=0;detaches++; }
static void iommu_domain_free(struct iommu_domain *d) { assert(d&&!d->attached);free(d);domains--; }
static void iommu_set_fault_handler(struct iommu_domain *d,iommu_fault_handler_t h,void *m) { assert(d->attached); }
static int qcom_iommu_get_context_bank_nr(struct iommu_domain *d) { return fault==6?-EINVAL:7; }
/* STAGED_DEFINITIONS */
int main(void) {
 unsigned cases=0;
 for(unsigned round=0;round<200;round++)for(fault=0;fault<=6;fault++) {
  struct kgsl_device device={&pdev};struct kgsl_mmu mmu={&device};struct kgsl_iommu_context ctx={0};
  int before=attachs;
  int ret=kgsl_iommu_setup_context(&mmu,&node,&ctx,"gfx3d_user",NULL);
  if(!fault){assert(!ret);assert(ctx.cb_num==7&&domains==1&&pdev.dev.refs==1);}
  else {assert(ret<0);assert(!ctx.domain&&!ctx.pdev&&!pdev.dev.refs&&!domains);}
  kgsl_iommu_detach_context(&ctx);kgsl_iommu_detach_context(&ctx);
  assert(!domains&&!pdev.dev.refs&&!pdev.dev.data&&!node.refs);assert(attachs==detaches);
  assert((attachs>before)==(!fault||fault==6));cases++;
 }
 printf("{\"iommu_failure_cleanup_cases\":%u,\"successful_attachments\":%d}\n",cases,attachs);
}
