#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#define __packed __attribute__((packed))
#define AUDIO_SCENE_NUM 6
#define SIPA_CHANNEL_NUM 4
#define U32_MAX UINT32_MAX
typedef uint32_t u32;
struct firmware {size_t size; const unsigned char *data;};
/* PRODUCTION_HEADER */
/* PRODUCTION_FUNCTIONS */
static unsigned cases;
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"failed line %d\n",__LINE__);abort();} cases++; } while (0)
static unsigned char bytes[4096];
static SIPA_PARAM_FW *param=(void *)bytes;
static SIPA_CHIP_CFG *chip;
static struct firmware fw;
static size_t size;
static void seal(void) {param->crc=crc32((void *)&param->version,sizeof(*param)+param->data_size-4);}
static void reset(void) {
 memset(bytes,0,sizeof(bytes));param->version=SIPA_FW_VER;
 param->ch_en[0]=1;param->data_size=sizeof(SIPA_CHIP_CFG);
 param->chip_cfg[0]=(SIPA_PARAM_LIST){0,1,sizeof(SIPA_CHIP_CFG)};
 chip=(void *)param->data;chip->reg_addr_width=8;chip->reg_val_width=16;
 fw=(struct firmware){sizeof(*param)+param->data_size,bytes};seal();
}
int main(void) {
 reset();CHECK(sipa_param_validate(&fw,&size)==0);CHECK(size==fw.size);
 CHECK(sipa_param_validate(NULL,&size)==-EINVAL);
 fw.size=sizeof(*param)-1;CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();fw.size=(size_t)UINT32_MAX+1;CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->data_size++;CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->version++;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->crc++;CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->ch_en[0]=2;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->chip_cfg[0].offset=UINT32_MAX;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->chip_cfg[0].num=UINT32_MAX;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->chip_cfg[0].node_size--;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();param->chip_cfg[0].num=0;seal();CHECK(sipa_param_validate(&fw,&size)==0);
 reset();chip->reg_val_width=33;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();chip->reg_addr_width=33;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();chip->pvdd_limit.bit_offset=32;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 reset();chip->trim_regs.crc_width=17;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 size_t offsets[]={offsetof(SIPA_CHIP_CFG,chip_id_ranges),offsetof(SIPA_CHIP_CFG,init),offsetof(SIPA_CHIP_CFG,startup),offsetof(SIPA_CHIP_CFG,shutdown),offsetof(SIPA_CHIP_CFG,trim_regs.efuse),offsetof(SIPA_CHIP_CFG,trim_regs.crc),offsetof(SIPA_CHIP_CFG,trim_regs.default_set),offsetof(SIPA_CHIP_CFG,sram_ops.sram)};
 size_t widths[]={sizeof(SIPA_VAL_RANGE),sizeof(SIPA_REG_COMMON),sizeof(SIPA_REG_PROC),sizeof(SIPA_REG_PROC),sizeof(SIPA_REG_PROC),sizeof(SIPA_REG_PROC),sizeof(SIPA_REG_PROC),sizeof(SIPA_REG_COMMON)};
 for (unsigned i=0;i<8;i++) {
  reset();SIPA_PARAM_LIST *list=(void *)((char *)chip+offsets[i]);
  *list=(SIPA_PARAM_LIST){0,1,widths[i]};seal();CHECK(sipa_param_validate(&fw,&size)==0);
  list->node_size++;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
  list->node_size=widths[i];list->offset=param->data_size;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
  list->num=0;seal();CHECK(sipa_param_validate(&fw,&size)==0);
  list->offset++;seal();CHECK(sipa_param_validate(&fw,&size)==-EINVAL);
 }
 reset();param->ch_en[3]=1;param->chip_cfg[3]=param->chip_cfg[0];seal();CHECK(sipa_param_validate(&fw,&size)==0);
 printf("PASS %u firmware cases\n",cases);return 0;
}
