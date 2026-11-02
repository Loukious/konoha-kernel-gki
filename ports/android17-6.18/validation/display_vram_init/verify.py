#!/usr/bin/env python3
"""Exercise VRAM selection at the actual driver initialization checkpoint."""
import hashlib,json,subprocess
from pathlib import Path
H=Path(__file__).resolve().parent;W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
def fn(s,sig):
 start=s.index(sig);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
a=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define EPROBE_DEFER 517
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x) || IS_ERR(x))
#define DRM_INFO(...) ((void)0)
#define DISP_DEV_ERR(...) ((void)0)
#define DRM_ERROR(...) ((void)0)
#define INIT_LIST_HEAD(x) ((void)(x))
#define BLOCKING_INIT_NOTIFIER_HEAD(x) ((void)(x))
#define mutex_init(x) ((void)(x))
#define init_waitqueue_head(x) ((void)(x))
#define spin_lock_init(x) ((void)(x))
#define MSM_SMMU_DOMAIN_UNSECURE 0
#define MSM_COMP_OBJECT_CREATED 0
#define PAGE_SHIFT 12
#define DMA_ATTR_NO_KERNEL_MAPPING 1
#define DMA_ATTR_WRITE_COMBINE 2
#define GFP_KERNEL 0
#define dev_info(...) ((void)0)
typedef uint32_t u32;
struct device_node { int unused; };
struct device { struct device_node *of_node; };
struct msm_kms { int ready; };
struct msm_drm_private { struct { unsigned long size,paddr; int mm,lock; } vram;
 struct msm_kms *kms; void *wq; int pending_crtcs_event,client_event_list,inactive_list,
 vm_client_list,fence_error_client_list,component_notifier_list,mm_lock,vm_client_lock,fence_error_client_lock; };
