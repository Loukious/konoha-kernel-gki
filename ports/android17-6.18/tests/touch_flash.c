#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef long loff_t;
#define __user
#define NVT_TRANSFER_LEN 64
#define GFP_KERNEL 0
#define GFP_DMA 0
#define NVT_TOUCH_ESD_PROTECT 0
#define NVT_LOG(...) do {} while (0)
#define NVT_ERR(...) do {} while (0)
#define unlikely(v) (v)
#define NVTREAD 0
#define NVTWRITE 1
#define kzalloc(n,f) calloc(1,n)
#define kfree(p) free(p)
struct file {int unused;};static struct {void *client;} panel,*ts=&panel;
static unsigned copies,transfers;static size_t accessible;
static int copy_from_user(void *d,const void *s,size_t n){assert(n<=accessible);memcpy(d,s,n);return 0;}
static int copy_to_user(void *d,const void *s,size_t n){assert(n<=accessible);memcpy(d,s,n);copies++;return 0;}
static int CTP_SPI_READ(void *client,uint8_t *buf,unsigned n){assert(n&&n<=accessible-2);memset(buf,0x42,n);transfers++;return 0;}
static int CTP_SPI_WRITE(void *client,uint8_t *buf,unsigned n){assert(n&&n<=accessible-2);transfers++;return 0;}
/* FUNCTIONS */
int main(void){unsigned cases=0;struct file f;uint8_t p[70];for(size_t n=3;n<=NVT_TRANSFER_LEN+3;n++){for(unsigned len=0;len<=100;len++){for(int write=0;write<2;write++){memset(p,0xa5,sizeof(p));p[0]=(write<<7)|(len>>8);p[1]=len;accessible=n;loff_t off=0;unsigned old=transfers;nvt_flash_read(&f,(char *)p,n,&off);assert((transfers>old)==(len&&len<=n-2));cases++;}}}accessible=3;p[0]=0x7f;p[1]=0xff;loff_t off=0;assert(nvt_flash_read(&f,(char *)p,3,&off)==-EINVAL);printf("{\"proc_spi_length_cases\":%u,\"declared_length_bounded_by_user_buffer\":true}\n",cases+1);}
