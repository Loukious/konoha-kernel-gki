#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define check_mul_overflow(a,b,p) __builtin_mul_overflow(a,b,p)
enum power_supply_property { POWER_SUPPLY_PROP_ONLINE };
enum { POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,POWER_SUPPLY_CAPACITY_LEVEL_LOW,POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,POWER_SUPPLY_CAPACITY_LEVEL_HIGH,POWER_SUPPLY_CAPACITY_LEVEL_FULL };
enum { POWER_SUPPLY_STATUS_DISCHARGING,POWER_SUPPLY_STATUS_CHARGING,POWER_SUPPLY_STATUS_FULL };
union power_supply_propval { int intval; const char *strval; };
struct power_supply { int online,error; };
struct thermal_zone_device { int unused; };
static struct power_supply usb,wireless;
static bool have_usb=true,have_wireless,done;
static int gets,puts_count,vote=-EINVAL,soc=42,temp=250,temp_error;
static struct power_supply *power_supply_get_by_name(const char *name) { struct power_supply *p=!strcmp(name,"usb")?(have_usb?&usb:NULL):(have_wireless?&wireless:NULL);if(p)gets++;return p; }
static int power_supply_get_property(struct power_supply *p,enum power_supply_property property,union power_supply_propval *value) {(void)property;if(p->error)return p->error;value->intval=p->online;return 0;}
static void power_supply_put(struct power_supply *p) { assert(p);puts_count++; }
static int mca_get_named_client_vote(const char *name,const char *client) {assert(!strcmp(name,"input_suspend")&&!strcmp(client,"micharge"));return vote;}
static int strategy_class_fg_ops_get_soc(void) {return soc;}
static bool strategy_class_fg_ops_get_charging_done(void) {return done;}
static int strategy_class_fg_ops_get_thermal_temperature(int *value) {if(temp_error)return temp_error;*value=temp;return 0;}
/* STAGED_DEFINITIONS */
int main(void) {
 union power_supply_propval value={.intval=123};
 assert(battery_capacity_level(-EIO,&value)==-EIO&&value.intval==123);
 assert(battery_capacity_level(101,&value)==-ERANGE);
 for(int n=0;n<=100;n++) {assert(!battery_capacity_level(n,&value));int expected=!n?POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL:n<=20?POWER_SUPPLY_CAPACITY_LEVEL_LOW:n<=80?POWER_SUPPLY_CAPACITY_LEVEL_NORMAL:n<100?POWER_SUPPLY_CAPACITY_LEVEL_HIGH:POWER_SUPPLY_CAPACITY_LEVEL_FULL;assert(value.intval==expected);}
 value.intval=567;assert(battery_scale_value(-EIO,100,1000,&value)==-EIO&&value.intval==567);
 assert(battery_scale_value(0,INT_MAX,1000,&value)==-ERANGE);
 assert(!battery_scale_value(0,3700,1000,&value)&&value.intval==3700000);
 assert(!battery_status(&value)&&value.intval==POWER_SUPPLY_STATUS_DISCHARGING);
 usb.online=1;assert(!battery_status(&value)&&value.intval==POWER_SUPPLY_STATUS_CHARGING);
 soc=100;done=true;assert(!battery_status(&value)&&value.intval==POWER_SUPPLY_STATUS_FULL);
 vote=-EINVAL;assert(!battery_status(&value)&&value.intval==POWER_SUPPLY_STATUS_FULL);
 vote=1;assert(!battery_status(&value)&&value.intval==POWER_SUPPLY_STATUS_DISCHARGING);vote=0;
 usb.error=-EIO;assert(battery_status(&value)==-EIO);usb.error=0;
 usb.online=0;have_wireless=true;wireless.online=1;assert(!battery_status(&value)&&value.intval==POWER_SUPPLY_STATUS_FULL);
 wireless.error=-ETIMEDOUT;assert(battery_status(&value)==-ETIMEDOUT);wireless.error=0;
 soc=101;assert(battery_status(&value)==-ERANGE);soc=-ENODATA;assert(battery_status(&value)==-ENODATA);
 have_usb=false;assert(battery_status(&value)==-ENODEV);
 assert(gets==puts_count);
 int out=123;temp_error=-EIO;assert(power_supply_read_temp(NULL,&out)==-EIO&&out==123);temp_error=0;temp=250;assert(!power_supply_read_temp(NULL,&out)&&out==25000);temp=INT_MAX;assert(power_supply_read_temp(NULL,&out)==-ERANGE);
 puts("{\"passed\":true,\"soc_boundaries\":101,\"wired_without_wireless\":true,\"provider_errors\":true,\"unit_overflow\":true,\"provider_refs_balanced\":true,\"negative_vote_not_enabled\":true}");
}
