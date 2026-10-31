#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define dev_err(d,...) ((void)(d))
#define dev_info(d,...) ((void)(d))
#define BTPOWER_MBOX_MSG_MAX_LEN 64
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((long)(p))
#define ERR_PTR(n) ((void*)(intptr_t)(n))
typedef uint8_t u8;
struct device { int unused; };struct platform_device { struct device dev; };
struct platform_pwr_data { void *qmp,*nvmem_cell_fmd_set,*nvmem_cell_fmd_chg_pon,*nvmem_cell_fmd_cnt2_stop;struct platform_device*pdev;const char*vreg_ipa;bool ipa_config_done; };
static struct platform_device pdev;static struct platform_pwr_data data={.pdev=&pdev},*pwr_data=&data;
static int write_error,read_error,live,writes,reads,qmp_error,qmp_calls;static size_t read_size;static u8 stored,readback;static char frame[64];
static int nvmem_cell_write(void*cell,void*buf,size_t len){assert(cell&&!IS_ERR(cell)&&len==1);writes++;stored=*(u8*)buf;return write_error;}
static void*nvmem_cell_read(void*cell,size_t*len){assert(cell&&!IS_ERR(cell));reads++;if(read_error)return ERR_PTR(read_error);*len=read_size;u8*p=malloc(read_size?read_size:1);assert(p);live++;if(read_size)*p=readback;return p;}
static void kfree(void*p){assert(p&&!IS_ERR(p));live--;free(p);}
static long strscpy(char*dst,const char*src,size_t size){size_t len=strlen(src);if(len>=size)return -E2BIG;memcpy(dst,src,len+1);return len;}
static int qmp_send(void*qmp,const char*buf,size_t len){assert(qmp==(void*)1&&len==64);qmp_calls++;memcpy(frame,buf,len);return qmp_error;}
/* PARAM_ENUMS */
/* FUNCTIONS */
int main(void){
 int (*fn[])(unsigned char)={fmd_set_sdam_bit,fmd_reboot_on_usb_detection,fmd_write_stop_counter};int cases=0;
 for(int n=0;n<3;n++){
  data.nvmem_cell_fmd_set=data.nvmem_cell_fmd_chg_pon=data.nvmem_cell_fmd_cnt2_stop=NULL;writes=reads=0;assert(fn[n](0)==-ENODEV&&!writes&&!reads);cases++;
  data.nvmem_cell_fmd_set=data.nvmem_cell_fmd_chg_pon=data.nvmem_cell_fmd_cnt2_stop=ERR_PTR(-517);assert(fn[n](0)==-ENODEV&&!writes&&!reads);cases++;
  data.nvmem_cell_fmd_set=data.nvmem_cell_fmd_chg_pon=data.nvmem_cell_fmd_cnt2_stop=(void*)1;
  write_error=-EIO;assert(fn[n](8)==-EIO&&!reads);write_error=0;cases++;
  read_error=-ENOMEM;assert(fn[n](8)==-ENOMEM&&!live);read_error=0;cases++;
  read_size=0;assert(fn[n](8)==-EIO&&!live);cases++;
  read_size=1;readback=7;assert(fn[n](8)==-EIO&&!live);cases++;
  for(int i=0;i<256;i++){readback=i;assert(!fn[n](i)&&stored==i&&!live);cases++;}
 }
 data.qmp=(void*)1;
 for(int len=0;len<64;len++){char*s=malloc(len+1);memset(s,'x',len);s[len]=0;qmp_error=0;assert(!bt_aop_send_msg(&data,s));assert(!memcmp(frame,s,len+1));for(int i=len+1;i<64;i++)assert(!frame[i]);free(s);cases++;}
 char oversized[65];memset(oversized,'x',64);oversized[64]=0;int calls=qmp_calls;assert(bt_aop_send_msg(&data,oversized)==-EMSGSIZE&&calls==qmp_calls);cases++;
 qmp_error=-ETIMEDOUT;assert(bt_aop_send_msg(&data,"{x:1}")==-ETIMEDOUT);cases++;data.qmp=ERR_PTR(-517);assert(bt_aop_send_msg(&data,"x")==-ENODEV);cases++;
 data.qmp=(void*)1;data.vreg_ipa="vdd-test";qmp_error=-EIO;assert(btpower_enable_ipa_vreg(&data)==-EIO&&!data.ipa_config_done);cases++;
 qmp_error=0;assert(!btpower_enable_ipa_vreg(&data)&&data.ipa_config_done);calls=qmp_calls;assert(!btpower_enable_ipa_vreg(&data)&&qmp_calls==calls);cases+=2;
 struct platform_pwr_data replacement={.qmp=(void*)1,.vreg_ipa="vdd-new"};assert(!btpower_enable_ipa_vreg(&replacement)&&replacement.ipa_config_done&&qmp_calls==calls+1);assert(strstr(frame,"vdd-new"));for(size_t i=strlen(frame)+1;i<64;i++)assert(!frame[i]);cases++;
 replacement.ipa_config_done=false;replacement.vreg_ipa=oversized;calls=qmp_calls;assert(btpower_enable_ipa_vreg(&replacement)==-EMSGSIZE&&!replacement.ipa_config_done&&qmp_calls==calls);cases++;
 printf("{\"native_nvmem_and_AOP_frame_cases\":%d}\n",cases);
}
