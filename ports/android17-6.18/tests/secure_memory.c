#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef uint64_t phys_addr_t;
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define IS_ALIGNED(v,a) (((v)&((a)-1))==0)
#define IS_ERR_OR_NULL(p) (!(p)||((intptr_t)(p)<0))
#define smp_load_acquire(p) (*(p))
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define SET_SI_OBJECT(o,t) ((o)->object_type=(t))
#define NULL_SI_OBJECT NULL
enum si_object_type {SI_OT_USER=1,SI_OT_CB_OBJECT=2,SI_OT_ROOT=8,SI_OT_NULL=16};
struct si_object_operations {int (*dispatch)(void);};
struct si_object {unsigned refcount;int object_type;struct si_object_operations *ops;char *name;void (*release)(struct si_object*);};
struct dma_buf {int refs;};
struct mem_object {struct si_object object;struct {int lock;void *buf_attach,*sgt;} map;uint64_t shm_bridge_handle;struct dma_buf *dma_buf;void *private;void (*release)(void*);struct {phys_addr_t p_addr;size_t p_addr_len;} mapping_info;int node;};
#define to_mem_object(o) ((struct mem_object *)(o))
#define DMA_BIDIRECTIONAL 0
static void *mem_object_pdev;
static int mo_list_mutex,mo_list,list_count,alloc_count,fail_alloc,live;
static int dispatch(void){return 0;}
static struct si_object_operations mem_ops={dispatch};
static void primordial_object_release(struct si_object *o){(void)o;}
static int primordial_object_register(struct si_object *o){assert(o->object_type==SI_OT_CB_OBJECT);return 0;}
static void kref_init(unsigned *p){*p=1;}
static void *kzalloc(size_t size,int flags){(void)flags;if(++alloc_count==fail_alloc)return NULL;void *p=calloc(1,size);assert(p);live++;return p;}
static char *kvasprintf_const(int flags,const char *fmt,va_list ap){(void)flags;(void)fmt;(void)ap;if(++alloc_count==fail_alloc)return NULL;char *p=strdup("object");assert(p);live++;return p;}
static void kfree(void *p){if(p){assert(live);live--;free(p);}}
static void mutex_init(int *p){*p=1;}
static void mutex_lock(int *p){(void)p;}
static void mutex_unlock(int *p){(void)p;}
static void get_dma_buf(struct dma_buf *p){p->refs++;}
static void dma_buf_put(struct dma_buf *p){assert(p->refs>1);p->refs--;}
static void list_add_tail(int *node,int *list){(void)list;*node=1;list_count++;}
static void list_del(int *node){assert(*node&&list_count);*node=0;list_count--;}
static int dereg_error,detached;
static int qtee_shmbridge_deregister(uint64_t handle){assert(handle);return dereg_error;}
static void dma_buf_unmap_attachment_unlocked(void *attachment,void *sgt,int direction){assert(attachment&&sgt);(void)direction;}
static void dma_buf_detach(struct dma_buf *buf,void *attachment){assert(buf&&attachment);detached++;}
/* FUNCTIONS */
static void destroy(struct si_object *object){struct mem_object *m=(void*)object;if(m->dma_buf)dma_buf_put(m->dma_buf);assert(m->node);list_count--;kfree(object->name);kfree(m);}
int main(void){unsigned cases=0;struct dma_buf buf={1};assert(!init_si_mem_object_user(&buf,NULL,NULL));assert(!init_si_mem_object(4096,4096,NULL,NULL));mem_object_pdev=(void*)1;
 for(int f=1;f<=2;f++){alloc_count=0;fail_alloc=f;assert(!init_si_mem_object_user(&buf,NULL,NULL)&&!live&&buf.refs==1&&!list_count);cases++;alloc_count=0;assert(!init_si_mem_object(4096,4096,NULL,NULL)&&!live&&!list_count);cases++;}
 fail_alloc=0;struct si_object *object=init_si_mem_object_user(&buf,NULL,NULL);assert(object&&live==2&&buf.refs==2&&list_count==1);destroy(object);assert(!live&&!list_count&&buf.refs==1);cases++;
 object=init_si_mem_object(4096,4096,NULL,NULL);assert(object&&live==2&&list_count==1);destroy(object);cases++;
 /* Check page boundaries without allocating a valid object. */
 for(unsigned i=1;i<4096;i++){assert(!init_si_mem_object(4096+i,4096,NULL,NULL));assert(!init_si_mem_object(4096,4096+i,NULL,NULL));cases+=2;}
 assert(!init_si_mem_object(UINT64_MAX-4095,4096,NULL,NULL));
 object=init_si_mem_object_user(&buf,NULL,NULL);assert(object);struct mem_object *mo=(void*)object;mo->shm_bridge_handle=123;mo->map.buf_attach=(void*)1;mo->map.sgt=(void*)2;char *saved_name=object->name;
 dereg_error=-EBUSY;mo_shm_bridge_release(object);assert(mo->shm_bridge_handle==123&&buf.refs==2&&live==2&&list_count==1&&!detached&&!object->name);kfree(saved_name);
 dereg_error=0;mo_shm_bridge_release(object);assert(buf.refs==1&&!live&&!list_count&&detached==1);
 printf("{\"constructor_boundary_and_allocation_cases\":%u,\"failed_name_allocation_releases_dma_ref\":true,\"withdrawal_failure_retains_mapping\":true}\n",cases);
}
