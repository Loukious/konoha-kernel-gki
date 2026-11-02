#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define hp_err(...) ((void)0)
#define hp_info(...) ((void)0)
#define READ_ONCE(x) (x)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define IOCTL_MMAP_BUF_SUM 16
#define IOCTL_WAIT_BUFF_VALID_MAX_TRY 100
#define MMAP_BUF_DATA_VALID 0x55
#define MMAP_BUF_DATA_INVALID 0xff
#define SIH_SYSSST_BIT_FIFO_AE 1
#define SIH_SYSSST_BIT_STANDBY 2
#define SIH_RTP_MODE 2
#define SIH688X_REG_RTPDATA 0
#define SIH688X_REG_SYSSST 1
#define SIH_I2C_OPERA_BYTE_ONE 1
#define PAGE_SIZE 4096
#define IOCTL_MMAP_PAGE_ORDER 2
#define VM_SHARED 1
#define VM_EXEC 2
#define VM_MAYEXEC 4
#define VM_DONTEXPAND 8
#define VM_DONTDUMP 16
struct mutex {int unused;};
static void mutex_lock(struct mutex *m){}
static void mutex_unlock(struct mutex *m){}
struct work_struct {int unused;};
typedef struct slot {int status,length;struct slot *kernel_next;char data[1000];} mmap_buf_format_t;
typedef struct sih_haptic sih_haptic_t;
struct funcs {void (*set_play_mode)(sih_haptic_t*,int);void (*set_rtp_aei)(sih_haptic_t*,bool);void (*clear_interrupt_state)(sih_haptic_t*);void (*play_go)(sih_haptic_t*,bool);};
struct sih_haptic {struct {struct work_struct stream_work;mmap_buf_format_t *start_buf;bool stream_mode,done_flag;} stream_para;struct mutex lock;struct funcs *hp_func;struct {void *regmapping;} regmapp;};
static unsigned int ticks,writes,limit;
static unsigned long sih_get_sys_msecs(void){return ticks++;}
static void msleep(int t){ticks+=t;}
static void usleep_range(int min,int max){ticks++;}
static void mode(sih_haptic_t *s,int m){}
static void toggle(sih_haptic_t *s,bool enable){}
static void clear(sih_haptic_t *s){}
static struct funcs funcs={mode,toggle,clear,toggle};
static int haptic_regmap_write(void *r,int reg,unsigned int length,const char *data){assert(length<=1000);writes++;return 0;}
static int haptic_regmap_read(void *r,int reg,int n,uint8_t *value){*value=writes>=limit?SIH_SYSSST_BIT_STANDBY:SIH_SYSSST_BIT_FIFO_AE;return 0;}
struct page {int refs;};
static struct page page={.refs=1};
struct vm_area_struct;
struct vm_operations_struct {void (*open)(struct vm_area_struct*);void (*close)(struct vm_area_struct*);};
struct vm_area_struct {unsigned long vm_start,vm_end,vm_flags,vm_pgoff;int vm_page_prot;void *vm_private_data;const struct vm_operations_struct *vm_ops;};
struct file {int unused;};
static sih_haptic_t *global;
static bool global_locked;
static sih_haptic_t *sih_lock_global(void){assert(!global_locked);if(global)global_locked=true;return global;}
static void sih_unlock_global(void){assert(global_locked);global_locked=false;}
static struct page *virt_to_page(void *p){return &page;}
static unsigned long page_to_pfn(struct page *p){return 123;}
static void get_page(struct page *p){assert(p==&page);p->refs++;}
static void put_page(struct page *p){assert(p==&page&&p->refs>0);p->refs--;}
static void vm_flags_set(struct vm_area_struct *v,unsigned long f){v->vm_flags|=f;}
static void vm_flags_clear(struct vm_area_struct *v,unsigned long f){v->vm_flags&=~f;}
static int mappings,map_failure;
static int remap_pfn_range(struct vm_area_struct *v,unsigned long start,unsigned long pfn,unsigned long len,int prot){assert(global_locked&&pfn==123&&len==16384);mappings++;return map_failure?-EIO:0;}
/* SOURCE */
int main(void){
 static mmap_buf_format_t slots[16];sih_haptic_t s={.hp_func=&funcs};s.stream_para.start_buf=slots;global=&s;
 for(int i=0;i<16;i++){slots[i].kernel_next=(void *)(uintptr_t)0xdeadbeef;slots[i].status=0;}
 haptic_clean_buf(&s,MMAP_BUF_DATA_INVALID);for(int i=0;i<16;i++)assert(slots[i].status==MMAP_BUF_DATA_INVALID);
 for(int i=0;i<16;i++){slots[i].status=MMAP_BUF_DATA_VALID;slots[i].length=i==15?1000:i;}
 s.stream_para.stream_mode=true;limit=16;stream_work_proc(&s.stream_para.stream_work);assert(writes==16&&!s.stream_para.stream_mode);
 for(int n=0;n<2;n++){writes=ticks=0;slots[0].status=MMAP_BUF_DATA_VALID;slots[0].length=n?-1:1001;s.stream_para.stream_mode=true;stream_work_proc(&s.stream_para.stream_work);assert(!writes&&!s.stream_para.stream_mode);}
 struct file file={0};struct vm_area_struct v={.vm_end=16384,.vm_flags=VM_SHARED|VM_MAYEXEC};assert(!memo_file_mmap(&file,&v)&&page.refs==2&&!global_locked);assert(!(v.vm_flags&VM_MAYEXEC));v.vm_ops->open(&v);assert(page.refs==3);put_page(&page);global=NULL;v.vm_ops->close(&v);v.vm_ops->close(&v);assert(!page.refs);assert(memo_file_mmap(&file,&v)==-ENODEV);global=&s;page.refs=1;
 for(int n=0;n<4;n++){v=(struct vm_area_struct){.vm_end=16384,.vm_flags=VM_SHARED};if(n==0)v.vm_flags|=VM_EXEC;if(n==1)v.vm_flags=0;if(n==2)v.vm_pgoff=1;if(n==3)v.vm_end=8192;int before=mappings;assert(memo_file_mmap(&file,&v)==-EINVAL&&mappings==before&&!global_locked&&page.refs==1);}
 map_failure=1;v=(struct vm_area_struct){.vm_end=16384,.vm_flags=VM_SHARED};assert(memo_file_mmap(&file,&v)==-EIO&&page.refs==1&&!global_locked);
 puts("{\"derived_stream_slots\":16,\"invalid_descriptor_lengths\":2,\"mapping_rejections\":6,\"mapping_page_lifetime\":true}");
}
