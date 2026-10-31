#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
struct folio { int lru; };
struct kgsl_memdesc { int lock;struct folio *shmem_page_list; };
struct shmem_inode_info { uintptr_t android_vendor_data1; };
static struct kgsl_memdesc *current;
static struct folio page;
static int alloc_ret,alloc_calls,locks;
#define mutex_lock(p) do{assert(!*(p));*(p)=1;locks++;}while(0)
#define mutex_unlock(p) do{assert(*(p));*(p)=0;}while(0)
#define list_empty(p) (!*(p))
#define list_first_entry(p,t,m) (*(p))
#define list_del(p) (current->shmem_page_list=NULL)
static int kgsl_shmem_alloc_pages(struct kgsl_memdesc *m){alloc_calls++;if(alloc_ret>0)m->shmem_page_list=&page;return alloc_ret;}
/* STAGED_DEFINITIONS */
int main(void){unsigned cases=0;
 for(int order=-1;order<64;order++)for(int result=-4;result<=4;result++)for(int existing=0;existing<2;existing++) {
  struct kgsl_memdesc m={.shmem_page_list=existing?&page:NULL};struct shmem_inode_info inode={(uintptr_t)&m};current=&m;
  struct folio *out=NULL;alloc_ret=result;alloc_calls=0;locks=0;
  kgsl_shmem_fill_page(NULL,&inode,&out,order);
  if(order){assert(!out&&!locks&&!alloc_calls);}
  else {assert(locks==1&&m.lock==0);assert(alloc_calls==!existing);assert((out!=NULL)==(existing||result>0));}
  cases++;
 }
 for(int order=0;order<8;order++){
  struct shmem_inode_info inode={0};struct folio *out=NULL;kgsl_shmem_fill_page(NULL,&inode,&out,order);assert(!out);
  inode.android_vendor_data1=(uintptr_t)-12;kgsl_shmem_fill_page(NULL,&inode,&out,order);assert(!out);cases+=2;
 }
 printf("{\"shmem_hook_order_cases\":%u}\n",cases);
}
