#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef uint8_t u8;typedef uint32_t u32;
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0
#define NVT_TRANSFER_LEN 65536
#define NVT_LOG(...)
#define NVT_ERR(...)
#define kzalloc(n,f) calloc(1,n)
struct nvt_ts_bin_map {uint32_t BIN_addr,SRAM_addr,size,crc;char name[12];};
static struct nvt_ts_bin_map *bin_map;
static struct {int hw_crc;} panel,*ts=&panel;
static uint32_t partition;static uint8_t ilm_dlm_num=2,cascade_2nd_header_info,spi_dma_div_cnt_val;
static struct nvt_ts_bin_map *original_bin_map;
static uint32_t original_partition;
static uint8_t original_cascade_2nd_header_info,original_spi_dma_div_cnt_val;
/* FUNCTIONS */
/* ORIGINAL */
static void word(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(8*i);}
static void valid(uint8_t *p,size_t n){assert(n>=128);memset(p,0,n);word(p,0x30);word(p+4,0x1000);word(p+8,15);word(p+12,0x50);word(p+16,0x2000);word(p+20,15);for(int i=0x30;i<0x60;i++)p[i]=i;}
static uint32_t rng=1;static uint32_t next(void){rng=rng*1664525+1013904223;return rng;}
static int parse(uint8_t *p,size_t n){int ret=nvt_bin_header_parser(p,n);free(bin_map);bin_map=NULL;return ret;}
int main(int argc,char **argv){unsigned cases=0,real_files=0;uint8_t p[512];for(int hw=0;hw<2;hw++){ts->hw_crc=hw;valid(p,sizeof(p));assert(!parse(p,sizeof(p)));cases++;for(unsigned n=0;n<0x30;n++){assert(parse(p,n)==-EINVAL);cases++;}uint32_t ends[]={0,1,0x2f,0x31,513,UINT32_MAX};for(unsigned i=0;i<sizeof(ends)/sizeof(*ends);i++){valid(p,sizeof(p));word(p,ends[i]);assert(parse(p,sizeof(p))==-EINVAL);cases++;}
for(int field=0;field<2;field++){valid(p,sizeof(p));word(p+field*12,UINT32_MAX-3);word(p+field*12+8,16);assert(parse(p,sizeof(p))==-EINVAL);cases++;}
valid(p,sizeof(p));word(p+8,UINT32_MAX);assert(parse(p,sizeof(p))==-EINVAL);cases++;
for(int i=0;i<40000;i++){size_t n=next()%sizeof(p);for(size_t j=0;j<n;j++)p[j]=next()>>24;parse(p,n);cases++;}for(int i=0;i<40000;i++){valid(p,sizeof(p));unsigned which=next()%6;uint32_t value=next();if(which==0)word(p,value);else if(which==1)word(p+8,value);else if(which==2)word(p+12,value);else if(which==3)word(p+20,value);else if(which==4)p[0x28]=value;else p[0x20]=value;parse(p,sizeof(p));cases++;}
}
for(int a=1;a<argc;a++){FILE *fp=fopen(argv[a],"rb");assert(fp);assert(!fseek(fp,0,SEEK_END));long n=ftell(fp);assert(n>0);rewind(fp);uint8_t *data=malloc(n);assert(data&&fread(data,1,n,fp)==(size_t)n);fclose(fp);for(int hw=0;hw<2;hw++){ts->hw_crc=hw;assert(!nvt_bin_header_parser(data,n));assert(!original_nvt_bin_header_parser(data,n));assert(partition==original_partition);for(unsigned i=0;i<partition;i++){assert(bin_map[i].BIN_addr==original_bin_map[i].BIN_addr);assert(bin_map[i].SRAM_addr==original_bin_map[i].SRAM_addr);assert(bin_map[i].size==original_bin_map[i].size);assert(bin_map[i].crc==original_bin_map[i].crc);}free(bin_map);bin_map=NULL;free(original_bin_map);original_bin_map=NULL;}free(data);real_files++;}
printf("{\"original_rom_firmware_files_matched\":%u,\"firmware_parser_cases\":%u,\"short_headers_rejected\":true,\"partition_address_wrap_rejected\":true,\"inclusive_crc_bounds\":true}\n",real_files,cases);}
