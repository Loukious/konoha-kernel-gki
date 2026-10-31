static struct regmap map;
static struct qti_flash_led led;
static struct flash_node_data fnodes[8];
static struct flash_switch_data snodes[3];
static int gpios[4]={-1,-1,-1,-1};
static struct platform_device pdev;
static struct led_trigger trigger;
static void init(void) {
 memset(&map,0,sizeof(map));memset(&led,0,sizeof(led));memset(fnodes,0,sizeof(fnodes));memset(snodes,0,sizeof(snodes));
 io_calls=fail_io=persistent_io=timer_cancels=work_cancels=work_runs=0;
 led=(struct qti_flash_led){.pdev=&pdev,.regmap=&map,.fnode=fnodes,.snode=snodes,.num_fnodes=8,.num_snodes=3,.max_channels=4,.hw_strobe_gpio=gpios,.ready=true,.base=0x100,.subtype=FLASH_LED_4_CHAN};
 mutex_init(&led.lock);mutex_init(&led.command_lock);INIT_LIST_HEAD(&led.entry);
 for(int i=0;i<8;i++) {struct flash_node_data *f=&fnodes[i];f->led=&led;f->id=i%4;f->type=i<4?FLASH_LED_TYPE_FLASH:FLASH_LED_TYPE_TORCH;f->default_ires_ua=f->ires_ua=5000;f->max_current=300;f->max_duration=300;f->duration=29;f->fdev.led_cdev.name="test";}
 for(int i=0;i<3;i++) {struct flash_switch_data *sw=&snodes[i];sw->led=&led;sw->led_mask=i==0?9:i==1?6:15;sw->cdev.name="switch";hrtimer_setup(&sw->on_timer,on_timer_function,0,0);hrtimer_setup(&sw->off_timer,off_timer_function,0,0);INIT_WORK(&sw->on_work,qti_flash_on_work);INIT_WORK(&sw->off_work,qti_flash_off_work);}
 snodes[0].cdev.trigger=&trigger;list_add_tail(&led.entry,&qti_flash_controllers);
}
static void fini(void) {list_del_init(&led.entry);assert(!pthread_mutex_destroy(&led.lock.raw));assert(!pthread_mutex_destroy(&led.command_lock.raw));}
static void configure(int i,int ma) {assert(!qti_flash_led_brightness_set(&fnodes[i].fdev.led_cdev,ma));}
static void pending_brightness(struct work_struct *work) {configure(0,100);}
int main(void) {
 int checks=0;
 for(unsigned n=0;n<2000;n++) {int code=timeout_to_code(n);if(n<10||n>1280)assert(code==-EINVAL);else assert(code>=0&&code<=127&&(code+1)*10<=n);checks++;}
 assert(qti_flash_time_valid(KTIME_MAX/NSEC_PER_MSEC));assert(!qti_flash_time_valid(KTIME_MAX/NSEC_PER_MSEC+1ULL));assert(!qti_flash_time_valid(UINT64_MAX));
 init();assert(qti_flash_led_set_param(NULL,(struct flash_led_param){1,2})==-EINVAL);struct led_trigger foreign={0};assert(qti_flash_led_set_param(&foreign,(struct flash_led_param){1,2})==-ENODEV);assert(qti_flash_led_set_param(&trigger,(struct flash_led_param){0,0})==-EINVAL);assert(qti_flash_led_set_param(&trigger,(struct flash_led_param){UINT64_MAX,1})==-EINVAL);
 assert(!qti_flash_led_set_param(&trigger,(struct flash_led_param){5,20}));
 /* A switch event flushes queued brightness events before hardware strobing. */
 INIT_WORK(&fnodes[0].fdev.led_cdev.set_brightness_work,pending_brightness);schedule_work(&fnodes[0].fdev.led_cdev.set_brightness_work);
 assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,1));assert(work_runs==1&&snodes[0].on_timer.active&&snodes[0].on_pending&&!led.chan_en_map);
 fire_timer(&snodes[0].on_timer);assert(snodes[0].on_work.pending&&!led.chan_en_map);flush_work(&snodes[0].on_work);assert(snodes[0].enabled&&led.chan_en_map==1&&snodes[0].off_timer.active);
 fire_timer(&snodes[0].off_timer);assert(led.chan_en_map==1);flush_work(&snodes[0].off_work);assert(!led.chan_en_map&&!led.module_en&&!snodes[0].enabled&&!fnodes[0].configured);
 /* A canceled worker cannot rearm a timer while cancellation is in flight. */
 snodes[0].canceling=true;snodes[0].on_pending=true;int cancel_before=io_calls;qti_flash_on_work(&snodes[0].on_work);qti_flash_off_work(&snodes[0].off_work);assert(io_calls==cancel_before&&!snodes[0].off_timer.active);snodes[0].canceling=false;snodes[0].on_pending=false;
 /* Explicit off cancels an expired but queued on callback. */
 configure(0,100);assert(!qti_flash_led_set_param(&trigger,(struct flash_led_param){5,20}));assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,1));fire_timer(&snodes[0].on_timer);assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,0));assert(!snodes[0].on_work.pending&&!snodes[0].on_pending&&!snodes[0].off_timer.active&&!led.chan_en_map);
 /* Code zero is the valid 10 ms timeout; failed writes do not commit duration. */
 configure(0,100);assert(!qti_flash_timeout_set(&fnodes[0].fdev,10000));assert(fnodes[0].duration==0);assert(!qti_flash_strobe_set(&fnodes[0].fdev,true));assert(led.chan_en_map==1);assert(qti_flash_timeout_set(&fnodes[0].fdev,1)==-EINVAL);fail_io=io_calls+1;assert(qti_flash_timeout_set(&fnodes[0].fdev,20000)==-EIO);assert(fnodes[0].duration==0);fail_io=0;assert(!qti_flash_strobe_set(&fnodes[0].fdev,false));
 int output=-77;led.secure_vm=true;assert(!qti_flash_led_prepare(&trigger,QUERY_MAX_AVAIL_CURRENT,&output)&&output==2000);led.secure_vm=false;has_battery=true;assert(qti_flash_led_prepare(&trigger,QUERY_MAX_AVAIL_CURRENT,&output)==-EINVAL);assert(output==2000&&battery.refs==1);assert(qti_flash_led_prepare(&trigger,QUERY_MAX_AVAIL_CURRENT,&output)==-EINVAL&&battery.refs==1);power_supply_put(led.batt_psy);led.batt_psy=NULL;has_battery=false;
 led.stopping=true;int before=io_calls;assert(qti_flash_led_brightness_set(&fnodes[0].fdev.led_cdev,5)==-ENODEV);assert(qti_flash_led_prepare(&trigger,0,NULL)==-ENODEV);assert(io_calls==before);fini();
 /* Every strobe/brightness failure leaves an error and supports explicit off retry. */
 int fault_cases=0;
 for(int f=1;f<=5;f++) {init();configure(0,100);configure(3,100);fail_io=io_calls+f;int rc=qti_flash_led_switch_brightness_set(&snodes[0].cdev,1);if(rc){assert(rc==-EIO);assert(!led.chan_en_map&&!snodes[0].off_timer.active);}fail_io=0;assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,0));assert(!led.chan_en_map&&!led.module_en&&!fnodes[0].configured&&!fnodes[3].configured);fault_cases++;fini();}
 for(int f=1;f<=7;f++) {init();configure(0,100);configure(3,100);assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,1));fail_io=io_calls+f;int rc=qti_flash_led_switch_brightness_set(&snodes[0].cdev,0);if(rc)assert(rc==-EIO);fail_io=0;assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,0));assert(!led.chan_en_map&&!led.module_en&&!fnodes[0].configured&&!fnodes[3].configured);fault_cases++;fini();}
 for(int f=1;f<=3;f++) {init();fail_io=f;assert(qti_flash_led_brightness_set(&fnodes[0].fdev.led_cdev,100)==-EIO);assert(!fnodes[0].configured&&!fnodes[0].current_ma);fail_io=0;configure(0,100);assert(fnodes[0].configured);fini();fault_cases++;}
 /* Failed timer off rearms cleanup instead of abandoning an enabled channel. */
 init();configure(4,100);snodes[0].off_time_ms=20;assert(!qti_flash_led_switch_brightness_set(&snodes[0].cdev,1));fire_timer(&snodes[0].off_timer);fail_io=io_calls+1;flush_work(&snodes[0].off_work);assert(snodes[0].off_timer.active);fail_io=0;fire_timer(&snodes[0].off_timer);flush_work(&snodes[0].off_work);assert(!led.chan_en_map&&!snodes[0].enabled);fini();
 /* All-channel symmetry is passed an interior switch pointer. */
 init();led.non_all_mask_switch_present=true;snodes[2].symmetry_en=true;snodes[0].off_time_ms=5000;snodes[1].off_time_ms=5001;
 for(int i=4;i<8;i++){fnodes[i].custom_max_cur=(struct custom_max_current){.max_currents={300,150},.time_for_currents={5000,UINT32_MAX},.num_current=2,.always_on_max_current=150};configure(i,300);}
 assert(!qti_flash_led_switch_brightness_set(&snodes[2].cdev,1));for(int i=4;i<8;i++)assert(fnodes[i].current_ma==(i==4||i==7?300:150));assert(!qti_flash_led_switch_brightness_set(&snodes[2].cdev,0));fini();
 printf("{\"timeout_bounds\":%d,\"io_fault_cases\":%d,\"queued_on_cancellation\":true,\"sleeping_timer_work\":true,\"brightness_before_strobe\":true,\"interior_switch_symmetry\":true,\"native_battery_gap\":true}\n",checks,fault_cases);
}
