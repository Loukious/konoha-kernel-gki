static struct device_node parent,children[3];
static struct platform_device device;
static void prop_u32(struct device_node *n,const char *name,u32 val) {struct prop *p=property(n,name);if(!p)p=&n->props[n->count++];*p=(struct prop){.name=name,.values={val},.count=1};}
static void prop_str(struct device_node *n,const char *name,const char *str) {struct prop *p=&n->props[n->count++];*p=(struct prop){.name=name,.string=str};}
static void init_dt(void) {
 memset(&parent,0,sizeof(parent));memset(children,0,sizeof(children));memset(&device,0,sizeof(device));memset(&device_map,0,sizeof(device_map));resource_step=published=released=0;io_calls=fail_io=persistent_io=0;gpio_rc=-ENOENT;irq_rc=10;irq_request_hook=request_irq;
 parent.full_name="flash";device.dev.of_node=&parent;prop_u32(&parent,"reg",0x100);parent.nchildren=3;
 for(int i=0;i<3;i++){parent.children[i]=&children[i];children[i].full_name="child";prop_str(&children[i],"qcom,led-name","test");prop_str(&children[i],"qcom,default-led-trigger","trigger");prop_str(&children[i],"label",i==2?"switch":i==1?"torch":"flash");if(i<2){prop_u32(&children[i],"qcom,id",0);prop_u32(&children[i],"qcom,ires-ua",5000);prop_u32(&children[i],"qcom,max-current-ma",300);}else prop_u32(&children[i],"qcom,led-mask",1);}
 prop_u32(&children[0],"xiaomi,max-duration-ms",300);prop_u32(&children[0],"qcom,duration-ms",300);
 struct prop *p=&children[1].props[children[1].count++];*p=(struct prop){.name="xiaomi,currents-with-time",.values={150,300},.count=2};p=&children[1].props[children[1].count++];*p=(struct prop){.name="xiaomi,time-for-currents",.values={UINT32_MAX,5000},.count=2};
 assert(!nresources&&!retained_nodes);INIT_LIST_HEAD(&qti_flash_controllers);
}
static void check_clean(void) {release_resources();assert(!nresources&&!retained_nodes&&published==released);for(int i=0;i<3;i++)assert(!children[i].refs);assert(qti_flash_controllers.next==&qti_flash_controllers);}
int main(void) {
 init_dt();fail_resource=0;assert(!qti_flash_led_probe(&device));int steps=resource_step;struct qti_flash_led *led=platform_get_drvdata(&device);assert(led->ready&&published==3);assert(led->fnode[1].custom_max_cur.time_for_currents[0]==5000&&led->fnode[1].custom_max_cur.time_for_currents[1]==UINT32_MAX);assert(led->fnode[1].custom_max_cur.max_currents[0]==300&&led->fnode[1].custom_max_cur.always_on_max_current==150);
 struct led_trigger trig={0};led->snode[0].cdev.trigger=&trig;assert(!qti_flash_led_set_param(&trig,(struct flash_led_param){5,10}));assert(!qti_flash_led_brightness_set(&led->fnode[0].fdev.led_cdev,100));assert(!qti_flash_led_switch_brightness_set(&led->snode[0].cdev,1));fire_timer(&led->snode[0].on_timer);assert(led->snode[0].on_work.pending);has_battery=true;int output;assert(qti_flash_led_prepare(&trig,QUERY_MAX_AVAIL_CURRENT,&output)==-EINVAL&&battery.refs==1);
 qti_flash_led_remove(&device);assert(led->stopping&&!led->ready&&!led->snode[0].on_work.pending&&!led->snode[0].off_timer.active&&!battery.refs);assert(qti_flash_led_set_param(&trig,(struct flash_led_param){5,10})==-ENODEV);assert(qti_flash_led_prepare(&trig,0,NULL)==-ENODEV);has_battery=false;check_clean();
 for(int f=1;f<=steps;f++){init_dt();fail_resource=f;assert(qti_flash_led_probe(&device)<0);check_clean();}fail_resource=0;
 for(int f=1;f<=6;f++){init_dt();fail_io=f;int rc=qti_flash_led_probe(&device);if(!rc)qti_flash_led_remove(&device);assert(rc==-EIO);check_clean();}
 int rejected=0;
 for(int type=0;type<13;type++){init_dt();switch(type){case 0:prop_u32(&children[0],"qcom,id",256);break;case 1:prop_u32(&children[0],"qcom,id",4);break;case 2:prop_u32(&children[0],"qcom,strobe-sel",256);break;case 3:prop_u32(&children[0],"qcom,strobe-sel",1);prop_u32(&children[0],"qcom,strobe-config",8);break;case 4:property(&children[1],"xiaomi,time-for-currents")->count=1;break;case 5:property(&children[1],"xiaomi,currents-with-time")->count=11;break;case 6:prop_u32(&children[0],"xiaomi,max-duration-ms",1);break;case 7:prop_u32(&children[0],"qcom,duration-ms",400);break;case 8:gpio_rc=-EPROBE_DEFER;break;case 9:irq_rc=-EPROBE_DEFER;break;case 10:prop_u32(&children[0],"qcom,max-current-ma",0);break;case 11:prop_u32(&parent,"reg",UINT32_MAX);break;case 12:property(&children[1],"xiaomi,time-for-currents")->count=3;break;}
 assert(qti_flash_led_probe(&device)<0);check_clean();rejected++;}
 /* Stop attempts all shutdown writes even if a previous write fails. */
 for(int f=1;f<=3;f++){init_dt();assert(!qti_flash_led_probe(&device));int before=io_calls;fail_io=before+f;qti_flash_led_remove(&device);assert(io_calls==before+3);check_clean();}
 printf("{\"probe_resource_failures\":%d,\"setup_io_failures\":6,\"invalid_dt_cases\":%d,\"all_shutdown_writes\":true,\"unpublished_callbacks\":true,\"node_and_battery_refs\":true,\"u32_time_sort\":true}\n",steps,rejected);
}
