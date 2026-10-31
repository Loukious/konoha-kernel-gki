/* SERVICES */
#include <stdarg.h>
#define PAGE_SIZE 4096
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define REGDUMP_PRINT_LEN 8
#define REGDUMP_PRINT_STRIDE 4
#define pr_debug(...) ((void)0)
static int bad_copy;
static size_t copy_to_user(void*to,const void*from,size_t n){if(bad_copy)return n;memcpy(to,from,n);return 0;}
static int scnprintf(char*b,size_t n,const char*fmt,...){va_list args;va_start(args,fmt);int ret=vsnprintf(b,n,fmt,args);va_end(args);return n?(ret<(int)n?ret:n-1):0;}
static int regmap_read_bypassed(struct regmap*m,int i,unsigned*v){assert(m->cache_only);return regmap_read(m,i,v);}
/* SOURCE */
int main(void){static struct regmap map={.cache_only=true};char buf[PAGE_SIZE+1]={0};loff_t pos=0;int cases=0;assert(regdump_read(&map,0,65532,buf,SIZE_MAX,&pos)>0&&pos>0&&pos<65532);assert(buf[PAGE_SIZE]==0&&!live);cases++;
pos=4;bad_copy=1;assert(regdump_read(&map,0,65532,buf,40,&pos)==-EFAULT&&pos==4&&!live);bad_copy=0;cases++;
pos=INT64_MAX;assert(!regdump_read(&map,0,65532,buf,40,&pos));pos=-1;assert(!regdump_read(&map,0,65532,buf,40,&pos));cases+=2;
pos=0;assert(!regdump_read(&map,0,65532,buf,1,&pos)&&pos==0);fault=steps+1;assert(regdump_read(&map,0,65532,buf,40,&pos)==-ENOMEM&&pos==0);fault=0;assert(!live);cases+=2;printf("{\"proc\":%d}\n",cases);}
