#include "icnss2_mock.h"
#define ICNSS_ICC_VOTE_MAX 2
#define check_mul_overflow(a,b,c) __builtin_mul_overflow(a,b,c)
struct device_node{const char*name;int refs;};
struct device{struct device_node*of_node;};struct platform_device{struct device dev;};struct icc_path{int alive;};
struct icnss_bus_bw_cfg{u32 avg_bw,peak_bw;};
struct icnss_bus_bw_info{struct list_head list;const char*icc_name;struct icc_path*icc_path;struct icnss_bus_bw_cfg*cfg_table;};
struct icnss_priv{struct platform_device*pdev;u32 ddr_type;struct{struct list_head list_head;u32 path_count,bus_bw_cfg_count;int current_bw_vote;}icc;};
static struct device_node child_node={"ddr_cfg",1};static u32 path_count=2,bw_count=3;static int refs,paths,step,fail_step;static void*devres[40];static unsigned devres_count;static struct icc_path path_objects[5];
static bool failing(void){return ++step==fail_step;}
static void*devm_kzalloc(struct device*d,size_t n,int f){if(failing())return NULL;assert(devres_count<40);return devres[devres_count++]=kzalloc(n,f);}
static void*devm_kcalloc(struct device*d,size_t n,size_t z,int f){return devm_kzalloc(d,n*z,f);}
static void*kcalloc(size_t n,size_t z,int f){return kzalloc(n*z,f);}
static struct device_node*of_node_get(struct device_node*n){n->refs++;refs++;return n;}
static void of_node_put(struct device_node*n){if(n){assert(n->refs>1);n->refs--;refs--;}}
#define for_each_available_child_of_node(root,n) for((n)=of_node_get(&child_node);(n);of_node_put(n),(n)=NULL)
static int of_property_read_u32(struct device_node*n,const char*name,u32*v){if(failing())return -EINVAL;if(!strcmp(name,"qcom,icc-path-count"))*v=path_count;else if(!strcmp(name,"qcom,bus-bw-cfg-count"))*v=bw_count;else *v=8;return 0;}
static int of_property_read_u32_array(struct device_node*n,const char*name,u32*v,u32 count){assert(n==&child_node&&n->refs>1);if(failing())return -EINVAL;assert(count==path_count*bw_count*2);for(unsigned i=0;i<count;i++)v[i]=i+10;return 0;}
static int of_property_read_string_index(struct device_node*n,const char*name,int idx,const char**v){if(failing())return -EINVAL;*v=idx?"b":"a";return 0;}
static struct icc_path*of_icc_get(struct device*d,const char*name){if(failing())return ERR_PTR(-EPROBE_DEFER);struct icc_path*p=&path_objects[paths++];p->alive=1;return p;}
static void icc_put(struct icc_path*p){assert(!IS_ERR_OR_NULL(p)&&p->alive);p->alive=0;paths--;}
/* FUNCTIONS */
static void clean(void){while(devres_count)kfree(devres[--devres_count]);assert(!live&&!paths&&!refs);}
int main(void){struct device_node root={"root",1};struct platform_device dev={.dev={.of_node=&root}};struct icnss_priv p={.pdev=&dev,.ddr_type=8};unsigned cases=0;for(fail_step=0;fail_step<16;fail_step++){step=0;int ret=icnss_register_bus_scale(&p);if(!ret){if(p.icc.path_count){struct icnss_bus_bw_info*entry=list_first_entry(&p.icc.list_head,struct icnss_bus_bw_info,list);assert(entry->cfg_table[0].avg_bw==10&&entry->cfg_table[2].peak_bw==15);}icnss_unregister_bus_scale(&p);}assert(list_empty(&p.icc.list_head));clean();cases++;}fail_step=0;u32 bad[][2]={{0,3},{2,0},{UINT32_MAX,2},{0x80000000U,1},{0x40000000U,2}};for(unsigned i=0;i<5;i++){path_count=bad[i][0];bw_count=bad[i][1];assert(icnss_register_bus_scale(&p)==-EINVAL);assert(list_empty(&p.icc.list_head));clean();cases++;}printf("{\"icc_failure_and_overflow_cases\":%u,\"deferred_paths_released\":true,\"node_refs_balanced\":true,\"bandwidth_table_preserved\":true}\n",cases);}
