#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef uint32_t u32;
typedef uint64_t u64;
#define GFP_KERNEL 0
#define d_mpr_e(...) ((void)0)
#define d_mpr_h(...) ((void)0)
#define MMRM_VDD_LEVEL_MAX 6
#define MMRM_VDD_LEVEL_LOW_SVS 0
#define MMRM_VDD_LEVEL_TURBO 5
#define MMRM_CRM_SW_DRV 0
static const int mmrm_sw_vdd_corner[6] = {64, 128, 192, 256, 320, 384};
struct device_node { int dummy; };
struct device { struct device_node *of_node; int kobj; void *data; };
struct platform_device { struct device dev; };
struct corner_info { const char *name; u32 volt_factor, scaling_factor_dyn, scaling_factor_leak; };
struct voltage_corner_set { int count; struct corner_info *corner_tbl; };
struct mmrm_clk_platform_resources { struct platform_device *pdev; struct voltage_corner_set corner_set; };
struct mmrm_driver_data { bool is_clk_scaling_supported; void *platform_data, *debugfs_root, *clk_mgr; };
struct mmrm_sw_clk_client_tbl_entry { void *clk; unsigned clk_src_id; u64 clk_rate; int max_rate_idx, crm_client_tbl_size, num_pwr_states; u64 *crm_client_tbl; };
struct mmrm_client_data { int drv_type; unsigned crm_drv_idx, pwr_st; };
static struct mmrm_driver_data *drv_data;
static bool mmrm_ready;
static int mmrm_probe_lock, mmrm_fs_attrs_group;
static int module_refs, steps, fail_step, live, resource_live, manager_live, debug_live;
static int client_cells = 30, corner_count = 6, rail_counts[3] = {6,6,6}, zero_voltage = -1, read_failure = -1, reads;
static bool client_property = true;
static long clock_rounded;
static int clock_voltage;
#define THIS_MODULE 0
#define smp_load_acquire(p) (*(p))
#define smp_store_release(p,v) (*(p)=(v))
static bool failure(void) { return ++steps == fail_step; }
static void mutex_lock(int *p) { assert(!*p); *p=1; }
static void mutex_unlock(int *p) { assert(*p); *p=0; }
static void *kzalloc(size_t size,int flags) { (void)flags; if(failure())return NULL;live++;return calloc(1,size); }
static void kfree(void *p) { if(p){live--;free(p);} }
static void platform_set_drvdata(struct platform_device *p,void *d) { p->dev.data=d; }
static void __module_get(int p) { (void)p; assert(!mmrm_ready);module_refs++; }
static void *mmrm_get_platform_data(struct device *p) { (void)p;return failure()?NULL:(void*)1; }
static int mmrm_read_platform_resources(struct platform_device *p,struct mmrm_driver_data *d) { (void)p;(void)d;resource_live=1;return failure()?-EIO:0; }
static int mmrm_init(struct mmrm_driver_data *d) { if(failure())return -EIO;manager_live=1;d->clk_mgr=(void*)1;return 0; }
static void *msm_mmrm_debugfs_init(void) { debug_live=1;return (void*)1; }
static int sysfs_create_group(int *k,int *g) { (void)k;(void)g;assert(!mmrm_ready);return failure()?-EIO:0; }
static void msm_mmrm_debugfs_deinit(void *p) { (void)p;debug_live=0; }
static void mmrm_deinit(struct mmrm_driver_data *d) { (void)d;manager_live=0; }
static void mmrm_free_platform_resources(struct mmrm_driver_data *d) { (void)d;resource_live=0; }
static int of_property_count_u32_elems(struct device_node *n,const char *p) { (void)n; if(!strcmp(p,"mmrm-client-info"))return client_property?client_cells:-EINVAL; if(!strcmp(p,"mm-rail-fact-volt"))return rail_counts[0];if(!strcmp(p,"scaling-fact-dyn"))return rail_counts[1];return rail_counts[2]; }
static void *of_find_property(struct device_node *n,const char *p,int *size) { (void)n;(void)p;if(size)*size=client_cells*4;return client_property?(void*)1:NULL; }
static int of_property_count_strings(struct device_node *n,const char *p) { (void)n;(void)p;return corner_count; }
static void *devm_kcalloc(struct device *d,int n,size_t size,int flags) { (void)d;(void)flags;if(failure())return NULL;return calloc(n,size); }
static int of_property_read_string_index(struct device_node *n,const char *p,int index,const char **value) { (void)n;(void)p;(void)index;if(++reads==read_failure)return -EIO;*value="corner";return 0; }
static int of_property_read_u32_index(struct device_node *n,const char *p,int index,u32 *value) { (void)n;if(++reads==read_failure)return -EIO;*value=(!strcmp(p,"mm-rail-fact-volt") && index==zero_voltage)?0:65536;return 0; }
static long clk_round_rate(void *clock,unsigned long rate) { (void)clock;(void)rate;return clock_rounded; }
static int qcom_clk_get_voltage(void *clock,unsigned long rate) { (void)clock;(void)rate;return clock_voltage; }
static int kstrtou32(const char *buf,int base,u32 *value) { char *end;errno=0;unsigned long v=strtoul(buf,&end,base);if(errno||v>UINT32_MAX)return -ERANGE;if(end==buf||(*end && !(*end=='\n' && !end[1])))return -EINVAL;*value=v;return 0; }
int mmrm_count_clk_clients_frm_dt(struct platform_device *pdev);
/* FUNCTIONS */
static void reset_success(struct platform_device *p) { struct mmrm_driver_data *d=p->dev.data;if(manager_live)mmrm_deinit(d);if(debug_live)msm_mmrm_debugfs_deinit(d->debugfs_root);if(resource_live)mmrm_free_platform_resources(d);kfree(d);p->dev.data=NULL;drv_data=NULL;mmrm_ready=false;module_refs=0; }
int main(void) {
 struct device_node node={0};struct platform_device p={.dev.of_node=&node};unsigned faults=0,bounds=0,rail=0,clock=0,crm=0;
 for(int f=1;f<=5;f++){steps=0;fail_step=f;assert(msm_mmrm_probe_init(&p)<0);assert(!live&&!resource_live&&!manager_live&&!debug_live&&!p.dev.data&&!mmrm_ready&&!module_refs&&!mmrm_probe_lock);faults++;}
 fail_step=0;steps=0;assert(!msm_mmrm_probe_init(&p));assert(mmrm_ready&&module_refs==1&&drv_data==p.dev.data);assert(msm_mmrm_probe_init(&p)==-EBUSY);reset_success(&p);
 for(int cells=-4;cells<128;cells++){client_cells=cells;int got=mmrm_count_clk_clients_frm_dt(&p);assert(got==((cells>0 && !(cells%5))?cells/5:-EINVAL));bounds++;}
 client_property=false;assert(!mmrm_count_clk_clients_frm_dt(&p));steps=0;assert(!msm_mmrm_probe_init(&p));assert(!((struct mmrm_driver_data*)p.dev.data)->is_clk_scaling_supported);reset_success(&p);client_property=true;client_cells=30;
 u32 parsed;assert(sysfs_get_param(NULL,&parsed)==-EINVAL);assert(sysfs_get_param("",&parsed)==-EINVAL);assert(!sysfs_get_param("0xff\n",&parsed)&&parsed==255);assert(!sysfs_get_param("42\n",&parsed)&&parsed==42);assert(sysfs_get_param("4294967296",&parsed)==-ERANGE);assert(sysfs_get_param("1junk",&parsed)==-EINVAL);
 for(int n=-3;n<13;n++)for(int a=-1;a<9;a++)for(int b=-1;b<9;b++)for(int c=-1;c<9;c++){corner_count=n;rail_counts[0]=a;rail_counts[1]=b;rail_counts[2]=c;steps=0;reads=0;struct mmrm_clk_platform_resources r={.pdev=&p};int ret=mmrm_load_mm_rail_corner_table(&r);assert((ret==0)==(n==6&&a==6&&b==6&&c==6));free(r.corner_set.corner_tbl);rail++;}
 corner_count=6;for(int i=0;i<3;i++)rail_counts[i]=6;
 for(int fail=1;fail<=24;fail++){reads=0;read_failure=fail;struct mmrm_clk_platform_resources r={.pdev=&p};assert(mmrm_load_mm_rail_corner_table(&r)==-EIO);free(r.corner_set.corner_tbl);rail++;}read_failure=-1;
 for(int i=0;i<6;i++){zero_voltage=i;struct mmrm_clk_platform_resources r={.pdev=&p};assert(mmrm_load_mm_rail_corner_table(&r)==-EINVAL);free(r.corner_set.corner_tbl);rail++;}zero_voltage=-1;
 struct mmrm_sw_clk_client_tbl_entry entry={0};u32 level=99;
 for(int v=-3;v<500;v++)for(int r=-3;r<3;r++){clock_voltage=v;clock_rounded=r;int ret=mmrm_sw_get_req_level(&entry,100,&level);if(r<0)assert(ret==r);else if(v<0)assert(ret==v);else if(v<64)assert(!ret&&level==0);else if(v<=384 && !(v%64))assert(!ret&&level==(u32)(v/64-1));else assert(ret==-EINVAL);clock++;}
 u64 table[7];entry.crm_client_tbl=table;entry.crm_client_tbl_size=7;entry.num_pwr_states=2;
 for(unsigned seed=0;seed<10000;seed++){for(int i=0;i<7;i++)table[i]=(seed*(i+31)+i*71)%10000;entry.clk_rate=0;for(int i=0;i<7;i++)if(table[i]>=entry.clk_rate){entry.clk_rate=table[i];entry.max_rate_idx=i;}for(int i=0;i<7;i++){struct mmrm_client_data d={.drv_type=i==6?0:1,.crm_drv_idx=i/2,.pwr_st=i%2};unsigned value=(seed*47+i)%12000;u64 expected=value;for(int j=0;j<7;j++)if(j!=i&&table[j]>expected)expected=table[j];int index=-1;assert(mmrm_sw_get_max_crm_rate(&entry,&d,value,&index)==expected);assert(index>=0&&index<7);crm++;}}
 printf("{\"mmrm_probe_fault_cases\":%u,\"mmrm_client_table_bounds_cases\":%u,\"mmrm_rail_corner_cases\":%u,\"mmrm_clock_error_cases\":%u,\"mmrm_crm_max_rate_cases\":%u}\n",faults,bounds,rail,clock,crm);
}