struct drm_device { struct device *dev; void *dev_private; struct {void *funcs,*helper_private;} mode_config; };
struct platform_device { struct device dev; struct drm_device *ddev; };
struct drm_crtc { int unused; };
struct resource { unsigned long start,end; };
static bool has_iommu,explicit_region,explicit_size,alloc_fail;
static int bind_error,kms_error,region_error,allocs,frees,queries_before_kms;
static struct msm_kms kms;
static unsigned long configured_size;
static int mode_config_funcs,mode_config_helper_funcs;
static char *vram="16m";
static struct device_node region;
static void *to_platform_device(struct device *d) {return (struct platform_device *)d;}
static struct drm_device *platform_get_drvdata(struct platform_device *p) {return p->ddev;}
static int msm_mdss_init(struct drm_device *d) {(void)d;return 0;}
static void msm_mdss_destroy(struct drm_device *d) {(void)d;}
static void *alloc_ordered_workqueue(const char *s,int f) {(void)s;(void)f;return (void *)1;}
static void destroy_workqueue(void *p) {(void)p;}
static int msm_component_bind_all(struct device *d,struct drm_device *v) {(void)d;(void)v;return bind_error;}
static struct device_node *of_parse_phandle(struct device_node *n,const char *s,int i) {(void)n;(void)s;(void)i;return explicit_region?&region:NULL;}
static int of_address_to_resource(struct device_node *n,int i,struct resource *r) {(void)n;(void)i;r->start=0x10000000;r->end=r->start+0x2000000;return region_error;}
static void of_node_put(struct device_node *n) {(void)n;}
static void *msm_gem_smmu_address_space_get(struct drm_device *d,int domain) {
 (void)domain;struct msm_drm_private *p=d->dev_private;
 if(!p->kms || !p->kms->ready){queries_before_kms++;return ERR_PTR(-EINVAL);}
 return has_iommu?(void *)1:NULL;
}
static int of_property_read_u32(struct device_node *n,const char *s,u32 *v) {(void)n;(void)s;*v=configured_size;return explicit_size?0:-EINVAL;}
static unsigned long memparse(const char *s,void *p) {(void)s;(void)p;return 16UL<<20;}
static void drm_mm_init(int *mm,unsigned long a,unsigned long b) {(void)a;(void)b;*mm=1;}
static void *dma_alloc_attrs(struct device *d,unsigned long size,unsigned long *addr,int flags,unsigned long attrs) {
 (void)d;(void)size;(void)flags;(void)attrs;allocs++;*addr=alloc_fail?0:0x10000000;return alloc_fail?NULL:(void *)1;
}
static struct msm_kms *_msm_drm_component_init_helper(struct msm_drm_private *p,struct drm_device *d,struct device *v,struct platform_device *f) {
 (void)d;(void)v;(void)f;if(kms_error)return ERR_PTR(kms_error);kms.ready=1;p->kms=&kms;return &kms;
}
static void msm_drm_uninit(struct device *d) {struct msm_drm_private *p=platform_get_drvdata(to_platform_device(d))->dev_private;
 if(p->vram.paddr)frees++;p->vram.paddr=0;p->kms=NULL;
}
static void sde_dbg_destroy(void) {}
static void sde_power_resource_deinit(struct platform_device *p,void *x) {(void)p;(void)x;}
static void drm_dev_put(struct drm_device *d) {(void)d;}
'''
t=r'''
int main(void){unsigned count=0;
 for(unsigned bits=0;bits<64;bits++)for(unsigned error=0;error<4;error++){
 has_iommu=bits&1;explicit_region=bits&2;explicit_size=bits&4;alloc_fail=bits&8;
 configured_size=(bits&16)?0:(32UL<<20);region_error=(bits&32)?-EINVAL:0;
 bind_error=error==1?-EPROBE_DEFER:0;kms_error=error==2?-EIO:0;if(error==3)bind_error=-EINVAL;
 allocs=frees=queries_before_kms=0;kms.ready=0;
 struct msm_drm_private p={0};struct device_node n={0};struct platform_device dev={.dev.of_node=&n};
 struct drm_device d={.dev=&dev.dev,.dev_private=&p};dev.ddev=&d;
 int ret=msm_drm_component_init(&dev.dev);
 if(bind_error)assert(ret==bind_error && allocs==0);
 else if(kms_error)assert(ret==kms_error && allocs==0);
 else if(explicit_region && region_error)assert(ret==region_error && allocs==0);
 else {
  unsigned long expected=explicit_region?(32UL<<20):has_iommu?0:explicit_size?configured_size:(16UL<<20);
  assert(p.vram.size==expected);assert(allocs==(expected!=0));
  assert(ret==(expected && alloc_fail?-ENOMEM:0));
  if(ret==0){msm_drm_uninit(&dev.dev);assert(frees==(expected!=0));}
 }
 assert(queries_before_kms==0);count++;
 }
 printf("PASS: %u IOMMU/carveout/fallback/zero/DT/bind/KMS/allocation/error-cleanup cases\n",count);
}
'''
results={}
for variant,suffix in [('candidate',''),('original','.before')]:
 s=(H/('qcom/opensource/display-drivers/msm/msm_drv.c'+suffix)).read_text()
 select=fn(s,'static int msm_init_vram(')
 init=fn(s,'static int msm_drm_component_init(struct device *dev)')
 prefix=init[:init.index('\tmsm_drm_notify_components(')]
 # Use unchanged actual init sequence through selection; other device setup
 # is outside this harness. Keep the production fail/bind/MDSS labels.
 labels=init[init.index('\nfail:'):]
 labels=labels.replace('\tsde_power_resource_deinit(pdev, &priv->phandle);','')
 source=a+select+'\n'+prefix+'\treturn 0;'+labels+t
 p=H/(variant+'-test.c');p.write_text(source);exe=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-misleading-indentation','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);results[variant]={'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr[:3000]}
 assert (r.returncode==0)==(variant=='candidate'),results
(H/'validation.json').write_text(json.dumps({'status':'PASS','cases':256,'scope':'Actual msm_init_vram and msm_drm_component_init prefix/failure labels with host resource/MMU adapters; hardware pending','results':results,'candidate_sha256':hashlib.sha256((H/'qcom/opensource/display-drivers/msm/msm_drv.c').read_bytes()).hexdigest()},indent=2)+'\n')
print(results['candidate']['stdout']);print('PASS: original premature query rejected')
