#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef uint64_t dma_addr_t;typedef uint64_t resource_size_t;
#define __iomem
#define __must_check
#define __maybe_unused
#define BIT(x) (1UL << (x))
#define IS_ALIGNED(x,n) (!((x)&((n)-1)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define U32_MAX UINT32_MAX
#define U64_MAX UINT64_MAX
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define PCIE_DBG(...) ((void)0)
#define PCIE_DBG3(...) ((void)0)
#define PCIE_ERR(...) ((void)0)
#define MHI_ERR(...) ((void)0)
#define MHI_VERB(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define pr_err(...) ((void)0)
#define GFP_KERNEL 0
#define DMA_ATTR_FORCE_CONTIGUOUS 0
#define min(a,b) ((a)<(b)?(a):(b))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define check_add_overflow(a,b,p) __builtin_add_overflow(a,b,p)
#define __ffs(x) __builtin_ctzl(x)
#define upper_32_bits(x) ((u32)((u64)(x)>>32))
#define lower_32_bits(x) ((u32)(x))
#define cpu_to_le64(x) (x)
static int allocation_call, fail_alloc, allocations;
static void *alloc_mem(size_t n){if(++allocation_call==fail_alloc)return NULL;assert(n<1024*1024);void *p=calloc(1,n?n:1);assert(p);allocations++;return p;}
static void free_mem(void *p){if(p){allocations--;free(p);}}
#define kzalloc(n,f) alloc_mem(n)
#define kcalloc(n,z,f) alloc_mem((n)*(z))
#define vcalloc(n,z) alloc_mem((n)*(z))
#define kfree(p) free_mem(p)
#define vfree(p) free_mem(p)
static int locked;
#define mutex_lock(p) assert(!(*(p))++)
#define mutex_unlock(p) assert((*(p))--)
#define raw_spin_lock_irqsave(p,f) do {(f)=0;assert(!locked++);}while(0)
#define raw_spin_unlock_irqrestore(p,f) do {(void)(f);assert(locked--);}while(0)
#define spin_lock_irqsave(p,f) raw_spin_lock_irqsave(p,f)
#define spin_unlock_irqrestore(p,f) raw_spin_unlock_irqrestore(p,f)
struct list_head {struct list_head *next,*prev;};
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define INIT_LIST_HEAD(p) ((p)->next=(p)->prev=(p))
static void list_add_tail(struct list_head *e,struct list_head *h){e->next=h;e->prev=h->prev;h->prev->next=e;h->prev=e;}
static void list_del(struct list_head *e){e->prev->next=e->next;e->next->prev=e->prev;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of((p)->m.next,__typeof__(*p),m);&(p)->m!=(h);p=t,t=container_of((t)->m.next,__typeof__(*t),m))
