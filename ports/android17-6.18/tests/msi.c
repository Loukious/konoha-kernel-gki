#include "mock.h"
#define DMA_FROM_DEVICE 2
#define IRQF_SHARED 1
#define MSI_IRQ_PER_GRP 32
#define handle_simple_irq NULL
struct device{int x;};struct device_node{int x;};struct irq_domain{struct irq_domain *parent;void *host_data;};struct msm_msi;struct msm_msi_client;struct msm_msi_grp;struct irq_data{void *data;};struct msi_desc{struct device *dev;};typedef struct{struct msi_desc *desc;}msi_alloc_info_t;
enum msi_type{MSM_MSI_TYPE_QCOM,MSM_MSI_TYPE_SNPS};struct msm_msi_irq{struct msm_msi_client *client;struct msm_msi_grp *grp;u32 grp_index;unsigned hwirq,virq;u32 pos;};struct msm_msi_grp{struct msm_msi_irq irqs[32];};struct msm_msi{struct list_head clients;struct device *dev;struct device_node *of_node;int nr_virqs,nr_hwirqs,nr_grps,mutex;struct msm_msi_grp *grps;unsigned long *bitmap;u64 msi_addr;u32 msi_addr_size;enum msi_type type;};struct msm_msi_client{struct list_head node;struct msm_msi *msi;struct device *dev;u32 nr_irqs;dma_addr_t msi_addr;};
static unsigned dma_maps,dma_unmaps,mapped,disposed;static bool dma_fail;static int map_fault;static struct irq_data data[256];static int msm_msi_bottom_irq_chip;
static dma_addr_t dma_map_resource(struct device *d,u64 a,u32 size,int direction,int flags){if(dma_fail)return 0;dma_maps++;return a+0x1000;}
static bool dma_mapping_error(struct device *d,dma_addr_t a){return !a;}
static void dma_unmap_resource(struct device *d,u64 a,u32 size,int direction,int flags){assert(size==8192);dma_unmaps++;}
static int bitmap_find_next_zero_area(unsigned long *map,int limit,int start,unsigned count,unsigned align){for(unsigned i=0;i<(unsigned)limit;i++){if(i&align)continue;bool found=i+count<=(unsigned)limit;for(unsigned n=0;found&&n<count;n++)found=!map[i+n];if(found)return i;}
return limit;}
static void bitmap_set(unsigned long *b,int pos,unsigned n){for(unsigned i=0;i<n;i++){assert(!b[pos+i]);b[pos+i]=1;}}
static void bitmap_clear(unsigned long *b,int pos,unsigned n){for(unsigned i=0;i<n;i++){assert(b[pos+i]);b[pos+i]=0;}}
static void irq_domain_set_info(struct irq_domain *d,unsigned v,unsigned h,void *chip,void *info,void *handler,void *x,void *y){assert(v<256);data[v].data=info;}
static struct irq_data *irq_domain_get_irq_data(struct irq_domain *d,unsigned v){return data[v].data?&data[v]:NULL;}
static void *irq_data_get_irq_chip_data(struct irq_data *d){return d->data;}
static void irq_domain_free_irqs_parent(struct irq_domain *d,unsigned v,unsigned n){for(unsigned i=0;i<n;i++)data[v+i].data=NULL;}
static unsigned irq_of_parse_and_map(struct device_node *n,int i){if(i==map_fault)return 0;mapped++;return i+1;}
static void msm_msi_qgic_handler(void){}
static void irq_set_chained_handler_and_data(unsigned i,void (*h)(void),void *d){assert(i);}
static void irq_dispose_mapping(unsigned i){assert(i);disposed++;}
/* FUNCTIONS */
int main(void){unsigned cases=0;struct device dev={0};for(int size=1;size<=65;size++){struct msm_msi m={.dev=&dev,.nr_virqs=size,.nr_hwirqs=size,.nr_grps=(size+31)/32,.msi_addr=0x10000,.msi_addr_size=8192};struct msm_msi_grp groups[3]={0};unsigned long map[65]={0};m.grps=groups;m.bitmap=map;INIT_LIST_HEAD(&m.clients);struct irq_domain parent={.host_data=&m},domain={.parent=&parent,.host_data=&m};struct msi_desc desc={&dev};msi_alloc_info_t arg={.desc=&desc};map_fault=-1;mapped=disposed=0;assert(!msm_msi_qgic_irq_setup(&m)&&mapped==(unsigned)size);for(int n=0;n<size;n++)assert(groups[n/32].irqs[n%32].hwirq==(unsigned)n+1&&groups[n/32].irqs[n%32].pos==(unsigned)n);
for(unsigned n=1;n<=(unsigned)size+2;n++){assert(!msm_msi_domain_prepare(&domain,&dev,1,&arg));arg.desc=&desc;int ret=msm_msi_irq_domain_alloc(&parent,70,n,&arg);if(ret)assert(ret==-ENOSPC);else{assert(allocations==1);msm_msi_irq_domain_free(&parent,70,n);}
assert(!allocations&&m.clients.next==&m.clients&&dma_maps==dma_unmaps);cases++;}
dma_fail=true;assert(msm_msi_domain_prepare(&domain,&dev,1,&arg)==-ENOMEM);dma_fail=false;assert(!allocations);for(map_fault=0;map_fault<size;map_fault++){mapped=disposed=0;assert(msm_msi_qgic_irq_setup(&m)==-ENODEV);assert(mapped==disposed&&mapped==(unsigned)map_fault);cases++;}}
printf("{\"msi_range_and_qgic_fault_cases\":%u,\"multiple_groups_and_dma_size_balance\":true}\n",cases);}
