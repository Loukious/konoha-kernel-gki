#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#define __user
#define ERESTARTSYS 512
#define NVT_TOUCH_ESD_PROTECT 0
#define NVT_LOG(...) do {} while(0)
struct file {int unused;};static struct {int lock;} state,*ts=&state;
static int fault;static size_t requested;
static int mutex_lock_interruptible(int *m){assert(!(*m)++);return 0;}
static void mutex_unlock(int *m){assert((*m)--);}
static int nvt_xm_htc_get_op_mode(int16_t *value){if(fault)return -EIO;*value=42;return 0;}
static ssize_t simple_read_from_buffer(void *dest,size_t count,loff_t *offset,const void *source,size_t length){assert(count==requested);if(*offset<0)return -EINVAL;if(*offset>=length||!count)return 0;size_t n=length-*offset;if(n>count)n=count;assert(n<=count);memcpy(dest,(const char *)source+*offset,n);*offset+=n;return n;}
/* FUNCTIONS */
int main(void){struct file f;unsigned cases=0;char output[129],reference[]="op_mode: 42\n";for(size_t n=0;n<128;n++){for(loff_t pos=0;pos<64;pos++){memset(output,0x55,sizeof(output));loff_t off=pos;requested=n;ssize_t ret=xm_htc_op_mode_proc_read(&f,output,n,&off);size_t expected=pos>=strlen(reference)?0:strlen(reference)-pos;if(expected>n)expected=n;assert(ret==(ssize_t)expected);assert(!memcmp(output,reference+(pos<strlen(reference)?pos:0),expected));for(size_t i=expected;i<sizeof(output);i++)assert(output[i]==0x55);assert(!ts->lock);cases++;}}
fault=1;loff_t off=0;requested=100;assert(xm_htc_op_mode_proc_read(&f,output,100,&off)==-EIO);assert(!ts->lock&&!off);printf("{\"proc_short_read_cases\":%u,\"uninitialized_stack_tail_not_copied\":true,\"firmware_read_errors_propagated\":true}\n",cases+1);}
