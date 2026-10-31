static int sequence,fail_at,live_psy,changed,battery_refs,provider_error,provider_value,config_value,config_error;
static void *allocations[16];static int allocations_count;
static struct power_supply battery;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;void *p=calloc(1,n);assert(p);allocations[allocations_count++]=p;return p;}
static void free_allocations(void){for(int n=0;n<allocations_count;n++)free(allocations[n]);allocations_count=0;}
static struct power_supply *power_supply_register(struct device *d,const struct power_supply_desc *desc,const struct power_supply_config *cfg){(void)d;if(fault())return ERR_PTR(-EAGAIN);struct power_supply *p=calloc(1,sizeof(*p));assert(p);p->data=cfg->drv_data;p->desc=desc;live_psy++;if(!strcmp(desc->name,"usb")){union power_supply_propval v={.intval=123};assert(!desc->get_property(p,POWER_SUPPLY_PROP_TYPE,&v)&&v.intval==POWER_SUPPLY_TYPE_UNKNOWN);}return p;}
static void power_supply_unregister(struct power_supply *p){assert(p&&live_psy>0);live_psy--;free(p);}
static struct power_supply *power_supply_get_by_name(const char *name){assert(!strcmp(name,"battery"));battery_refs++;return &battery;}
static void power_supply_put(struct power_supply *p){assert(p==&battery&&battery_refs>0);battery_refs--;}
static void power_supply_changed(struct power_supply *p){assert(p);changed++;}
static int mca_strategy_func_get_status(int type,int status,int *value){(void)type;(void)status;if(provider_error)return provider_error;*value=provider_value;return 0;}
static int platform_class_buckchg_ops_get_bus_volt(int role,int *out){(void)role;if(provider_error)return provider_error;*out=provider_value;return 0;}
static int platform_class_buckchg_ops_get_bus_curr(int role,int *out){return platform_class_buckchg_ops_get_bus_volt(role,out);}
static int strategy_class_fg_ops_get_voltage(int *out){return platform_class_buckchg_ops_get_bus_volt(0,out);}
static int strategy_class_fg_ops_get_current(int *out){return platform_class_buckchg_ops_get_bus_volt(0,out);}
static int strategy_class_fg_ops_get_temperature(int *out){return platform_class_buckchg_ops_get_bus_volt(0,out);}
static int mca_strategy_func_set_config(int type,int config,int value){(void)type;(void)config;config_value=value;return config_error;}
/* FUNCTIONS */
int main(void){
 struct device d={0};int faults=0;
 for(int wireless=0;wireless<2;wireless++)for(int n=1;n<=2;n++){sequence=0;fail_at=n;void *p=wireless?(void *)business_charger_wireless_psy_init(&d):(void *)business_charger_usb_psy_init(&d);assert(IS_ERR(p)&&!live_psy);free_allocations();faults++;}
 fail_at=0;sequence=0;struct usb_psy_info *usb=business_charger_usb_psy_init(&d);assert(!IS_ERR(usb));struct usb_psy_info *second=business_charger_usb_psy_init(&d);assert(!IS_ERR(second));struct wireless_psy_info *wls=business_charger_wireless_psy_init(&d);assert(!IS_ERR(wls));
 union power_supply_propval v;int cases=0;
 int supported_usb[]={POWER_SUPPLY_PROP_PRESENT,POWER_SUPPLY_PROP_ONLINE,POWER_SUPPLY_PROP_VOLTAGE_NOW,POWER_SUPPLY_PROP_CURRENT_NOW};
 for(unsigned n=0;n<ARRAY_SIZE(supported_usb);n++){provider_value=(supported_usb[n]==POWER_SUPPLY_PROP_PRESENT||supported_usb[n]==POWER_SUPPLY_PROP_ONLINE)?1:123456;provider_error=0;v.intval=99;assert(!usb_psy_get_prop(usb->usb_psy,supported_usb[n],&v)&&v.intval==provider_value);cases++;provider_error=-EAGAIN;v.intval=99;assert(usb_psy_get_prop(usb->usb_psy,supported_usb[n],&v)==-EAGAIN&&v.intval==99);cases++;}
 int unsupported_usb[]={POWER_SUPPLY_PROP_CURRENT_MAX,POWER_SUPPLY_PROP_VOLTAGE_MAX,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT};for(unsigned n=0;n<ARRAY_SIZE(unsupported_usb);n++){v.intval=99;assert(usb_psy_get_prop(usb->usb_psy,unsupported_usb[n],&v)==-ENODATA&&v.intval==99);cases++;}
 assert(usb_psy_get_prop(usb->usb_psy,(enum power_supply_property)999,&v)==-EINVAL);cases++;
 business_charger_update_usb_psy(XM_CHARGER_TYPE_PPS,usb);assert(usb->usb_psy_desc.type==POWER_SUPPLY_TYPE_USB_PD&&second->usb_psy_desc.type==POWER_SUPPLY_TYPE_UNKNOWN&&usb_psy_template.type==POWER_SUPPLY_TYPE_UNKNOWN&&battery_refs==0);business_charger_update_usb_psy(XM_CHARGER_TYPE_CDP,second);assert(second->usb_psy_desc.type==POWER_SUPPLY_TYPE_USB_CDP&&usb->usb_psy_desc.type==POWER_SUPPLY_TYPE_USB_PD);
 int limits[]={-1000,-1,0,500000,30000000};for(unsigned n=0;n<ARRAY_SIZE(limits);n++){v.intval=limits[n];config_error=0;assert(!usb_psy_set_prop(usb->usb_psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)&&config_value==limits[n]);config_error=-EIO;assert(usb_psy_set_prop(usb->usb_psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)==-EIO);cases+=2;}
 v.intval=-1001;assert(usb_psy_set_prop(usb->usb_psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)==-EINVAL);v.intval=30000001;assert(usb_psy_set_prop(usb->usb_psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)==-EINVAL);cases+=2;
 provider_error=0;provider_value=4211;assert(!wireless_psy_get_prop(wls->wireless_psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)&&v.intval==4211000);provider_value=-1234;assert(!wireless_psy_get_prop(wls->wireless_psy,POWER_SUPPLY_PROP_CURRENT_NOW,&v)&&v.intval==-1234000);provider_value=310;assert(!wireless_psy_get_prop(wls->wireless_psy,POWER_SUPPLY_PROP_TEMP,&v)&&v.intval==310);cases+=3;
 int supported_wls[]={POWER_SUPPLY_PROP_PRESENT,POWER_SUPPLY_PROP_VOLTAGE_NOW,POWER_SUPPLY_PROP_CURRENT_NOW,POWER_SUPPLY_PROP_TEMP};for(unsigned n=0;n<ARRAY_SIZE(supported_wls);n++){provider_error=-EIO;v.intval=99;assert(wireless_psy_get_prop(wls->wireless_psy,supported_wls[n],&v)==-EIO&&v.intval==99);cases++;}provider_error=0;provider_value=INT_MAX;assert(wireless_psy_get_prop(wls->wireless_psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)==-ERANGE);provider_value=INT_MIN;assert(wireless_psy_get_prop(wls->wireless_psy,POWER_SUPPLY_PROP_CURRENT_NOW,&v)==-ERANGE);cases+=2;
 int unsupported_wls[]={POWER_SUPPLY_PROP_VOLTAGE_MAX,POWER_SUPPLY_PROP_CURRENT_MAX,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX};for(unsigned n=0;n<ARRAY_SIZE(unsupported_wls);n++){assert(wireless_psy_get_prop(wls->wireless_psy,unsupported_wls[n],&v)==-ENODATA);cases++;}
 for(int n=-2;n<=3;n++){v.intval=n;assert(wireless_psy_set_prop(wls->wireless_psy,POWER_SUPPLY_PROP_ONLINE,&v)==((n==0||n==1)?0:-EINVAL));cases++;}v.intval=1;assert(!wireless_psy_set_prop(wls->wireless_psy,POWER_SUPPLY_PROP_ONLINE,&v));assert(!wireless_psy_get_prop(wls->wireless_psy,POWER_SUPPLY_PROP_ONLINE,&v)&&v.intval==1);
 business_charger_update_wireless_psy(wls);assert(!battery_refs);business_charger_usb_psy_deinit(usb);business_charger_usb_psy_deinit(usb);business_charger_usb_psy_deinit(second);business_charger_wireless_psy_deinit(wls);business_charger_wireless_psy_deinit(wls);assert(!live_psy&&!battery_refs);free_allocations();
 printf("{\"registration_failure_cases\":%d,\"property_and_control_cases\":%d,\"provider_errors_preserved\":true,\"wireless_unit_scaling_and_overflow\":true,\"per_device_usb_descriptors\":true,\"battery_references_balanced\":true,\"single_unregister_per_supply\":true}\n",faults,cases);
}
