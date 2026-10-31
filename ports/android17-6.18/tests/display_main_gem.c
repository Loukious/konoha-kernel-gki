#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint64_t ktime_t;
#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12
#define PAGE_ALIGN(n) ALIGN(n, PAGE_SIZE)
#define ALIGN(n,a) (((n) + (a) - 1) & ~((a) - 1))
#define DIV_ROUND_UP_ULL(n,d) (((uint64_t)(n) + (d) - 1) / (d))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define U32_MAX UINT32_MAX
#define MSM_BO_SCANOUT 1
#define MSM_BO_CACHED 2
#define MSM_BO_WC 4
#define MSM_BO_UNCACHED 8
#define MSM_BO_EXTBUF 16
#define MSM_PREP_WRITE 1
#define MSM_PREP_NOSYNC 2
#define DMA_BIDIRECTIONAL 0
#define LINUX_VERSION_CODE 61800
#define KERNEL_VERSION(a,b,c) ((a)*10000+(b)*100+(c))
#define IS_ERR(p) ((uintptr_t)(p) > (uintptr_t)-4096)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define ERR_CAST(p) ((void *)(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define DISP_DEV_ERR(...) ((void)0)
#define DRM_ERROR(...) ((void)0)
#define check_mul_overflow(a,b,r) __builtin_mul_overflow(a,b,r)
#define check_add_overflow(a,b,r) __builtin_add_overflow(a,b,r)
struct device {int dummy;};
struct drm_device {struct device *dev;};
struct page {int dummy;};
struct sg_table {void *sgl;int nents,orig_nents;};
struct dma_resv {int dummy;};
enum dma_resv_usage {DMA_RESV_USAGE_KERNEL,DMA_RESV_USAGE_WRITE,DMA_RESV_USAGE_READ};
struct drm_gem_object {struct drm_device *dev;uint64_t size;void *import_attach;};
struct msm_gem_object {struct drm_gem_object base;struct page **pages;struct sg_table *sgt;void *vram_node,*aspace;uint32_t flags;struct dma_resv *resv;};
struct drm_file {int dummy;};
struct drm_mode_create_dumb {u32 width,height,bpp,pitch,handle;u64 size;};
struct drm_mode_fb_cmd2 {u32 width,height,pitches[4],offsets[4];};
struct drm_format_info {unsigned hsub,vsub;unsigned cpp[4];};
static struct device fake_device;
static int page_fail,sg_fail,map_fail,page_live,sg_live,map_live;
static unsigned requested_size,requested_flags;
static enum dma_resv_usage waited_usage;
static long wait_result;
static unsigned long waited_ticks;
#define to_msm_bo(obj) ((struct msm_gem_object *)((char *)(obj)-offsetof(struct msm_gem_object,base)))
static bool use_pages(struct drm_gem_object *obj) {return !to_msm_bo(obj)->vram_node;}
static struct device *msm_gem_get_aspace_device(void *p) {return p?&fake_device:NULL;}
static struct page **drm_gem_get_pages(struct drm_gem_object *obj) {(void)obj;if(page_fail)return ERR_PTR(-ENOMEM);struct page **p=calloc(1,sizeof(*p));assert(p);page_live++;return p;}
static struct page **get_pages_vram(struct drm_gem_object *obj,int n) {(void)n;return drm_gem_get_pages(obj);}
static void drm_gem_put_pages(struct drm_gem_object *obj,struct page **p,bool dirty,bool accessed) {(void)obj;(void)dirty;(void)accessed;assert(page_live>0);free(p);page_live--;}
static void put_pages_vram(struct drm_gem_object *obj) {assert(to_msm_bo(obj)->vram_node);drm_gem_put_pages(obj,to_msm_bo(obj)->pages,true,false);}
static struct sg_table *drm_prime_pages_to_sg(struct drm_device *dev,struct page **p,int n) {(void)dev;(void)p;(void)n;if(sg_fail)return ERR_PTR(-ENOMEM);struct sg_table *s=calloc(1,sizeof(*s));assert(s);s->nents=1;s->orig_nents=3;sg_live++;return s;}
static void sg_free_table(struct sg_table *s) {assert(s&&sg_live>0);sg_live--;}
#define kfree(p) free(p)
static int dma_map_sg(struct device *dev,void *sgl,int n,int dir) {(void)sgl;(void)dir;assert(dev&&n==3);if(map_fail)return 0;map_live++;return 1;}
static void dma_unmap_sg(struct device *dev,void *sgl,int n,int dir) {(void)sgl;(void)dir;assert(dev&&n==3&&map_live>0);map_live--;}
static int msm_gem_new_handle(struct drm_device *dev,struct drm_file *file,uint32_t size,uint32_t flags,u32 *handle,char *name) {(void)dev;(void)file;assert(!strcmp(name,"dumb"));requested_size=size;requested_flags=flags;*handle=123;return 0;}
static enum dma_resv_usage dma_resv_usage_rw(bool write) {return write?DMA_RESV_USAGE_READ:DMA_RESV_USAGE_WRITE;}
static unsigned long timeout_to_jiffies(ktime_t *t) {return *t;}
static long dma_resv_wait_timeout(struct dma_resv *resv,enum dma_resv_usage usage,bool intr,unsigned long remain) {assert(resv&&intr);waited_usage=usage;waited_ticks=remain;return wait_result;}
static void put_pages(struct drm_gem_object *obj);
/* FUNCTIONS */
static uint32_t seed=17;
static uint32_t next(void) {seed=seed*1664525+1013904223;return seed;}
int main(void) {
 struct drm_device dev={.dev=&fake_device};struct drm_file file={0};int cases=0;
 const uint32_t values[]={0,1,7,8,31,32,33,4095,4096,65535,UINT32_MAX-31,UINT32_MAX};
 for(unsigned i=0;i<ARRAY_SIZE(values);i++)for(unsigned j=0;j<ARRAY_SIZE(values);j++)for(unsigned k=0;k<ARRAY_SIZE(values);k++){
  struct drm_mode_create_dumb a={.width=values[i],.height=values[j],.bpp=values[k]};
  __uint128_t pitch=(((__uint128_t)a.bpp+7)/8)*(((__uint128_t)a.width+31)&~(__uint128_t)31),size=pitch*a.height;
  bool valid=a.width&&a.height&&a.bpp&&pitch<=UINT32_MAX&&size<=UINT32_MAX-(PAGE_SIZE-1);
  int r=msm_gem_dumb_create(&file,&dev,&a);assert((r==0)==valid);if(valid){assert(a.pitch==pitch&&a.size==(uint64_t)((size+PAGE_SIZE-1)&~(__uint128_t)(PAGE_SIZE-1)));assert(requested_size==a.size&&requested_flags==(MSM_BO_SCANOUT|MSM_BO_CACHED)&&a.handle==123);}cases++;
 }
 struct dma_resv resv={0};struct msm_gem_object obj={.base={.dev=&dev,.size=4096},.aspace=&fake_device,.resv=&resv};ktime_t ticks=100;
 for(unsigned write=0;write<2;write++)for(unsigned nosync=0;nosync<2;nosync++)for(int result=-1;result<=1;result++){
  wait_result=result;int r=msm_gem_cpu_prep(&obj.base,(write?MSM_PREP_WRITE:0)|(nosync?MSM_PREP_NOSYNC:0),&ticks);assert(waited_usage==(write?DMA_RESV_USAGE_READ:DMA_RESV_USAGE_WRITE)&&waited_ticks==(nosync?0:100));assert(r==(result<0?-1:result==0?(nosync?-EBUSY:-ETIMEDOUT):0));
 }
 int faults=0;
 for(int vram=0;vram<2;vram++)for(int fault=0;fault<4;fault++){
  obj.vram_node=vram?&obj:NULL;obj.flags=MSM_BO_WC;page_fail=fault==1;sg_fail=fault==2;map_fail=fault==3;
  struct page **p=get_pages(&obj.base);bool failure=fault==1||fault==2||(!vram&&fault==3);assert(IS_ERR(p)==failure);
  if(failure){assert(!page_live&&!sg_live&&!map_live&&!obj.pages&&!obj.sgt);faults++;page_fail=sg_fail=map_fail=0;p=get_pages(&obj.base);assert(!IS_ERR(p));}
  assert(p==obj.pages&&obj.sgt&&page_live==1&&sg_live==1);assert(get_pages(&obj.base)==p);put_pages(&obj.base);assert(!page_live&&!sg_live&&!map_live&&!obj.pages&&!obj.sgt);put_pages(&obj.base);
 }
 int fb_cases=0;struct drm_format_info info={.hsub=2,.vsub=2,.cpp={4,2,1,1}};struct drm_gem_object bo[4];struct drm_gem_object *bos[4];for(int i=0;i<4;i++){bo[i].size=UINT64_MAX;bos[i]=&bo[i];}
 for(int n=1;n<=4;n++)for(int i=0;i<25000;i++){
  struct drm_mode_fb_cmd2 cmd={.width=next(),.height=next()};bool valid=cmd.width&&cmd.height;
  for(int p=0;p<n;p++){cmd.pitches[p]=next();cmd.offsets[p]=next();uint64_t w=((uint64_t)cmd.width+(p?1:0))/(p?2:1),h=((uint64_t)cmd.height+(p?1:0))/(p?2:1);__uint128_t row=(__uint128_t)w*info.cpp[p],size=(__uint128_t)(h-1)*cmd.pitches[p]+row+cmd.offsets[p];if(row>cmd.pitches[p]||size>UINT64_MAX)valid=false;}
  assert((msm_framebuffer_check_size(&cmd,&info,n,bos)==0)==valid);fb_cases++;
 }
 struct drm_mode_fb_cmd2 cmd={.width=33,.height=17,.pitches={132,34,17,17},.offsets={4,4,4,4}};
 for(int n=1;n<=4;n++){for(int p=0;p<n;p++){uint64_t w=(33+(p?1:0))/(p?2:1),h=(17+(p?1:0))/(p?2:1);bo[p].size=(h-1)*cmd.pitches[p]+w*info.cpp[p]+cmd.offsets[p];}assert(!msm_framebuffer_check_size(&cmd,&info,n,bos));bo[n-1].size--;assert(msm_framebuffer_check_size(&cmd,&info,n,bos)==-EINVAL);bo[n-1].size++;}
 assert(msm_framebuffer_check_size(&cmd,NULL,1,bos)==-EINVAL);assert(msm_framebuffer_check_size(&cmd,&info,0,bos)==-EINVAL);cmd.width=0;assert(msm_framebuffer_check_size(&cmd,&info,1,bos)==-EINVAL);
 printf("{\"dumb_buffer_bounds_cases\":%d,\"cpu_fence_usage_cases\":12,\"page_and_sg_fault_cases\":%d,\"framebuffer_bounds_cases\":%d}\n",cases,faults,fb_cases+11);
}
