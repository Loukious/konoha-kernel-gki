#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define X_Y_MAX 64
#define TOUCH_KEY_NUM 0
#define GFP_KERNEL 0
#define NVT_LOG(...) do {} while(0)
#define NVT_ERR(...) do {} while(0)
#define printk(...) do {} while(0)
#define kzalloc(n,f) calloc(1,n)
#define kfree(p) free(p)
static long simple_strtol(const char *s,char **p,int base){return strtol(s,p,base);}
static int kstrtou8(const char *s,int base,uint8_t *v){char *end;errno=0;long n=strtol(s,&end,base);if(errno||*end||end==s||n<0||n>255)return -EINVAL;*v=n;return 0;}
static void nvt_print_result_log_in_one_line(uint8_t *a,unsigned n){}
static void nvt_print_data_array(int32_t *a,int x,int y){}
/* FUNCTIONS */
int main(void){unsigned cases=0;char src[1024],dest[65];
for(unsigned n=0;n<1024;n++){memset(src,'x',n);src[n]=0;memset(dest,0xaa,sizeof(dest));int r=copy_this_line(dest,src,64);assert((r==0)==(n<64));assert((unsigned char)dest[64]==0xaa);cases++;}
char no_newline[]="unterminated";char *p=no_newline;goto_next_line(&p);assert(p==no_newline+strlen(no_newline));cases++;
for(unsigned size=1;size<=64;size++){for(unsigned count=0;count<=size+2;count++){strcpy(src,"AIN:\n");for(unsigned i=0;i<count;i++)strcat(src,"1,");uint8_t *a=calloc(size,1);p=src;int ret=parse_mp_setting_ain_array(&p,"AIN:",a,size);assert((ret==0)==(count==size));free(a);cases++;}}
strcpy(src,"AIN:\n0xFF,0xff,255,");uint8_t a[3];p=src;assert(!parse_mp_setting_ain_array(&p,"AIN:",a,3));for(int i=0;i<3;i++)assert(a[i]==255);cases++;
for(int x=1;x<=64;x++){for(int count=0;count<=x+2;count++){strcpy(src,"LIMIT:\n");for(int i=0;i<count;i++)strcat(src,"12,");int32_t *a=calloc(x,sizeof(*a));p=src;int ret=parse_mp_criteria_item_array(&p,"LIMIT:",a,x,1);assert((ret==0)==(count==x));free(a);cases++;}}
int32_t value=123;p="LIMIT:\nnot_a_number";assert(parse_mp_setting_criteria_item(&p,"LIMIT:",&value)<0);assert(value==123);cases++;
printf("{\"csv_boundary_cases\":%u,\"unterminated_last_line_safe\":true,\"columns_and_ain_count_bounded_before_store\":true,\"long_lines_rejected\":true}\n",cases);}
