#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
typedef uint32_t u32;
typedef uint64_t u64;
#define __user
#define PAGE_SIZE 4096
#define DISP_LOG_BUF_SIZE 4096
#define GFP_KERNEL 0
#define MI_INTF_DSI 1
#define LOG_TYPE_USER 1
#define LOG_TYPE_MAX 2
#define DISP_DEBUG(...) ((void)0)
#define DISP_ERROR(...) ((void)0)
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define kmalloc(n,f) malloc(n)
#define kfree(p) free(p)
#define mutex_lock_interruptible(p) pthread_mutex_lock(p)
#define mutex_unlock(p) pthread_mutex_unlock(p)
#define spin_lock_irqsave(p,f) do {(f)=0;assert(!pthread_mutex_lock(p));}while(0)
#define spin_unlock_irqrestore(p,f) do {(void)(f);assert(!pthread_mutex_unlock(p));}while(0)
struct log_pos {u32 wrap,offset,lastoffset,remainsize;};
struct log_buf {pthread_mutex_t lock;int wait;struct log_pos pos;size_t size;char *buf;};
struct disp_log_read {struct log_pos read_pos;unsigned log_type;char buf[DISP_LOG_BUF_SIZE];pthread_mutex_t lock;};
struct disp_log {pthread_mutex_t lock;struct log_buf log[LOG_TYPE_MAX];};
struct disp_display {int intf_type;void *display;};
struct inode {struct disp_display *data;};
struct file {struct inode *inode;void *private_data;};
struct dsi_panel {int dummy;};
enum {DSI_CMD_SET_PRE_ON,DSI_CMD_SET_MAX=300};
static const char *cmd_set_prop_map[DSI_CMD_SET_MAX];
static int copy_fault,cmd_type,callbacks;
static size_t expected_length;
static unsigned long long local_clock(void) {return 12345678900000ULL;}
static void atomic_set(int *p,int value) {*p=value;}
static int scnprintf(char *s,size_t n,const char *fmt,...) {va_list ap;va_start(ap,fmt);int ret=vsnprintf(s,n,fmt,ap);va_end(ap);return n?(ret>=(int)n?(int)n-1:ret):0;}
static int vscnprintf(char *s,size_t n,const char *fmt,va_list ap) {int ret=vsnprintf(s,n,fmt,ap);return n?(ret>=(int)n?(int)n-1:ret):0;}
static int copy_from_user(void *dst,const void *src,size_t n) {if(copy_fault)return 1;memcpy(dst,src,n);return 0;}
static struct inode *file_inode(struct file *f) {return f->inode;}
static void *pde_data(struct inode *i) {return i->data;}
static const char *get_disp_intf_type_name(int type) {(void)type;return "dsi";}
static char *strim(char *s) {return s;}
static int kstrtoint(const char *s,int base,int *v) {char *end;long x=strtol(s,&end,base);if(!*s||(*end&&*end!='\n'))return -EINVAL;*v=x;return 0;}
static int mi_dsi_display_write_dsi_cmd_set(void *d,int type) {assert(d);cmd_type=type;callbacks++;return 0;}
static int mi_dsi_display_set_mipi_rw(void *d,char *s) {assert(d&&strlen(s)==expected_length);callbacks++;return 0;}
static void mi_dsi_display_set_dsi_phy_rw(struct disp_display *d,char *s) {assert(d&&strlen(s)==expected_length);callbacks++;}
static void mi_dsi_display_set_dsi_porch_rw(struct disp_display *d,char *s) {assert(d&&strlen(s)==expected_length);callbacks++;}
static int mi_disp_log_user_printk(struct log_buf *log,const char *fmt,...);
/* FUNCTIONS */
static int mi_disp_log_user_printk(struct log_buf *log,const char *fmt,...) {va_list ap;va_start(ap,fmt);int ret=mi_disp_log_printk_func(log,fmt,ap);va_end(ap);return ret;}
typedef ssize_t (*writer)(struct file *,const char *,size_t,loff_t *);
int main(void) {
 char *text=malloc(PAGE_SIZE+1);assert(text);memset(text,'a',PAGE_SIZE+1);struct disp_display dd={.intf_type=MI_INTF_DSI,.display=&dd};struct inode inode={.data=&dd};struct file file={.inode=&inode};loff_t pos=0;
 writer stack[]={mi_disp_procfs_dsi_phy_rw_write,mi_disp_procfs_dsi_porch_rw_write,mi_disp_procfs_pps_rw_write};int writes=0;
 for(unsigned w=0;w<3;w++)for(size_t n=0;n<=PAGE_SIZE+1;n++){expected_length=n;ssize_t r=stack[w](&file,text,n,&pos);assert(r==(n>=512?-E2BIG:(ssize_t)n));writes++;}
 for(size_t n=0;n<=PAGE_SIZE+1;n++){expected_length=n;ssize_t r=mi_disp_procfs_mipi_rw_write(&file,text,n,&pos);assert(r==(n>PAGE_SIZE?-E2BIG:(ssize_t)n));writes++;}
 assert(mi_disp_procfs_tx_cmd_set_write(&file,"23",2,&pos)==2&&cmd_type==23);assert(mi_disp_procfs_tx_cmd_set_write(&file,text,0,&pos)==0);assert(mi_disp_procfs_tx_cmd_set_write(&file,text,PAGE_SIZE+1,&pos)==-E2BIG);
 copy_fault=1;for(unsigned w=0;w<3;w++)assert(stack[w](&file,text,10,&pos)==-EFAULT);assert(mi_disp_procfs_mipi_rw_write(&file,text,10,&pos)==-EFAULT);copy_fault=0;
 struct disp_log log={.lock=PTHREAD_MUTEX_INITIALIZER};for(int i=0;i<LOG_TYPE_MAX;i++){log.log[i].lock=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;log.log[i].size=DISP_LOG_BUF_SIZE;log.log[i].buf=calloc(DISP_LOG_BUF_SIZE,1);assert(log.log[i].buf);}file.private_data=&log;int format_cases=0;
 for(size_t n=0;n<=PAGE_SIZE+1;n++){memset(text,'x',PAGE_SIZE+1);expected_length=n;ssize_t r=mi_disp_log_write(&file,text,n,&pos);assert(r==(n>PAGE_SIZE?-E2BIG:(ssize_t)n));format_cases++;}
 for(size_t n=0;n<PAGE_SIZE;n++){text[n]=0;int r=mi_disp_log_user_printk(&log.log[1],"%s",text);assert(r>=0&&r<=1023);text[n]='x';format_cases++;}
 struct log_buf *ring=&log.log[0];struct disp_log_read read={.lock=PTHREAD_MUTEX_INITIALIZER};int ring_cases=0;
 for(unsigned i=0;i<DISP_LOG_BUF_SIZE;i++)ring->buf[i]=(char)(i%251);
 const unsigned offsets[]={0,1,4094,4095,4096,UINT32_MAX};
 const unsigned wraps[]={0,1,2,UINT32_MAX};
 for(unsigned a=0;a<6;a++)for(unsigned end=0;end<DISP_LOG_BUF_SIZE;end+=31)for(unsigned w=0;w<4;w++)for(unsigned limit=0;limit<=DISP_LOG_BUF_SIZE+1;limit+=511){
  ring->pos.wrap=1;ring->pos.offset=end;read.read_pos.wrap=wraps[w];read.read_pos.offset=offsets[a];unsigned distance=ring->pos.wrap-read.read_pos.wrap,start=read.read_pos.offset;
  if(start>=DISP_LOG_BUF_SIZE||distance>1||(distance==1&&end>=start))start=(end+1)%DISP_LOG_BUF_SIZE;
  unsigned available=(end+DISP_LOG_BUF_SIZE-start)%DISP_LOG_BUF_SIZE,expect=available<limit?available:limit;if(expect>DISP_LOG_BUF_SIZE)expect=DISP_LOG_BUF_SIZE;
  int r=mi_disp_log_read_stats(ring,&read,limit);assert(r==(int)expect);for(int i=0;i<r;i++)assert(read.buf[i]==ring->buf[(start+i)%DISP_LOG_BUF_SIZE]);assert(read.read_pos.offset<DISP_LOG_BUF_SIZE);ring_cases++;
 }
 struct dsi_panel panel={0};for(int i=0;i<DSI_CMD_SET_MAX;i++)cmd_set_prop_map[i]="long_command_name_repeated_to_exhaust_the_buffer";int show_cases=0;
 for(size_t n=1;n<=PAGE_SIZE;n++){char *buf=malloc(n);assert(buf);ssize_t r=mi_dsi_panel_show_dsi_cmd_set_type(&panel,buf,n);assert(r>=0&&(size_t)r<n&&buf[r]==0);free(buf);show_cases++;}
 assert(mi_dsi_panel_show_dsi_cmd_set_type(&panel,text,0)==-EINVAL);
 for(int i=0;i<LOG_TYPE_MAX;i++)free(log.log[i].buf);free(text);
 printf("{\"procfs_write_bounds_cases\":%d,\"log_format_and_write_cases\":%d,\"log_ring_wrap_cases\":%d,\"command_list_truncation_cases\":%d}\n",writes+7,format_cases,ring_cases,show_cases+1);
}
