#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GFP_KERNEL 0
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define ERR_CAST(p) ((void *)(p))
#define MCA_CHARGE_LOG_ID_BATTERY_INFO 4
#define mutex_init pthread_mutex_init_mock
struct device { int value; };
struct power_supply { int value; };
struct thermal_zone_device { int value; };
struct thermal_zone_params { int value; };
struct power_supply_config { void *fwnode, *drv_data; };
struct batt_psy_info { struct device *dev;struct power_supply *batt_psy;struct thermal_zone_device *tzd;pthread_mutex_t model_lock;bool model_valid;char model_name[64]; };
static int batt_psy_desc,psy_tzd_ops,battery_log_ops;
static struct power_supply supply;
static struct thermal_zone_device thermal;
static struct batt_psy_info *allocated;
static int failure,supplies,zones,logs,sequence;
static void *dev_fwnode(struct device *dev) {return dev;}
static void *devm_kzalloc(struct device *dev,size_t size,int flags) {(void)dev;(void)flags;if(failure==1)return NULL;allocated=calloc(1,size);return allocated;}
static void pthread_mutex_init_mock(pthread_mutex_t *m) {assert(!pthread_mutex_init(m,NULL));}
static struct power_supply *power_supply_register(struct device *dev,void *desc,struct power_supply_config *cfg) {(void)dev;(void)desc;assert(cfg->drv_data==allocated);if(failure==2)return ERR_PTR(-EIO);supplies++;return &supply;}
static struct thermal_zone_device *thermal_tripless_zone_device_register(const char *name,void *data,void *ops,void *params) {(void)name;(void)data;(void)ops;(void)params;if(failure==3)return ERR_PTR(-ENOSPC);zones++;return &thermal;}
static int thermal_zone_device_enable(struct thermal_zone_device *tzd) {assert(tzd==&thermal);return failure==4?-EINVAL:0;}
static void mca_log_charge_log_register(int id,void *ops,void *data) {(void)id;(void)ops;assert(data==allocated);logs++;}
static void mca_log_charge_log_unregister(int id,void *data) {(void)id;assert(data==allocated&&logs==1);logs--;assert(sequence++==0);}
static void thermal_zone_device_unregister(struct thermal_zone_device *tzd) {assert(tzd==&thermal&&zones==1);zones--;if(!failure)assert(sequence++==1);}
static void power_supply_unregister(struct power_supply *psy) {assert(psy==&supply&&supplies==1);supplies--;if(!failure)assert(sequence++==2);}
/* STAGED_DEFINITIONS */
int main(void) {
 struct device dev;
 for(failure=1;failure<=4;failure++) {struct batt_psy_info *p=business_battery_psy_init(&dev);assert(IS_ERR(p));assert(!supplies&&!zones&&!logs);free(allocated);allocated=NULL;}
 failure=0;struct batt_psy_info *p=business_battery_psy_init(&dev);assert(p==allocated&&supplies==1&&zones==1&&logs==1);business_battery_psy_deinit(p);assert(!supplies&&!zones&&!logs&&sequence==3);free(allocated);
 puts("{\"passed\":true,\"four_registration_failures_unwind\":true,\"remove_drains_log_before_zone_and_supply\":true}");
}
