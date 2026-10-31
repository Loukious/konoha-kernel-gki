#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t sector_t;
typedef int blk_opf_t;
#define CHARGER_PARTITION_RWSIZE 4096
#define SECTOR_SHIFT 9
#define REQ_OP_READ 0
#define REQ_OP_WRITE 1
#define REQ_SYNC 2
#define REQ_FUA 4
#define GFP_KERNEL 0
#define __GFP_ZERO 0
#define EXPORT_SYMBOL(name)
#define THIS_MODULE NULL
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
/* HEADER_DEFINITIONS */
struct page { unsigned char bytes[4096]; };
struct block_device { size_t sectors; };
struct file { struct block_device *bdev; };
struct task_struct { int id; };
static struct task_struct main_task, other_task;
static struct task_struct *current = &main_task;
struct charger_partition_device { struct file *file; bool eu_model, eu_valid; };
static struct block_device device = { .sectors = CHARGER_PARTITION_INFO_LAST * 8 };
static struct file file = { &device };
static struct charger_partition_device cp = { .file = &file };
static struct charger_partition_device *partition = &cp;
static pthread_mutex_t partition_lock = PTHREAD_MUTEX_INITIALIZER;
static struct task_struct *transaction_owner;
static struct page *header_page, *data_page;
static u8 transaction_host, transaction_info;
static u32 transaction_size;
static unsigned char disk[CHARGER_PARTITION_INFO_LAST * 4096];
static int pages, bios, refs, alloc_call, fail_alloc_at, io_call, fail_io_at, fail_copy;
static struct page *alloc_page(int flags) { (void)flags; if (++alloc_call == fail_alloc_at) return NULL; struct page *p = calloc(1,sizeof(*p)); if (p) pages++; return p; }
static void __free_page(struct page *p) { assert(p); pages--; free(p); }
static void *page_address(struct page *p) { return p->bytes; }
static struct block_device *file_bdev(struct file *f) { return f->bdev; }
static size_t bdev_nr_sectors(struct block_device *b) { return b->sectors; }
struct bio { struct { sector_t bi_sector; } bi_iter; struct page *page; blk_opf_t op; };
static struct bio *bio_alloc(struct block_device *b, int n, blk_opf_t op, int flags) { (void)b;(void)n;(void)flags; struct bio *p=calloc(1,sizeof(*p)); if(p) {p->op=op;bios++;} return p; }
static int bio_add_page(struct bio *b, struct page *p, int size, int off) { assert(!off);b->page=p;return size; }
static void bio_put(struct bio *b) { bios--;free(b); }
static int submit_bio_wait(struct bio *b) { if (++io_call == fail_io_at) return -EIO; size_t off=b->bi_iter.bi_sector*512; assert(off+4096<=sizeof(disk)); if (b->op&REQ_OP_WRITE) memcpy(disk+off,b->page->bytes,4096);else memcpy(b->page->bytes,disk+off,4096);return 0; }
static void __module_get(void *p) { (void)p;refs++; }
static void module_put(void *p) { (void)p;refs--; }
static void *kmemdup(const void *p, size_t n, int flags) { (void)flags;if(fail_copy)return NULL;void *q=malloc(n);if(q)memcpy(q,p,n);return q; }
#define kfree free
/* STAGED_DEFINITIONS */
static void reset(void) {
 assert(!pages&&!bios&&!refs&&!transaction_owner);
 assert(pthread_mutex_trylock(&partition_lock)==0);pthread_mutex_unlock(&partition_lock);
 memset(disk,0x5a,sizeof(disk));
 charger_partition_header *h=(void *)disk;
 h->magic=CHARGER_PARTITION_MAGIC;h->initialized=1;h->available=1;
 alloc_call=io_call=fail_alloc_at=fail_io_at=fail_copy=0;
 partition=&cp;current=&main_task;
}
int main(void) {
 reset();
 for (int host=0;host<256;host++) for(int info=0;info<256;info++) {
  bool valid=host<CHARGER_PARTITION_HOST_LAST&&info>0&&info<CHARGER_PARTITION_INFO_LAST;
  assert(charger_partition_request_valid(host,info,32)==valid);
 }
 assert(!charger_partition_request_valid(4,1,0));
 assert(charger_partition_request_valid(4,1,4096));
 assert(!charger_partition_request_valid(4,1,4097));
 for(int n=1;n<=2;n++) {reset();fail_alloc_at=n;assert(charger_partition_alloc(4,1,32)==-ENOMEM);reset();}
 for(int n=1;n<=2;n++) {reset();fail_io_at=n;assert(charger_partition_alloc(4,1,32)==-EIO);reset();}
 ((charger_partition_header *)disk)->magic=0;assert(charger_partition_alloc(4,1,32)==-EUCLEAN);assert(((charger_partition_header *)disk)->magic==0);reset();
 ((charger_partition_header *)disk)->available=0;assert(charger_partition_alloc(4,1,32)==-EBUSY);reset();
 partition=NULL;assert(charger_partition_alloc(4,1,32)==-ENODEV);reset();
 assert(charger_partition_alloc(4,1,32)==0);assert(charger_partition_alloc(4,1,32)==-EDEADLK);
 current=&other_task;assert(charger_partition_read(4,1,32)==NULL);assert(charger_partition_dealloc(4,1,32)==-EPERM);current=&main_task;
 assert(charger_partition_read(4,2,32)==NULL);
 unsigned char *buf=charger_partition_read(4,1,32);assert(buf);memset(buf,0xab,32);
 assert(charger_partition_write(4,1,buf,32)==0);for(int i=0;i<32;i++)assert(disk[4096+i]==0xab);assert(disk[4096+32]==0x5a);
 fail_copy=1;assert(charger_partition_write(4,1,buf,32)==-ENOMEM);fail_copy=0;
 assert(charger_partition_dealloc(4,1,32)==0);assert(((charger_partition_header *)disk)->available==1);reset();
 for(int n=3;n<=6;n++) {reset();assert(!charger_partition_alloc(4,1,32));fail_io_at=n;u32 val[8]={0};int r=charger_partition_write(4,1,val,32);int e=charger_partition_dealloc(4,1,32);if(n<5)assert(r==-EIO);if(n==5)assert(e==-EIO);reset();}
 u32 value=1;assert(charger_partition_field(2,0,&value,4,true)==0);assert(cp.eu_model&&cp.eu_valid);assert(disk[8192+4]==0x5a);
 value=0;assert(charger_partition_field(2,0,&value,4,false)==0&&value==1);
 assert(charger_partition_field(1,SIZE_MAX,&value,4,false)==-EINVAL);
 assert(charger_partition_field(1,0,NULL,4,false)==-EINVAL);
 reset();
 puts("{\"passed\":true,\"request_combinations\":65536,\"allocation_and_io_faults\":true,\"wrong_task_rejected\":true,\"scratch_alias_preserved\":true,\"record_remainder_preserved\":true}");
}
