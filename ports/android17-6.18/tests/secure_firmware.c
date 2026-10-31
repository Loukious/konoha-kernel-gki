#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#define elf32_hdr Elf32_Ehdr
#define elf64_hdr Elf64_Ehdr
#define PAGE_SIZE 4096
#define MAX_FW_APP_SIZE 256
#define FILE_EXT_SIZE 5
#define GFP_KERNEL 0
#define IS_ERR_OR_NULL(x) (!(x))
#define check_add_overflow(a,b,p) __builtin_add_overflow((a),(b),(p))
#define max(a,b) ((a)>(b)?(a):(b))
struct firmware {const unsigned char *data;size_t size;};
struct qtee_shm {char *vaddr;};
static void *smci_dev=(void*)1;
static unsigned char bytes[1024],one[16],two[32];
static struct firmware parts[3];
static unsigned request_step,alloc_step,fail_request,fail_alloc,live,held;
static void *kcalloc(size_t n,size_t sz,int flag) {(void)flag;if(++alloc_step==fail_alloc)return NULL;void *p=calloc(n,sz);assert(p);live++;return p;}
static void kfree(void *p) {if(p){assert(live);live--;free(p);}}
static int firmware_request_nowarn(const struct firmware **p,const char *name,void *dev) {assert(dev);if(++request_step==fail_request)return -EIO;unsigned i=atoi(strrchr(name,'b')+1);if(i>=3)return -ENOENT;*p=&parts[i];held++;return 0;}
static void release_firmware(const struct firmware *p) {if(p){assert(held);held--;}}
static int qtee_shmbridge_allocate_shm(size_t size,struct qtee_shm *shm) {if(++alloc_step==fail_alloc)return -ENOMEM;assert(size<8192);shm->vaddr=malloc(size);assert(shm->vaddr);live++;return 0;}
/* FUNCTIONS */
static void setup(bool is64) {
 memset(bytes,0,sizeof(bytes));memcpy(bytes,ELFMAG,SELFMAG);bytes[EI_CLASS]=is64?ELFCLASS64:ELFCLASS32;bytes[EI_DATA]=ELFDATA2LSB;bytes[EI_VERSION]=EV_CURRENT;
 if(is64){Elf64_Ehdr *h=(void*)bytes;h->e_phnum=3;h->e_phoff=sizeof(*h);h->e_phentsize=sizeof(Elf64_Phdr);Elf64_Phdr *p=(void*)(bytes+h->e_phoff);p[1].p_offset=512;p[2].p_offset=768;}
 else {Elf32_Ehdr *h=(void*)bytes;h->e_phnum=3;h->e_phoff=sizeof(*h);h->e_phentsize=sizeof(Elf32_Phdr);Elf32_Phdr *p=(void*)(bytes+h->e_phoff);p[1].p_offset=512;p[2].p_offset=768;}
 parts[0]=(struct firmware){bytes,256};parts[1]=(struct firmware){one,sizeof(one)};parts[2]=(struct firmware){two,sizeof(two)};
 request_step=alloc_step=fail_request=fail_alloc=0;assert(!held&&!live);
}
int main(void) {
 unsigned cases=0;memset(one,0xa3,sizeof(one));memset(two,0xd7,sizeof(two));
 for(unsigned v=0;v<2;v++) {
  setup(v);size_t size=0;struct qtee_shm shm={0};char *img=firmware_request_from_smcinvoke("test",&size,&shm);assert(img&&size==800&&held==0&&live==1);assert(!memcmp(img,bytes,256)&&!memcmp(img+512,one,16)&&!memcmp(img+768,two,32));for(unsigned i=256;i<512;i++)assert(!img[i]);kfree(img);cases++;
  for(unsigned f=1;f<=3;f++){setup(v);fail_request=f;assert(!firmware_request_from_smcinvoke("test",&size,&shm)&&!held&&!live);cases++;}
  for(unsigned f=1;f<=3;f++){setup(v);fail_alloc=f;assert(!firmware_request_from_smcinvoke("test",&size,&shm)&&!held&&!live);cases++;}
  for(unsigned len=0;len<256;len++) {setup(v);parts[0].size=len;size_t *offset=NULL;unsigned count;int ret=split_firmware_layout(&parts[0],&offset,&count);size_t minimum=v?sizeof(Elf64_Ehdr)+3*sizeof(Elf64_Phdr):sizeof(Elf32_Ehdr)+3*sizeof(Elf32_Phdr);assert((ret==0)==(len>=minimum));kfree(offset);cases++;}
  for(unsigned pos=0;pos<EI_NIDENT;pos++){setup(v);if(pos>EI_VERSION)continue;bytes[pos]=0xff;size_t *offset=NULL;unsigned count;assert(split_firmware_layout(&parts[0],&offset,&count)<0);assert(!offset);cases++;}
 }
 setup(true);Elf64_Ehdr *h=(void*)bytes;h->e_phoff=SIZE_MAX;size_t size;struct qtee_shm shm;assert(!firmware_request_from_smcinvoke("test",&size,&shm)&&!live&&!held);cases++;
 setup(true);Elf64_Phdr *ph=(void*)(bytes+sizeof(Elf64_Ehdr));ph[1].p_offset=SIZE_MAX-4;assert(!firmware_request_from_smcinvoke("test",&size,&shm)&&!live&&!held);cases++;
 setup(true);parts[1].size=SIZE_MAX;assert(!firmware_request_from_smcinvoke("test",&size,&shm)&&!live&&!held);cases++;
 printf("{\"elf_and_allocation_cases\":%u,\"split_images_and_zeroed_gaps\":true,\"all_faults_release_firmware\":true}\n",cases);
}
