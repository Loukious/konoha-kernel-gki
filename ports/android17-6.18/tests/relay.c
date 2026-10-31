#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CONFIG_RELAY 1
#define __init
#define __exit
#define __user
#define __percpu
#define ____cacheline_aligned
#define GFP_KERNEL 0
#define __GFP_ZERO 1
#define VM_MAP 1
#define PAGE_KERNEL 0
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL<<PAGE_SHIFT)
#define PAGE_ALIGN(x) (((x)+PAGE_SIZE-1)&~(PAGE_SIZE-1))
#define VM_DONTEXPAND 1
#define VM_FAULT_OOM 1
#define VM_FAULT_SIGBUS 2
#define KMALLOC_MAX_SIZE (1UL<<30)
#define NR_CPUS 4
#define S_IRUSR 0400
#define EPOLLERR 8
#define EPOLLIN 1
#define EPOLLRDNORM 64
#define FMODE_READ 1
#define CPUHP_BP_PREPARE_DYN 130
#define likely(x) (x)
#define unlikely(x) (x)
#define BUG_ON(x) assert(!(x))
#define min(x,y) ((x)<(y)?(x):(y))
#define pr_err(...) ((void)0)
#define smp_mb() atomic_thread_fence(memory_order_seq_cst)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef uint32_t u32;
typedef unsigned vm_fault_t,__poll_t;
typedef unsigned long pgoff_t;
typedef unsigned short umode_t;
typedef int poll_table;
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_lock(struct mutex*m){pthread_mutex_lock(&m->lock);}
static void mutex_unlock(struct mutex*m){pthread_mutex_unlock(&m->lock);}
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
static void list_add(struct list_head*p,struct list_head*h){p->next=h->next;p->prev=h;h->next->prev=p;h->next=p;}
static void list_del(struct list_head*p){p->prev->next=p->next;p->next->prev=p->prev;}
static bool list_empty(struct list_head*h){return h->next==h;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
struct kref {atomic_int refs;};
static void kref_init(struct kref*r){atomic_store(&r->refs,1);}
static void kref_get(struct kref*r){assert(atomic_fetch_add(&r->refs,1)>0);}
static void kref_put(struct kref*r,void(*f)(struct kref*)){assert(atomic_load(&r->refs)>0);if(atomic_fetch_sub(&r->refs,1)==1)f(r);}
typedef struct {bool initialized,active;unsigned wakeups;} wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t*w){w->initialized=true;}
static bool waitqueue_active(wait_queue_head_t*w){assert(w->initialized);return w->active;}
static void wake_up_interruptible(wait_queue_head_t*w){assert(w->initialized);w->wakeups++;}
struct irq_work {bool initialized,pending;void(*fn)(struct irq_work*);};
static void init_irq_work(struct irq_work*w,void(*fn)(struct irq_work*)){w->initialized=true;w->fn=fn;}
static void irq_work_queue(struct irq_work*w){assert(w->initialized);w->pending=true;}
static void irq_work_sync(struct irq_work*w){assert(w->initialized);if(w->pending){w->pending=false;w->fn(w);}}
static atomic_int live,module_refs;
static int allocation_step,allocation_fail;
static void *alloc(size_t n){if(allocation_fail&&++allocation_step==allocation_fail)return NULL;void*p=calloc(1,n);if(p)atomic_fetch_add(&live,1);return p;}
#define kzalloc(n,g) alloc(n)
#define kmalloc_array(n,s,g) alloc((n)*(s))
#define kvcalloc(n,s,g) alloc((n)*(s))
static void kfree(const void*p){if(p){atomic_fetch_sub(&live,1);free((void*)p);}}
#define kvfree kfree
static char *kasprintf(int g,const char*fmt,...){(void)g;char b[256];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);char*p=alloc(strlen(b)+1);if(p)strcpy(p,b);return p;}
static int strscpy(char*d,const char*s,size_t n){snprintf(d,n,"%s",s);return strlen(d);}
struct module {int unused;};static struct module owner;
#define THIS_MODULE (&owner)
static bool module_available=true;
static bool try_module_get(struct module*m){assert(m==THIS_MODULE);if(!module_available)return false;atomic_fetch_add(&module_refs,1);return true;}
static void module_put(struct module*m){assert(m==THIS_MODULE&&atomic_load(&module_refs)>0);atomic_fetch_sub(&module_refs,1);}
static unsigned online_mask=1,current_cpu;
#define for_each_possible_cpu(i) for((i)=0;(i)<NR_CPUS;(i)++)
#define for_each_online_cpu(i) for((i)=0;(i)<NR_CPUS;(i)++) if(online_mask&(1U<<(i)))
#define alloc_percpu(type) ((type*)alloc(NR_CPUS*sizeof(type)))
#define free_percpu kfree
#define per_cpu_ptr(p,c) (&(p)[c])
#define this_cpu_ptr(p) per_cpu_ptr(p,current_cpu)
#define get_cpu_ptr(p) this_cpu_ptr(p)
#define put_cpu_ptr(p) ((void)(p))
#define local_irq_save(f) ((f)=0)
#define local_irq_restore(f) ((void)(f))
static pthread_rwlock_t cpu_lock=PTHREAD_RWLOCK_INITIALIZER;
static atomic_int cpu_readers;
static void cpus_read_lock(void){pthread_rwlock_rdlock(&cpu_lock);atomic_fetch_add(&cpu_readers,1);}
static void cpus_read_unlock(void){assert(atomic_load(&cpu_readers)>0);atomic_fetch_sub(&cpu_readers,1);pthread_rwlock_unlock(&cpu_lock);}
struct page {unsigned long private;int refs;};
static struct page *alloc_page(int g){(void)g;struct page*p=alloc(sizeof(*p));if(p)p->refs=1;return p;}
static void set_page_private(struct page*p,unsigned long v){p->private=v;}
static void __free_page(struct page*p){assert(p->refs==1);kfree(p);}
static void get_page(struct page*p){p->refs++;}
static void put_page(struct page*p){assert(p->refs>1);p->refs--;}
struct mapping {void*base;size_t size;struct page**pages;};static struct mapping maps[32];static int fault_lookups;
static void *vmap(struct page**p,unsigned n,unsigned flags,int protection){(void)flags;(void)protection;void*m=alloc(n*PAGE_SIZE);if(!m)return NULL;for(unsigned i=0;i<32;i++)if(!maps[i].base){maps[i]=(struct mapping){m,n*PAGE_SIZE,p};return m;}abort();}
static void vunmap(void*p){for(unsigned i=0;i<32;i++)if(maps[i].base==p){maps[i].base=NULL;kfree(p);return;}abort();}
static struct page *vmalloc_to_page(void*p){fault_lookups++;for(unsigned i=0;i<32;i++)if(maps[i].base&&(char*)p>=(char*)maps[i].base&&(char*)p<(char*)maps[i].base+maps[i].size)return maps[i].pages[((char*)p-(char*)maps[i].base)>>PAGE_SHIFT];return NULL;}
struct inode {void*i_private;size_t i_size;pthread_mutex_t lock;};
struct dentry {struct inode inode;unsigned refs;};
#define d_inode(d) (&(d)->inode)
struct file {void*private_data;struct inode*inode;int f_mode,refs;};
struct vm_area_struct;
struct vm_fault {struct vm_area_struct*vma;pgoff_t pgoff;struct page*page;};
struct vm_operations_struct {vm_fault_t(*fault)(struct vm_fault*);};
struct vm_area_struct {unsigned long vm_start,vm_end,vm_pgoff,vm_flags;void*vm_private_data;const struct vm_operations_struct*vm_ops;struct file*vm_file;};
static void vm_flags_set(struct vm_area_struct*v,unsigned f){v->vm_flags|=f;}
static int nonseekable_open(struct inode*i,struct file*f){f->inode=i;return 0;}
static void poll_wait(struct file*f,wait_queue_head_t*w,poll_table*p){(void)f;(void)p;w->active=true;}
static struct inode *file_inode(struct file*f){return f->inode;}
static void inode_lock(struct inode*i){pthread_mutex_lock(&i->lock);}
static void inode_unlock(struct inode*i){pthread_mutex_unlock(&i->lock);}
static int copy_to_user(void*d,const void*s,size_t n){memcpy(d,s,n);return 0;}
struct file_operations {struct module*owner;int(*open)(struct inode*,struct file*);__poll_t(*poll)(struct file*,poll_table*);int(*mmap)(struct file*,struct vm_area_struct*);ssize_t(*read)(struct file*,char*,size_t,loff_t*);int(*release)(struct inode*,struct file*);};
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
static int hp_result=137,hp_live,hp_remove_count;static int(*hp_start)(unsigned);
static int cpuhp_setup_state(int state,const char*name,int(*start)(unsigned),void*end){assert(state==CPUHP_BP_PREPARE_DYN&&!strcmp(name,"onyx/relay:prepare")&&!end&&!hp_live);if(hp_result<0)return hp_result;hp_live=hp_result;hp_start=start;unsigned i;for_each_online_cpu(i)assert(!start(i));return hp_result;}
static void cpuhp_remove_state(int state){assert(state==hp_live&&hp_live>0);hp_live=0;hp_start=NULL;hp_remove_count++;}
/* HEADER */
/* SOURCE */
static int create_fail_cpu=-1;static bool global_mode,publish_open;
static atomic_bool create_entered,create_release,hotplug_done;
static bool block_create;
static struct file early_file;
static struct dentry *entries[NR_CPUS];
static void vfs_open(struct file*f,struct rchan_buf*b){b->dentry->refs++;assert(relay_file_operations.owner==THIS_MODULE);assert(try_module_get(relay_file_operations.owner));f->f_mode=FMODE_READ;f->refs=1;assert(!relay_file_operations.open(&b->dentry->inode,f));}
static void vfs_put(struct file*f){assert(f->refs>0);if(--f->refs==0){struct dentry*d=container_of(f->inode,struct dentry,inode);relay_file_operations.release(NULL,f);assert(d->refs);if(!--d->refs)kfree(d);module_put(relay_file_operations.owner);}}
static struct dentry *create_file(const char*n,struct dentry*p,umode_t mode,struct rchan_buf*b,int*g){(void)p;assert(mode==S_IRUSR);if(block_create){atomic_store(&create_entered,true);while(!atomic_load(&create_release))usleep(100);}
 unsigned cpu=n?strtoul(n+strlen(n)-1,NULL,10):0;if((int)cpu==create_fail_cpu)return (void*)(intptr_t)-ENOMEM;struct dentry*d=alloc(sizeof(*d));if(!d)return NULL;d->refs=1;d->inode.i_private=b;pthread_mutex_init(&d->inode.lock,NULL);entries[cpu]=d;*g=global_mode;if(publish_open){d->refs++;assert(b->read_wait.initialized&&b->wakeup_work.initialized);assert(try_module_get(THIS_MODULE));early_file.refs=1;early_file.f_mode=FMODE_READ;assert(!relay_file_open(&d->inode,&early_file));}return d;}
