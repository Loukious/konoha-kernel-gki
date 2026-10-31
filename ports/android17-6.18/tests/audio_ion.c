#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#define __init
#define __exit
#define __user
#define GFP_KERNEL 0
#define CONFIG_SPF_CORE 1
#define THIS_MODULE ((void *)1)
#define DMA_BIDIRECTIONAL 0
#define EPROBE_DEFER 517
#define U32_MAX UINT32_MAX
#define U64_MAX UINT64_MAX
#define SZ_4K 4096
#define BIT(n) (1UL<<(n))
#define BIT_ULL(n) (1ULL<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ALIGNED(v,a) (!((v)&((a)-1)))
#define KERNEL_VERSION(a,b,c) (((a)<<16)+((b)<<8)+(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,0)
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((intptr_t)(p))
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define check_add_overflow(a,b,out) __builtin_add_overflow((a),(b),(out))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define MAJOR(d) ((unsigned)((d)>>8))
typedef uint32_t u32,__u32;
typedef uint64_t u64,__u64,dma_addr_t,phys_addr_t;
typedef uint8_t u8;
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&(n),&(n)}
#define INIT_LIST_HEAD(n) ((n)->next=(n),(n)->prev=(n))
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->next->prev=n->prev;n->prev->next=n->next;}
static bool list_empty(const struct list_head *h){return h->next==h;}
static bool list_is_singular(const struct list_head *h){return !list_empty(h)&&h->next==h->prev;}
#define list_entry(p,t,m) container_of(p,t,m)
#define list_for_each_entry(p,h,m) for(p=list_entry((h)->next,typeof(*(p)),m);&(p)->m!=(h);p=list_entry((p)->m.next,typeof(*(p)),m))
#define list_for_each_entry_safe(p,n,h,m) for(p=list_entry((h)->next,typeof(*(p)),m),n=list_entry((p)->m.next,typeof(*(p)),m);&(p)->m!=(h);p=n,n=list_entry((n)->m.next,typeof(*(n)),m))
#define list_for_each_safe(p,n,h) for(p=(h)->next,n=p->next;p!=(h);p=n,n=p->next)
static atomic_int live;
static int fail_alloc=-1;
static void *kzalloc(size_t bytes,int flags){(void)flags;if(fail_alloc==0)return NULL;if(fail_alloc>0)fail_alloc--;void *p=calloc(1,bytes);assert(p);live++;return p;}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
struct device;
struct cdev;
struct inode {struct cdev *i_cdev;};
struct file {struct inode *f_inode;void *private_data;};
struct file_operations {void *owner;int (*open)(struct inode *,struct file *);int (*release)(struct inode *,struct file *);long (*unlocked_ioctl)(struct file *,unsigned int,unsigned long);long (*compat_ioctl)(struct file *,unsigned int,unsigned long);};
struct class {int unused;};
struct device_node {bool smmu;unsigned refs;};
struct device {atomic_uint refs;void (*release)(struct device *);struct device *parent;struct class *class;dev_t devt;struct device_node *of_node;void *data;void *managed[8];unsigned allocations;bool added;};
struct cdev {const struct file_operations *fops;void *owner;struct device *parent;bool added;};
struct platform_device {struct device dev;};
static struct device *get_device(struct device *d){assert(atomic_fetch_add(&d->refs,1)>0);return d;}
static void put_device(struct device *d){assert(d);unsigned old=atomic_fetch_sub(&d->refs,1);assert(old);if(old==1&&d->release)d->release(d);}
static void device_initialize(struct device *d){atomic_init(&d->refs,1);}
static void platform_set_drvdata(struct platform_device *p,void *d){p->dev.data=d;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static void *devm_kzalloc(struct device *d,size_t size,int flags){void *p=kzalloc(size,flags);if(p){assert(d->allocations<8);d->managed[d->allocations++]=p;}return p;}
static void managed_cleanup(struct device *d){for(unsigned i=0;i<d->allocations;i++)kfree(d->managed[i]);d->allocations=0;}
static int fail_stage;
static unsigned next_major=10,regions,classes;
static dev_t active_regions[8];
static bool assert_region;
static int alloc_chrdev_region(dev_t *d,unsigned minor,unsigned n,const char *name){assert(!minor&&n==1&&name);if(fail_stage==1)return -ENOMEM;*d=(next_major++)<<8;for(unsigned i=0;i<8;i++)if(!active_regions[i]){active_regions[i]=*d;regions++;return 0;}assert(0);}
static void unregister_chrdev_region(dev_t d,unsigned n){assert(n==1);if(assert_region)assert(d);for(unsigned i=0;i<8;i++)if(active_regions[i]==d&&d){active_regions[i]=0;assert(regions-->0);return;}}
static struct class *class_create(const char *name){assert(name);if(fail_stage==2)return ERR_PTR(-ENOMEM);struct class *c=kzalloc(sizeof(*c),0);if(!c)return ERR_PTR(-ENOMEM);classes++;return c;}
static void class_destroy(struct class *c){assert(c&&!IS_ERR(c)&&classes-->0);kfree(c);}
static int dev_set_name(struct device *d,const char *fmt,const char *name){assert(d&&fmt&&name);return fail_stage==3?-ENOMEM:0;}
static void standalone_release(struct device *d){kfree(d);}
static struct device *device_create(struct class *c,struct device *parent,dev_t devt,void *data,const char *name){(void)name;if(fail_stage==3)return ERR_PTR(-ENOMEM);struct device *d=kzalloc(sizeof(*d),0);if(!d)return ERR_PTR(-ENOMEM);device_initialize(d);d->release=standalone_release;d->class=c;d->parent=parent;d->devt=devt;d->data=data;d->added=true;return d;}
static struct device *native_devices[8];
static void device_destroy(struct class *c,dev_t devt){for(unsigned i=0;i<8;i++)if(native_devices[i]&&native_devices[i]->class==c&&native_devices[i]->devt==devt){put_device(native_devices[i]);native_devices[i]=NULL;return;}}
static struct device *tracked_device_create(struct class *c,struct device *p,dev_t t,void *v,const char *n){struct device *d=device_create(c,p,t,v,n);if(!IS_ERR(d)){for(unsigned i=0;i<8;i++)if(!native_devices[i]){native_devices[i]=d;break;}}return d;}
#define device_create tracked_device_create
static void cdev_init(struct cdev *c,const struct file_operations *f){c->fops=f;}
static int cdev_add(struct cdev *c,dev_t t,unsigned n){assert(t&&n==1);if(fail_stage==4)return -ENOMEM;c->added=true;return 0;}
static void cdev_del(struct cdev *c){c->added=false;}
static void model_publish_open(struct cdev *c);
static int cdev_device_add(struct cdev *c,struct device *d){if(fail_stage==5){model_publish_open(c);return -ENOMEM;}if(fail_stage==4)return -ENOMEM;get_device(d);c->parent=d;c->added=d->added=true;return 0;}
static void cdev_device_del(struct cdev *c,struct device *d){assert(c->parent==d&&c->added&&d->added);c->added=d->added=false;c->parent=NULL;put_device(d);}
static atomic_uint module_pins;
static bool fail_module_get;
static bool try_module_get(void *m){assert(m==THIS_MODULE);if(fail_module_get)return false;module_pins++;return true;}
static void module_put(void *m){assert(m==THIS_MODULE&&module_pins-->0);}
struct of_phandle_args {struct device_node *np;int args_count;u32 args[4];};
static struct device_node iommu_node={.refs=1};
static bool bad_version,bad_phandle,zero_args;
static bool of_property_read_bool(struct device_node *n,const char *name){assert(n);return !strcmp(name,"qcom,smmu-enabled")&&n->smmu;}
static int of_property_read_u32(struct device_node *n,const char *name,u32 *v){assert(n&&!strcmp(name,"qcom,smmu-version"));if(bad_version)return -EINVAL;*v=2;return 0;}
static int of_property_read_u64(struct device_node *n,const char *name,u64 *v){assert(n&&!strcmp(name,"qcom,smmu-sid-mask"));*v=0xff;return 0;}
static int of_parse_phandle_with_args(struct device_node *n,const char *p,const char *cells,int index,struct of_phandle_args *s){assert(n&&!strcmp(p,"iommus")&&!strcmp(cells,"#iommu-cells")&&!index);if(bad_phandle)return -EPROBE_DEFER;s->np=&iommu_node;iommu_node.refs++;s->args_count=zero_args?0:1;s->args[0]=0x12a;return 0;}
static void of_node_put(struct device_node *n){assert(n&&n->refs>1);n->refs--;}
struct scatterlist {dma_addr_t dma_address;phys_addr_t physical;size_t dma_length;unsigned length;};
struct sg_table {struct scatterlist *sgl;unsigned nents,orig_nents;};
#define sg_dma_address(s) ((s)->dma_address)
#define sg_dma_len(s) ((s)->dma_length)
#define sg_phys(s) ((s)->physical)
#define for_each_sgtable_dma_sg(t,s,i) for((i)=0;(i)<(t)->nents&&((s)=&(t)->sgl[i],true);(i)++)
#define for_each_sgtable_sg(t,s,i) for((i)=0;(i)<(t)->orig_nents&&((s)=&(t)->sgl[i],true);(i)++)
struct dma_buf {size_t size;atomic_uint refs;struct scatterlist sg[2];struct sg_table table;bool secure;unsigned cpu,vmap;char bytes[8];};
struct dma_buf_attachment {struct dma_buf *buffer;struct device *dev;bool mapped;};
static struct dma_buf buffers[2],*fd_table[8];
static unsigned attachments,maps,unmaps,attach_calls,map_calls;
static bool fail_attach,fail_map;
static pthread_mutex_t dma_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dma_changed=PTHREAD_COND_INITIALIZER;
static bool block_map,map_entered,map_release;
static struct dma_buf *dma_buf_get(int fd){if(fd<0||fd>=8||!fd_table[fd])return ERR_PTR(-EBADF);struct dma_buf *b=fd_table[fd];assert(atomic_fetch_add(&b->refs,1)>0);return b;}
static void dma_buf_put(struct dma_buf *b){assert(b&&!IS_ERR(b)&&atomic_fetch_sub(&b->refs,1)>1);}
static struct dma_buf_attachment *dma_buf_attach(struct dma_buf *b,struct device *d){assert(b&&d);attach_calls++;if(fail_attach)return ERR_PTR(-EIO);struct dma_buf_attachment *a=kzalloc(sizeof(*a),0);if(!a)return ERR_PTR(-ENOMEM);a->buffer=b;a->dev=d;attachments++;return a;}
static struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *a,int direction){assert(a&&direction==0&&!a->mapped);map_calls++;if(block_map){pthread_mutex_lock(&dma_gate);map_entered=true;pthread_cond_broadcast(&dma_changed);while(!map_release)pthread_cond_wait(&dma_changed,&dma_gate);pthread_mutex_unlock(&dma_gate);}if(fail_map)return ERR_PTR(-EIO);a->mapped=true;maps++;return &a->buffer->table;}
static void dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *a,struct sg_table *t,int d){assert(a&&a->mapped&&t==&a->buffer->table&&!d);a->mapped=false;unmaps++;}
static void dma_buf_detach(struct dma_buf *b,struct dma_buf_attachment *a){assert(a&&a->buffer==b&&!a->mapped&&attachments-->0);kfree(a);}
struct iosys_map {void *vaddr;};
static int dma_buf_begin_cpu_access(struct dma_buf *b,int d){assert(b&&!d);b->cpu++;return 0;}
static int dma_buf_end_cpu_access(struct dma_buf *b,int d){assert(b&&!d&&b->cpu-->0);return 0;}
static int dma_buf_vmap_unlocked(struct dma_buf *b,struct iosys_map *m){b->vmap++;m->vaddr=b->bytes;return 0;}
static void dma_buf_vunmap_unlocked(struct dma_buf *b,struct iosys_map *m){assert(m->vaddr==b->bytes&&b->vmap-->0);}
static int dma_buf_get_flags(struct dma_buf *b,unsigned long *f){assert(b);*f=0;return 0;}
struct qcom_scm_vmperm {int vmid,perm;};
#define QCOM_SCM_VMID_HLOS 3
#define VMID_LPASS 0x16
#define VMID_ADSP_HEAP 0x25
#define PERM_READ 4
#define PERM_WRITE 2
#define PERM_EXEC 1
static bool fail_assign,fail_unassign;
static unsigned assignments,unassignments;
static int qcom_scm_assign_mem(phys_addr_t p,size_t len,u64 *sources,const struct qcom_scm_vmperm *dest,int count){struct dma_buf *b=NULL;for(unsigned i=0;i<2;i++)if(buffers[i].sg[0].physical==p&&buffers[i].size==len)b=&buffers[i];assert(b);if(*sources==BIT_ULL(QCOM_SCM_VMID_HLOS)){assert(count==2&&dest[0].vmid==VMID_LPASS&&dest[1].vmid==VMID_ADSP_HEAP);if(fail_assign)return -EIO;assert(!b->secure);b->secure=true;assignments++;}else{assert(*sources==(BIT_ULL(VMID_LPASS)|BIT_ULL(VMID_ADSP_HEAP))&&count==1&&dest[0].vmid==QCOM_SCM_VMID_HLOS);if(fail_unassign)return -EIO;assert(b->secure);b->secure=false;unassignments++;}return 0;}
/* UAPI */
struct of_device_id {const char *compatible;};
struct platform_driver {struct {const char *name;void *owner;const struct of_device_id *of_match_table;bool suppress_bind_attrs;} driver;int (*probe)(struct platform_device *);void (*remove)(struct platform_device *);};
static int platform_driver_register(struct platform_driver *p){(void)p;return 0;}
static void platform_driver_unregister(struct platform_driver *p){(void)p;}
/* SOURCE */
static struct device_node main_node={.smmu=true,.refs=1},cma_node={.refs=1};
static struct platform_device main_platform={.dev.of_node=&main_node},cma_platform={.dev.of_node=&cma_node};
static struct inode inodes[4];
static struct file files[4];
static struct inode publish_inode;
static struct file publish_file;
static const struct file_operations *publish_fops;
static void model_publish_open(struct cdev *c){publish_inode.i_cdev=c;publish_file.f_inode=&publish_inode;publish_fops=c->fops;assert(!publish_fops->open(&publish_inode,&publish_file));assert(!publish_fops->unlocked_ioctl(&publish_file,IOCTL_MAP_PHYS_ADDR,0));assert(!publish_fops->unlocked_ioctl(&publish_file,IOCTL_MAP_HYP_ASSIGN,0));fail_unassign=true;}
static void init(void){for(unsigned i=0;i<2;i++){buffers[i].size=8192;atomic_init(&buffers[i].refs,1);buffers[i].sg[0]=(struct scatterlist){.dma_address=0x100000+i*0x10000,.physical=0x100000+i*0x10000,.dma_length=8192,.length=8192};buffers[i].table=(struct sg_table){.sgl=buffers[i].sg,.nents=1,.orig_nents=1};}fd_table[0]=fd_table[2]=&buffers[0];fd_table[1]=&buffers[1];device_initialize(&main_platform.dev);device_initialize(&cma_platform.dev);}
static int bind(struct platform_device *p){int ret=msm_audio_ion_probe(p);if(ret)managed_cleanup(&p->dev);return ret;}
static void remove_provider(struct platform_device *p){msm_audio_ion_remove(p);managed_cleanup(&p->dev);}
static void open_file(struct platform_device *p,unsigned f){struct msm_audio_ion_private *priv=platform_get_drvdata(p);inodes[f].i_cdev=&priv->cdev;files[f].f_inode=&inodes[f];assert(!msm_audio_ion_open(&inodes[f],&files[f]));}
static long ioctl_fd(unsigned f,unsigned cmd,unsigned long fd){return msm_audio_ion_ioctl(&files[f],cmd,fd);}
static void close_file(unsigned f){assert(!msm_audio_ion_release(&inodes[f],&files[f]));}
static void clean(void){assert(!live&&!regions&&!classes&&!attachments&&maps==unmaps&&!module_pins&&iommu_node.refs==1&&main_platform.dev.refs==1&&cma_platform.dev.refs==1);for(unsigned i=0;i<2;i++)assert(buffers[i].refs==1&&!buffers[i].secure&&!buffers[i].cpu&&!buffers[i].vmap);}
static unsigned basic(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));dma_addr_t addr;size_t len;assert(!msm_audio_get_phy_addr(0,&addr,&len)&&addr==0x100000&&len==8192);assert(!ioctl_fd(0,IOCTL_UNMAP_PHYS_ADDR,0));assert(msm_audio_get_phy_addr(0,&addr,&len)==-ENOENT);close_file(0);remove_provider(&cma_platform);clean();return 3;}
static unsigned identity(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));fd_table[0]=&buffers[1];dma_addr_t addr;size_t len;assert(msm_audio_get_phy_addr(0,&addr,&len)==-ENOENT);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(!msm_audio_get_phy_addr(0,&addr,&len)&&addr==0x110000);fd_table[0]=&buffers[0];assert(!msm_audio_get_phy_addr(0,&addr,&len)&&addr==0x100000);close_file(0);remove_provider(&cma_platform);clean();return 4;}
static unsigned sharing(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);open_file(&cma_platform,1);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,2));assert(maps==1);assert(ioctl_fd(1,IOCTL_UNMAP_PHYS_ADDR,0)==-ENOENT);assert(!ioctl_fd(1,IOCTL_MAP_PHYS_ADDR,0)&&maps==1);close_file(0);dma_addr_t addr;size_t len;assert(!msm_audio_get_phy_addr(0,&addr,&len));assert(!ioctl_fd(1,IOCTL_UNMAP_PHYS_ADDR,0));close_file(1);remove_provider(&cma_platform);clean();return 5;}
static unsigned ranges(void){init();assert(!bind(&main_platform));open_file(&main_platform,0);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));dma_addr_t addr;size_t len;assert(!msm_audio_get_phy_addr(0,&addr,&len)&&addr==((0x2aULL<<32)|0x100000));assert(ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0)==-EINVAL&&!assignments);assert(!ioctl_fd(0,IOCTL_UNMAP_PHYS_ADDR,0));buffers[0].sg[0].dma_address=0x100000000ULL;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EOVERFLOW);buffers[0].sg[0].dma_address=0x100000;buffers[0].sg[0].dma_length=4096;buffers[0].sg[1]=(struct scatterlist){.dma_address=0x200000,.physical=0x200000,.dma_length=4096,.length=4096};buffers[0].table.nents=buffers[0].table.orig_nents=2;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EINVAL);close_file(0);remove_provider(&main_platform);assert(!bind(&cma_platform));open_file(&cma_platform,0);buffers[0].sg[1].dma_address=0x101000;buffers[0].sg[0].length=4096;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EINVAL);buffers[0].sg[1].physical=0x101000;buffers[0].sg[0].length=4096;assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));close_file(0);remove_provider(&cma_platform);clean();return 6;}
static unsigned secure(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));fail_assign=true;assert(ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0)==-EIO&&!buffers[0].secure);fail_assign=false;assert(!ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0)&&buffers[0].secure);assert(!ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0)&&assignments==1);fail_unassign=true;assert(ioctl_fd(0,IOCTL_UNMAP_PHYS_ADDR,0)==-EIO&&buffers[0].secure&&attachments==1);fail_unassign=false;assert(!ioctl_fd(0,IOCTL_UNMAP_PHYS_ADDR,0)&&!buffers[0].secure);close_file(0);remove_provider(&cma_platform);clean();return 5;}
static unsigned quarantine(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(!ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0));fail_unassign=true;close_file(0);assert(buffers[0].secure&&attachments==1&&module_pins==1);remove_provider(&cma_platform);assert(buffers[0].secure&&attachments==1&&cma_platform.dev.refs==2);msm_audio_ion_crash_handler();assert(buffers[0].secure&&attachments==1);fail_unassign=false;msm_audio_ion_crash_handler();clean();return 3;}
static unsigned faults(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);for(int n=0;n<3;n++){fail_alloc=n;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-ENOMEM&&buffers[0].refs==1&&!attachments);}fail_alloc=-1;fail_attach=true;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EIO);fail_attach=false;fail_map=true;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EIO&&!attachments);fail_map=false;fail_module_get=true;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-ENODEV&&!attachments);fail_module_get=false;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,7)==-EBADF);assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,1ULL<<32)==-EINVAL);dma_addr_t a;size_t len;assert(msm_audio_get_phy_addr(0,NULL,&len)==-EINVAL&&msm_audio_get_phy_addr(0,&a,NULL)==-EINVAL);close_file(0);remove_provider(&cma_platform);clean();return 10;}
static unsigned cleanup(void){init();assert_region=true;for(int n=1;n<=4;n++){fail_stage=n;assert(bind(&cma_platform)==-ENOMEM);assert(!live&&!regions&&!classes&&cma_platform.dev.refs==1);}fail_stage=0;bad_version=true;assert(bind(&main_platform)==-EINVAL);bad_version=false;bad_phandle=true;assert(bind(&main_platform)==-EPROBE_DEFER);bad_phandle=false;zero_args=true;assert(bind(&main_platform)==-EINVAL);zero_args=false;clean();return 7;}
static unsigned crash_secure(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(!ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0));fail_unassign=true;msm_audio_ion_crash_handler();assert(buffers[0].secure&&attachments==1&&buffers[0].refs==2);fail_unassign=false;msm_audio_ion_crash_handler();assert(!buffers[0].secure&&!attachments);close_file(0);remove_provider(&cma_platform);clean();return 2;}
static unsigned shared_secure(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);open_file(&cma_platform,1);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(!ioctl_fd(1,IOCTL_MAP_PHYS_ADDR,0));assert(!ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0));assert(!ioctl_fd(0,IOCTL_UNMAP_PHYS_ADDR,0)&&buffers[0].secure&&attachments==1);assert(!ioctl_fd(1,IOCTL_UNMAP_HYP_ASSIGN,0)&&!buffers[0].secure);assert(!ioctl_fd(1,IOCTL_UNMAP_HYP_ASSIGN,0)&&unassignments==1);assert(!ioctl_fd(1,IOCTL_UNMAP_PHYS_ADDR,0));close_file(0);close_file(1);remove_provider(&cma_platform);clean();return 4;}
static unsigned providers(void){init();assert(!bind(&main_platform));assert(!bind(&cma_platform));open_file(&main_platform,0);open_file(&cma_platform,1);assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(ioctl_fd(1,IOCTL_MAP_PHYS_ADDR,0)==-EBUSY);assert(!ioctl_fd(1,IOCTL_MAP_PHYS_ADDR,1));remove_provider(&main_platform);dma_addr_t address;size_t len;assert(msm_audio_get_phy_addr(0,&address,&len)==-ENOENT);assert(!msm_audio_get_phy_addr(1,&address,&len)&&address==0x110000);close_file(0);close_file(1);remove_provider(&cma_platform);clean();return 3;}
static unsigned boundaries(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);buffers[0].size=0;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EINVAL);buffers[0].size=8192;buffers[0].table.nents=0;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EINVAL);buffers[0].table.nents=1;buffers[0].sg[0].dma_address=UINT64_MAX-4095;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EOVERFLOW);buffers[0].sg[0].dma_address=0x100000;buffers[0].sg[0].physical=UINT64_MAX-4095;assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-EOVERFLOW);buffers[0].sg[0].physical=0x100001;assert(!ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0));assert(ioctl_fd(0,IOCTL_MAP_HYP_ASSIGN,0)==-EINVAL&&!assignments);close_file(0);remove_provider(&cma_platform);clean();return 5;}
static unsigned publication(void){init();fail_stage=5;assert(bind(&cma_platform)==-ENOMEM&&buffers[0].secure&&attachments==1&&!regions&&!classes);assert(publish_fops->unlocked_ioctl(&publish_file,IOCTL_MAP_PHYS_ADDR,0)==-ENODEV);fail_unassign=false;assert(!publish_fops->release(&publish_inode,&publish_file));clean();return 2;}
static unsigned open_remove(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);remove_provider(&cma_platform);assert(ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-ENODEV);close_file(0);clean();return 1;}
static long map_result;
static atomic_bool removal_done;
static void *map_thread(void *unused){(void)unused;map_result=ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0);return NULL;}
static void *remove_thread(void *unused){(void)unused;remove_provider(&cma_platform);atomic_store(&removal_done,true);return NULL;}
static unsigned removal(void){init();assert(!bind(&cma_platform));open_file(&cma_platform,0);block_map=true;pthread_t mapper,remover;assert(!pthread_create(&mapper,NULL,map_thread,NULL));pthread_mutex_lock(&dma_gate);while(!map_entered)pthread_cond_wait(&dma_changed,&dma_gate);pthread_mutex_unlock(&dma_gate);assert(!pthread_create(&remover,NULL,remove_thread,NULL));struct timespec pause={.tv_nsec=20000000};nanosleep(&pause,NULL);assert(!atomic_load(&removal_done));pthread_mutex_lock(&dma_gate);map_release=true;pthread_cond_broadcast(&dma_changed);pthread_mutex_unlock(&dma_gate);assert(!pthread_join(mapper,NULL)&&!pthread_join(remover,NULL));assert(!map_result&&!attachments&&ioctl_fd(0,IOCTL_MAP_PHYS_ADDR,0)==-ENODEV);close_file(0);clean();return 1;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;if(!strcmp(argv[1],"basic"))n=basic();else if(!strcmp(argv[1],"identity"))n=identity();else if(!strcmp(argv[1],"sharing"))n=sharing();else if(!strcmp(argv[1],"ranges"))n=ranges();else if(!strcmp(argv[1],"secure"))n=secure();else if(!strcmp(argv[1],"quarantine"))n=quarantine();else if(!strcmp(argv[1],"faults"))n=faults();else if(!strcmp(argv[1],"cleanup"))n=cleanup();else if(!strcmp(argv[1],"crash_secure"))n=crash_secure();else if(!strcmp(argv[1],"shared_secure"))n=shared_secure();else if(!strcmp(argv[1],"providers"))n=providers();else if(!strcmp(argv[1],"boundaries"))n=boundaries();else if(!strcmp(argv[1],"publication"))n=publication();else if(!strcmp(argv[1],"open_remove"))n=open_remove();else if(!strcmp(argv[1],"removal"))n=removal();else abort();printf("{\"ion_%s_cases\":%u}\n",argv[1],n);}
