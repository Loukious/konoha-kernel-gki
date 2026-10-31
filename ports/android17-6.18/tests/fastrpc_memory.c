#include "fastrpc_services.h"
#include <fcntl.h>
typedef int32_t __s32;typedef uint32_t __u32;typedef uint64_t __u64;
struct mutex {int unused;};struct timespec64 {long a,b;};
struct device {int unused;};struct dma_buf {void *priv;};
struct fastrpc_channel_ctx {int refs;atomic_t exported_bufs;int export_wait;};
struct fastrpc_user {struct fastrpc_channel_ctx *cctx;void *sctx;};
#include "memory_types.inc"
static int alloc_fail,export_fail,fd_fail,copy_in_fail,copy_out_fail,permission_error,permission_mode;
static int vmid=3,wakes,freed;static struct dma_buf *installed;
struct dma_buf_export_info {void *ops,*priv;u64 size;int flags;};
#define DEFINE_DMA_BUF_EXPORT_INFO(n) struct dma_buf_export_info n={0}
static int fastrpc_dma_buf_ops;
static void wake_up_all(int *q){(void)q;wakes++;}
static void fastrpc_channel_ctx_get(struct fastrpc_channel_ctx *c){c->refs++;}
static void fastrpc_channel_ctx_put(struct fastrpc_channel_ctx *c){assert(c->refs>0);c->refs--;}
static void __fastrpc_buf_free(struct fastrpc_buf *b){assert(!b->in_use);freed++;kfree(b);}
static void fastrpc_buf_free(struct fastrpc_buf *b,bool cache){assert(!cache);__fastrpc_buf_free(b);}
static int fastrpc_smmu_buf_alloc(struct fastrpc_user *fl,u64 size,u32 type,struct fastrpc_buf **out){if(alloc_fail)return -ENOMEM;*out=kzalloc(sizeof(**out),0);(*out)->fl=fl;(*out)->size=size;(*out)->type=type;return 0;}
static unsigned copy_from_user(void *d,const void *s,size_t n){if(copy_in_fail)return 1;memcpy(d,s,n);return 0;}
static unsigned copy_to_user(void *d,const void *s,size_t n){if(copy_out_fail)return 1;memcpy(d,s,n);return 0;}
static struct dma_buf *dma_buf_export(struct dma_buf_export_info *info){if(export_fail)return ERR_PTR(-EINVAL);assert(info->flags==O_RDWR);struct dma_buf *b=kzalloc(sizeof(*b),0);b->priv=info->priv;return b;}
static void fastrpc_release(struct dma_buf *);
static void dma_buf_put(struct dma_buf *b){fastrpc_release(b);kfree(b);}
static int dma_buf_fd(struct dma_buf *b,int flags){(void)flags;if(fd_fail)return -EMFILE;installed=b;return 27;}
struct vm_area_struct {size_t vm_start,vm_end,vm_pgoff;};
#define FASTRPC_PHYS(n) (n)
static int dma_mmap_coherent(struct device *d,struct vm_area_struct *v,void *p,u64 phys,size_t size){(void)d;(void)p;(void)phys;return v->vm_pgoff*4096+v->vm_end-v->vm_start>size?-EINVAL:0;}
static int mem_buf_current_vmid(void){return vmid;}
static int mem_buf_dma_buf_copy_vmperm(struct dma_buf *b,int **ids,int **perms,int *count){(void)b;if(permission_error)return -EACCES;*count=permission_mode?2:1;*ids=kzalloc(*count*sizeof(int),0);*perms=kzalloc(*count*sizeof(int),0);(*ids)[0]=permission_mode==2?99:vmid;return 0;}
#include "memory.inc"
int main(void){struct fastrpc_channel_ctx c={.refs=1};struct fastrpc_alloc_dma_buf request={.size=8192};int cases=0;bool own;
 for(int mode=0;mode<3;mode++){permission_mode=mode;assert(!get_buffer_attr(NULL,&own) && own==(mode==0));assert(!atomic_load(&allocations));cases++;}permission_error=1;assert(get_buffer_attr(NULL,&own)==-EACCES && !own);cases++;
 for(int fail=0;fail<6;fail++){struct fastrpc_user *fl=kzalloc(sizeof(*fl),0);fl->cctx=&c;fl->sctx=(void *)1;copy_in_fail=fail==1;alloc_fail=fail==2;export_fail=fail==3;fd_fail=fail==4;copy_out_fail=fail==5;installed=NULL;int r=fastrpc_dmabuf_alloc(fl,(char *)&request);kfree(fl);if(fail==0 || fail==5){assert((fail==0?!r:r==-EFAULT) && installed && c.refs==2 && atomic_read(&c.exported_bufs)==1);struct fastrpc_buf *buf=installed->priv;assert(!buf->fl);struct vm_area_struct v={.vm_end=4096,.vm_pgoff=1};assert(!fastrpc_mmap(installed,&v));v.vm_pgoff=2;assert(fastrpc_mmap(installed,&v)==-EINVAL);dma_buf_put(installed);}else assert(r<0);assert(c.refs==1 && !atomic_read(&c.exported_bufs) && !atomic_load(&allocations));cases++;}
 printf("{\"permission_export_failure_scenarios\":%d,\"export_after_user_free\":true,\"dma_mmap_offset_bounds\":true}\n",cases);
}