static int remove_file(struct dentry*d){assert(d);struct rchan_buf*b=d->inode.i_private;assert(!b->wakeup_work.pending);for(unsigned i=0;i<NR_CPUS;i++)if(entries[i]==d)entries[i]=NULL;/* Model the inode held by every open file, including its VMAs. */assert(d->refs);if(!--d->refs)kfree(d);return 0;}
static const struct rchan_callbacks cb={.create_buf_file=create_file,.remove_buf_file=remove_file};
static struct rchan *open_channel(void){struct rchan*c=relay_open("relay",NULL,64,4,&cb,NULL);assert(c);return c;}
static void clean(void){assert(!atomic_load(&live)&&!atomic_load(&module_refs)&&list_empty(&relay_channels));}
static unsigned module_init_test(void){hp_result=-ENOMEM;assert(onyx_relay_init()==-ENOMEM&&!hp_live);hp_result=137;assert(!onyx_relay_init()&&hp_live==137&&hp_start==relay_prepare_cpu);onyx_relay_exit();assert(!hp_live&&hp_remove_count==1);module_available=false;assert(!relay_open("r",NULL,64,4,&cb,NULL));clean();return 4;}
static unsigned allocation_failures(void){unsigned count=0;for(int step=1;step<=12;step++){allocation_step=0;allocation_fail=step;struct rchan*c=relay_open("r",NULL,64,4,&cb,NULL);if(c)relay_close(c);clean();count++;}allocation_fail=0;assert(!relay_open("r",NULL,0,4,&cb,NULL));assert(!relay_open("r",NULL,SIZE_MAX,2,&cb,NULL));assert(!relay_open("r",NULL,64,4,NULL,NULL));clean();return count+3;}
static unsigned cpu_prepare_test(void){struct rchan*c=open_channel();struct rchan_buf*zero=*per_cpu_ptr(c->buf,0);create_fail_cpu=1;assert(relay_prepare_cpu(1)==-ENOMEM);assert(*per_cpu_ptr(c->buf,0)==zero&&!*per_cpu_ptr(c->buf,1));create_fail_cpu=-1;assert(!relay_prepare_cpu(1));struct rchan_buf*one=*per_cpu_ptr(c->buf,1);assert(one&&one!=zero&&one->cpu==1);assert(!relay_prepare_cpu(1)&&*per_cpu_ptr(c->buf,1)==one);relay_close(c);clean();return 4;}
static unsigned global_test(void){global_mode=true;online_mask=7;struct rchan*c=open_channel();assert(c->is_global&&*per_cpu_ptr(c->buf,0)==*per_cpu_ptr(c->buf,2));assert(!relay_prepare_cpu(3)&&*per_cpu_ptr(c->buf,3)==*per_cpu_ptr(c->buf,0));relay_close(c);clean();return 2;}
static unsigned file_mmap_test(void){struct rchan*c=open_channel();struct rchan_buf*b=*per_cpu_ptr(c->buf,0);struct file f={0};vfs_open(&f,b);assert(module_refs==2);struct vm_area_struct v={.vm_start=0x1000,.vm_end=0x2000,.vm_pgoff=1,.vm_file=&f};assert(relay_file_mmap(&f,&v)==-EINVAL);v.vm_pgoff=0;assert(!relay_file_mmap(&f,&v)&&v.vm_ops==&relay_file_mmap_ops);f.refs++;relay_write(c,"abc",3);relay_close(c);assert(b->finalized&&module_refs==2);vfs_put(&f);assert(f.refs==1&&module_refs==2);struct vm_fault fault={.vma=&v,.pgoff=0};assert(!relay_buf_fault(&fault));assert(!memcmp(b->start,"abc",3));put_page(fault.page);int lookups=fault_lookups;fault.pgoff=1;assert(relay_buf_fault(&fault)==VM_FAULT_SIGBUS&&fault_lookups==lookups);vfs_put(&f);clean();return 8;}
static unsigned ring_test(void){struct rchan*c=open_channel();struct rchan_buf*b=*per_cpu_ptr(c->buf,0);b->read_wait.active=true;char data[64];memset(data,'x',sizeof(data));relay_write(c,data,64);relay_flush(c);assert(b->subbufs_produced==1&&b->wakeup_work.pending);relay_subbufs_consumed(c,0,1);assert(b->subbufs_consumed==1);relay_reset(c);assert(!b->wakeup_work.pending&&b->subbufs_produced==0&&b->read_wait.wakeups==1);relay_write(c,data,64);relay_flush(c);assert(b->wakeup_work.pending);relay_close(c);clean();return 5;}
static unsigned publish_open_test(void){publish_open=true;struct rchan*c=open_channel();struct rchan_buf*b=*per_cpu_ptr(c->buf,0);assert(b->kref.refs==2&&module_refs==2);relay_close(c);assert(module_refs==2);vfs_put(&early_file);clean();return 3;}
static unsigned read_poll_test(void){struct rchan*c=open_channel();struct rchan_buf*b=*per_cpu_ptr(c->buf,0);struct file f={0};char out[8]={0};loff_t pos=0;vfs_open(&f,b);assert(!relay_file_poll(&f,NULL));relay_write(c,"abcdef",6);assert(relay_file_read(&f,out,2,&pos)==2&&!memcmp(out,"ab",2));relay_close(c);assert(relay_file_poll(&f,NULL)==EPOLLERR);assert(relay_file_read(&f,out,8,&pos)==4&&!memcmp(out,"cdef",4));assert(!relay_file_read(&f,out,8,&pos));vfs_put(&f);clean();return 5;}
static unsigned partial_open_failure(void){online_mask=3;publish_open=true;create_fail_cpu=1;assert(!relay_open("relay",NULL,64,4,&cb,NULL));struct rchan_buf*b=early_file.private_data;assert(b->finalized&&module_refs==2&&list_empty(&relay_channels));char out[8];loff_t pos=0;assert(!relay_file_read(&early_file,out,8,&pos));vfs_put(&early_file);clean();return 3;}
static struct rchan*thread_channel;
static void *open_thread(void*p){(void)p;thread_channel=open_channel();return NULL;}
static void *hotplug_thread(void*p){(void)p;pthread_rwlock_wrlock(&cpu_lock);assert(!relay_prepare_cpu(1));online_mask|=2;pthread_rwlock_unlock(&cpu_lock);atomic_store(&hotplug_done,true);return NULL;}
static unsigned hotplug_order(void){block_create=true;pthread_t a,b;pthread_create(&a,NULL,open_thread,NULL);while(!atomic_load(&create_entered))usleep(100);assert(atomic_load(&cpu_readers)==1);pthread_create(&b,NULL,hotplug_thread,NULL);usleep(1000);assert(!atomic_load(&hotplug_done));atomic_store(&create_release,true);pthread_join(a,NULL);pthread_join(b,NULL);assert(*per_cpu_ptr(thread_channel->buf,1));relay_close(thread_channel);clean();return 3;}
int main(int argc,char**argv){assert(argc==2);unsigned n;if(!strcmp(argv[1],"module_init"))n=module_init_test();else if(!strcmp(argv[1],"allocation_failures"))n=allocation_failures();else if(!strcmp(argv[1],"cpu_prepare"))n=cpu_prepare_test();else if(!strcmp(argv[1],"global"))n=global_test();else if(!strcmp(argv[1],"file_mmap"))n=file_mmap_test();else if(!strcmp(argv[1],"ring"))n=ring_test();else if(!strcmp(argv[1],"publish_open"))n=publish_open_test();else if(!strcmp(argv[1],"read_poll"))n=read_poll_test();else if(!strcmp(argv[1],"partial_open_failure"))n=partial_open_failure();else if(!strcmp(argv[1],"hotplug_order"))n=hotplug_order();else abort();printf("{\"%s\":%u}\n",argv[1],n);return 0;}
