#include "cnss2_mock.h"
#define QCOM_SMEM_HOST_ANY 0
#define PERISEC_SMEM_ID 134
#define HW_WIFI_UID 0x502
#define CNSS_WLAN_HW_DISABLED 2
#define CNSS_ASSERT(v) assert(v)
struct cnss_plat_data { bool sec_peri_feature_disable;unsigned long driver_state; };
static size_t size;static int error;static uint32_t value;
static void *qcom_smem_get(unsigned host,unsigned id,size_t *len) { *len=size;return error?ERR_PTR(error):&value; }
/* FUNCTIONS */
int main(void)
{
 struct cnss_plat_data p={0};unsigned cases=0;
 for(size=0;size<4;size++){assert(cnss_wlan_hw_disable_check(&p)==-EMSGSIZE);cases++;}
 size=4;for(value=0;value<256;value++){assert(!cnss_wlan_hw_disable_check(&p));assert(test_bit(CNSS_WLAN_HW_DISABLED,&p.driver_state)==!!(value&4));cases++;}
 error=-ENOENT;assert(!cnss_wlan_hw_disable_check(&p) && p.sec_peri_feature_disable);error=-EIO;assert(!cnss_wlan_hw_disable_check(&p));
 printf("{\"secure_smem_length_and_flag_cases\":%u,\"unsupported_feature_cached\":true}\n",cases);
}
