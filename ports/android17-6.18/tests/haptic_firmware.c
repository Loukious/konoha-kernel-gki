#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct firmware { size_t size;const unsigned char *data; };
struct aw8697_container { int len;unsigned char data[]; };
struct aw86927_container { int len;unsigned char data[]; };
struct mock_aw { int nv_flag,ram_init,rtp_init;struct {int check_sum,len,ram_shift;}ram; };
#define GFP_KERNEL 0
#define aw_info(...) ((void)0)
#define aw_err(...) ((void)0)
static const char *awinic_ram_name[]={"default","backup"};
static int released,live,fail_alloc,updates,triggers,numbers,rtp;
static void release_firmware(const struct firmware *firmware){if(firmware)released++;}
static int get_rb_max_size(void){return 8192;}
static void *kzalloc(size_t n,int flags){(void)flags;if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void kfree(void *p){assert(live==1);live--;free(p);}
static void aw8697_container_update(struct mock_aw *a,struct aw8697_container *c){(void)c;a->ram.ram_shift=4;updates++;}
static int aw86927_container_update(struct mock_aw *a,struct aw86927_container *c){(void)c;a->ram.ram_shift=4;updates++;return 0;}
static void aw8697_haptic_trig_enable_config(struct mock_aw *a){(void)a;triggers++;}
static void aw86927_trig_config(struct mock_aw *a){(void)a;triggers++;}
static void aw86927_haptic_get_ram_number(struct mock_aw *a){(void)a;numbers++;}
static void aw8697_rtp_update(struct mock_aw *a){(void)a;rtp++;}
/* SOURCE */
static void checksum(unsigned char *data,size_t n){unsigned short sum=0;for(size_t i=2;i<n;i++)sum+=data[i];data[0]=sum>>8;data[1]=sum;}
int main(void){
 int cases=0;void (*loaders[])(const struct firmware *,void *)={aw8697_ram_loaded,aw86927_ram_loaded};
 for(int chip=0;chip<2;chip++)for(int nv=0;nv<2;nv++){
  struct mock_aw a={.nv_flag=nv};released=updates=0;loaders[chip](NULL,&a);assert(!released&&!updates);
  for(size_t n=0;n<12;n++)for(int base=0;base<10;base++){
   unsigned char *data=malloc(n?n:1);assert(data);memset(data,1,n);if(n>=4){unsigned int address[]={0,1,2,3,4,1024,8191,8192,8193,65535};data[2]=address[base]>>8;data[3]=address[base];}if(n>=2)checksum(data,n);struct firmware f={n,data};released=updates=triggers=numbers=rtp=0;a.ram_init=0;loaders[chip](&f,&a);bool valid=n>=5&&base>=4&&base<=7;assert(released==1&&!live&&updates==valid&&triggers==valid&&a.ram_init==valid);free(data);cases++;
  }
  unsigned char data[8]={0,0,4,0,1,2,3,4};checksum(data,sizeof(data));struct firmware f={sizeof(data),data};released=updates=triggers=numbers=rtp=0;fail_alloc=1;loaders[chip](&f,&a);assert(released==1&&!live&&!updates);fail_alloc=0;cases++;
  released=updates=0;data[0]^=1;loaders[chip](&f,&a);assert(released==1&&!live&&!updates);cases++;
  f.size=128*1024+1;released=updates=0;loaders[chip](&f,&a);assert(released==1&&!updates);cases++;
 }
 printf("{\"malformed_header_checksum_fifo_cases\":%d,\"null_firmware_safe\":true,\"releases_on_all_failure_paths\":true}\n",cases);
}
