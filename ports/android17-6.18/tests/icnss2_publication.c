#include "icnss2_mock.h"
#define WCN7750_DEVICE_ID 2
#define WCN6750_DEVICE_ID 1
#define WCN6450_DEVICE_ID 3
struct icnss_priv{int device_id;bool smmu_s1_enable,fault_handler_registered;void*iommu_domain;};
static int irq_failure,irq_published,regular_published;static void*token;static int icnss_smmu_fault_handler;
static int icnss_register_iommu_fault_handler_irq(struct icnss_priv*p){assert(!irq_published&&!regular_published);if(irq_failure)return -EINVAL;irq_published++;token=p;return 0;}
static void iommu_set_fault_handler(void*d,int handler,void*p){assert(!irq_published&&!regular_published);regular_published++;token=p;}
static void __module_get(struct module*m){assert(irq_published+regular_published==1);m->refs++;}
/* FUNCTIONS */
int main(void){unsigned cases=0;for(int id=0;id<5;id++)for(int s1=0;s1<2;s1++)for(int fail=0;fail<2;fail++){struct icnss_priv p={.device_id=id,.smmu_s1_enable=s1};irq_failure=fail;irq_published=regular_published=0;self_module.refs=0;token=NULL;int ret=icnss_publish_fault_handler(&p);bool publishes=s1&&id>=1&&id<=3&&!(id==2&&fail);assert(ret==(s1&&id==2&&fail?-EINVAL:0));assert(p.fault_handler_registered==publishes);assert(self_module.refs==publishes);assert(!!token==publishes);if(publishes)assert(token==&p);assert(irq_published==(publishes&&id==2));assert(regular_published==(publishes&&id!=2));cases++;}printf("{\"fault_publication_cases\":%u,\"all_native_variants_retain_module\":true,\"failed_irq_publication_keeps_no_reference\":true}\n",cases);}
