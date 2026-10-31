struct attribute {const char *name;};struct device_attribute {struct attribute attr;};
struct mca_sysfs_attr_info {struct device_attribute attr;int sysfs_attr_name;};
#define mca_sysfs_attr_ro(prefix,mode,id,name) {{{#name}},id}
#define mca_sysfs_attr_rw(prefix,mode,id,name) mca_sysfs_attr_ro(prefix,mode,id,name)
/* TABLE */
#define ADAPTER_PROTOCOL_BC12 1
#define ADAPTER_PROTOCOL_PD 2
#define ADAPTER_PROTOCOL_PPS 3
#define CHARGER_PARTITION_HOST_KERNEL 1
#define CHARGER_PARTITION_INFO_2 2
#define CHARGE_DFX_LPD_DETECTED 9
#define CHARGE_DFX_NOT_STANDARD_ADAPTER 10
#define FSA_USBC_SBU_LPD_SENSE 1
#define FSA_USBC_SBU_LPD_ISOLATE 2
#define FSA_USBC_DISPLAYPORT_DISCONNECTED 3
typedef struct {int eu_mode,test,reserved;} charger_partition_info_2;
struct of_phandle_args {void *np;};
static int provider_error,provider_value,partition_error,partition_cleanup_error,partition_alloc_error,partition_deallocs,partition_calls,event_calls,last_event,last_value,strategy_calls,protection_error,bc12_error,pd_error,bc12_value,pd_value,battery_refs,sbu_refs,sbu_error;
static struct power_supply battery;
static int mca_strategy_func_process(int type,int event,int value){(void)type;strategy_calls++;last_event=event;last_value=value;return provider_error;}
static int mca_strategy_func_get_status(int type,int status,int *value){(void)type;(void)status;if(provider_error)return provider_error;*value=provider_value;return 0;}
static int protocol_class_get_adapter_type(int proto,int *value){int ret=proto==ADAPTER_PROTOCOL_BC12?bc12_error:pd_error;if(ret)return ret;*value=proto==ADAPTER_PROTOCOL_BC12?bc12_value:pd_value;return 0;}
static struct power_supply *power_supply_get_by_name(const char *name){assert(!strcmp(name,"battery"));battery_refs++;return &battery;}
static void power_supply_put(struct power_supply *p){assert(p==&battery&&battery_refs>0);battery_refs--;}
static int power_supply_get_property(struct power_supply *p,enum power_supply_property prop,union power_supply_propval *value){(void)p;(void)prop;if(provider_error)return provider_error;value->intval=provider_value;return 0;}
void business_charger_update_usb_psy(int type,struct usb_psy_info *info){(void)info;last_value=type;event_calls++;}
void business_usb_psy_event_process(struct usb_psy_info *info){(void)info;}
static void business_charger_update_wired_quick_charge_type(struct business_charger *c){(void)c;}
static void mca_charge_mievent_report(int event,void *data,int n){(void)event;(void)data;(void)n;}
static void mca_event_report_uevent(void *data){(void)data;}
struct mca_event_notify_data {const char *event;int event_len;};
static int connector_antiburn_is_triggered(void){return protection_error;}
static int lpd_is_charging_limit(void){return protection_error;}
static int of_parse_phandle_with_args(void *node,const char *name,void *args,int index,struct of_phandle_args *out){(void)node;(void)name;(void)args;(void)index;out->np=&battery;sbu_refs++;return 0;}
static void of_node_put(void *node){assert(node==&battery&&sbu_refs==1);sbu_refs--;}
static int fsa4480_switch_event(void *node,int event){assert(node==&battery);last_event=event;return sbu_error;}
static int platform_class_buckchg_ops_set_too_hot_limit(int role,int value){(void)role;(void)value;return provider_error;}
static int platform_class_buckchg_ops_get_bus_volt(int role,int *value){(void)role;if(provider_error)return provider_error;*value=5000000;return 0;}
static int platform_class_buckchg_ops_get_bus_curr(int role,int *value){(void)role;if(provider_error)return provider_error;*value=1000000;return 0;}
static int charger_partition_alloc(int host,int info,size_t n){(void)host;(void)info;(void)n;return partition_alloc_error;}
static int charger_partition_write(int host,int info,void *data,size_t n){(void)host;(void)info;(void)data;(void)n;return partition_error;}
static int charger_partition_dealloc(int host,int info,size_t n){(void)host;(void)info;(void)n;partition_deallocs++;return partition_cleanup_error;}
#define PARTITION_WRITER(name) static int charger_partition_write_##name(int value){last_value=value;partition_calls++;return partition_error;}
PARTITION_WRITER(soc_limit) PARTITION_WRITER(double85) PARTITION_WRITER(remove_temp_limit) PARTITION_WRITER(memory_test)
static void mca_event_block_notify(int type,int event,void *value){(void)type;event_calls++;last_event=event;last_value=*(int *)value;}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,struct mca_sysfs_attr_info *table,size_t count){for(size_t n=0;n<count;n++)if(!strcmp(name,table[n].attr.attr.name))return table+n;return NULL;}
/* FUNCTIONS */
static struct device_attribute attr(const char *name){return(struct device_attribute){{name}};}
int main(void){
 struct business_charger a={0},b={0};struct usb_psy_info ua={.charge_enable=1},ub={.charge_enable=1};struct device da={.data=&a},db={.data=&b};a.dev=&da;b.dev=&db;a.usb_psy_info=&ua;b.usb_psy_info=&ub;mutex_init(&a.policy_lock);mutex_init(&b.policy_lock);
 int bad=0;const char *invalid[]={"", "soc_limit", "soc_limit -1 0", "soc_limit 101 0", "soc_limit 70 80", "soc_limit 80 -1", "soc_limit 80 101", "soc_limit 80 20 extra", "double85bad 1", "double85 -1", "double85 2", "memory_test 999999999999999999999", "remove_temp_limit 1 extra", "unknown 1"};
 for(unsigned n=0;n<ARRAY_SIZE(invalid);n++){int before=partition_calls;assert(business_charger_process_debug_ctrl_input(&a,invalid[n])==-EINVAL&&partition_calls==before);bad++;}
 assert(!business_charger_process_debug_ctrl_input(&a,"soc_limit 80 20\n")&&last_value==((80<<8)|20));assert(!business_charger_process_debug_ctrl_input(&a,"double85 1"));partition_error=-EIO;assert(business_charger_process_debug_ctrl_input(&a,"memory_test 1")==-EIO);partition_error=0;provider_error=-ENOSPC;assert(!business_charger_process_debug_ctrl_input(&a,"remove_temp_limit 1"));provider_error=0;
 struct device_attribute handle=attr("handle_state"),eu=attr("is_eu_model"),rev=attr("revchg_bcl");
 assert(bussiness_charger_sysfs_store(&da,&handle,"-1",2)==-EINVAL);assert(bussiness_charger_sysfs_store(&da,&handle,"bad",3)==-EINVAL);assert(bussiness_charger_sysfs_store(&da,&rev,"30001",5)==-ERANGE);
 assert(bussiness_charger_sysfs_store(&da,&handle,"1",1)==-EOPNOTSUPP&&ua.charge_enable==1&&a.handle_state==0);a.handle_state=1;business_charger_process_handle_logic(&a);assert(ua.charge_enable==0&&a.last_handle_state==1);b.handle_state=1;business_charger_process_handle_logic(&b);assert(ub.charge_enable==0&&b.last_handle_state==1);a.handle_state=0;business_charger_process_handle_logic(&a);assert(ua.charge_enable==1&&ub.charge_enable==0);
 partition_error=-EAGAIN;assert(bussiness_charger_sysfs_store(&da,&eu,"1",1)==-EAGAIN&&a.is_eu_model==0);partition_error=0;provider_error=-EOPNOTSUPP;assert(bussiness_charger_sysfs_store(&da,&rev,"1",1)==-EOPNOTSUPP&&a.revchg_bcl==0);provider_error=0;
 partition_error=-EIO;partition_cleanup_error=-ENOMEM;int before=partition_deallocs;assert(business_charger_partition_set_status(1)==-EIO&&partition_deallocs==before+1);partition_error=0;assert(business_charger_partition_set_status(1)==-ENOMEM);partition_cleanup_error=0;
 int value=77;a.bc12_active=0;assert(!business_charger_get_power_max(&a,&value)&&value==0);value=77;assert(!business_charger_get_quick_charge_type(&a,&value)&&value==0);a.bc12_active=1;a.wired_power_max=55;a.wired_qucik_charge_type=3;assert(!business_charger_get_power_max(&a,&value)&&value==55);assert(!business_charger_get_quick_charge_type(&a,&value)&&value==3);
 bc12_error=-EIO;assert(update_usb_pd_type(&a)==-EIO);bc12_error=0;bc12_value=XM_CHARGER_TYPE_DCP;pd_error=-ENODEV;assert(!update_usb_pd_type(&a)&&a.real_type==XM_CHARGER_TYPE_DCP);pd_error=0;pd_value=XM_CHARGER_TYPE_PPS;assert(!update_usb_pd_type(&a)&&a.real_type==XM_CHARGER_TYPE_PPS);
 int liquid=0;protection_error=-ENODEV;business_charger_process_lpd_status_change(&a,MCA_EVENT_LPD_STATUS_CHANGE,&liquid);assert(last_value==1);business_charger_process_antiburn_change(&a,MCA_EVENT_CONN_ANTIBURN_CHANGE);assert(last_value==1);protection_error=0;liquid=1;business_charger_process_lpd_status_change(&a,MCA_EVENT_LPD_STATUS_CHANGE,&liquid);assert(last_value==0);
 business_charger_process_sbu_lpd_status_change(&a,1);assert(!sbu_refs&&last_event==FSA_USBC_SBU_LPD_SENSE);sbu_error=-EIO;business_charger_process_sbu_lpd_status_change(&a,2);assert(!sbu_refs&&last_event==FSA_USBC_SBU_LPD_ISOLATE);
 union power_supply_propval prop;provider_error=-EIO;assert(business_charger_battery_property(POWER_SUPPLY_PROP_HEALTH,&prop)==-EIO&&!battery_refs);provider_error=0;provider_value=15;char log[256];assert(business_charge_dump_log_context(&a,log,sizeof(log))>0);provider_value=-1;assert(business_charge_dump_log_context(&a,log,sizeof(log))==-ERANGE);provider_error=-EAGAIN;assert(business_charge_dump_log_context(&a,log,sizeof(log))==-EAGAIN);
 a.stopping=true;assert(bussiness_charger_sysfs_store(&da,&handle,"1",1)==-ENODEV);assert(business_charge_dump_log_context(&a,log,sizeof(log))==-ENODEV);
 printf("{\"invalid_debug_inputs\":%d,\"boolean_and_current_control_validation\":true,\"primary_provider_and_partition_errors\":true,\"per_device_dormant_handle_history\":true,\"unavailable_strategy_controls_rejected\":true,\"wired_only_getters_initialize_output\":true,\"negative_protection_getters_limit_charging\":true,\"sbu_node_references_balanced\":true,\"battery_refs_balanced\":true,\"packed_log_bounds\":true}\n",bad);
}
