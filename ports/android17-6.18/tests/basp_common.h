#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint8_t u8;
typedef int32_t s32;
#define GFP_KERNEL 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define THIS_MODULE NULL
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define guard(kind) mock_guard
static void mock_guard(void *p) { (void)p; }
struct mutex { int unused; };
static void mutex_lock(struct mutex *p) { (void)p; }
static void mutex_unlock(struct mutex *p) { (void)p; }
struct kref { int count; };
static void kref_get(struct kref *p) { assert(p->count > 0); p->count++; }
static void kref_put(struct kref *p, void (*freefn)(struct kref *)) {
 assert(p->count > 0); if (!--p->count) freefn(p);
}
static int allocations, fail_at, attempt, pages, module_refs;
static void *kcalloc(size_t n,size_t s,int flags) {
 (void)flags; if (++attempt==fail_at) return NULL;
 void *p=calloc(n,s); if(p) allocations++; return p;
}
static void kfree(void *p) { if(p) {allocations--;free(p);} }
struct page { u8 bytes[4096]; };
static void __free_page(struct page *p) { pages--;free(p); }
static void __module_get(void *p) { (void)p;module_refs++; }
static void module_put(void *p) { (void)p;assert(module_refs > 0);module_refs--; }
struct vm_area_struct { void *vm_private_data; };
static u32 get_unaligned_le32(const void *p) {
 const u8 *b=p;return b[0]|(u32)b[1]<<8|(u32)b[2]<<16|(u32)b[3]<<24;
}
static void put32(u8 *b,u32 x) { for(int i=0;i<4;i++) b[i]=x>>(i*8); }
/* STAGED_HEADER */
struct smart_charge_info {
 struct mutex state_lock;
 bool stopping;
 int enable_fv_dec_by_cc;
 struct smart_basp_transport *basp;
};
static int smart_ops_srcu;
static struct mca_smartchg_if_ops *mock_ops[MCA_SMARTCHG_IF_CHG_TYPE_END];
static struct mca_smartchg_if_ops *mca_smartchg_if_get_ops(unsigned int type) { return mock_ops[type]; }
/* STAGED_STRUCTS */
/* STAGED_FUNCTIONS */
