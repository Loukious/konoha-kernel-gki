/* SERVICES */
#define CONFIG_PM 1
#define ENOTSYNC 522
#define READ_ONCE(v) (v)
#define WRITE_ONCE(v,x) ((v)=(x))
#define MODULE_DEVICE_TABLE(...)
#define SET_SYSTEM_SLEEP_PM_OPS(...)
#define SET_RUNTIME_PM_OPS(...)
#define MODULE_SOFTDEP(...)
#define SND_EVENT_UP 1
#define SWR_DEVICE_SSR_UP 1
#define SWR_DEVICE_SSR_DOWN 2
typedef int irqreturn_t;
struct blocking_notifier_head {int unused;};
struct clk {int refs;};static struct clk clocks[4];
struct snd_event_ops {int(*enable)(struct device*,void*);void(*disable)(struct device*,void*);};
struct of_device_id {const char*compatible;};
struct dev_pm_ops {int unused;};
struct platform_driver {struct {const char*name;const struct dev_pm_ops*pm;const struct of_device_id*of_match_table;bool suppress_bind_attrs;}driver;int(*probe)(struct platform_device*);void(*remove)(struct platform_device*);};
static int registration_error;
static int platform_driver_register(struct platform_driver*p){assert(p->probe&&p->remove);return registration_error;}
static void platform_driver_unregister(struct platform_driver*p){assert(p);}
static void mutex_init(struct mutex*m){pthread_mutex_init(&m->lock,NULL);}
static void *platform_get_drvdata(struct platform_device*p){return p->dev.data;}
static struct device_node *of_node_get(struct device_node*n){if(n)n->refs++;return n;}
static struct device_node *next_child(struct device_node*n){struct device_node*next=n->next;of_node_put(n);return of_node_get(next);}
#define for_each_available_child_of_node(np,n) for((n)=of_node_get((np)->child);(n);(n)=next_child(n))
static char *strnstr(const char*s,const char*n,size_t len){return strncmp(s,n,len)?NULL:(char*)s;}
static int child_registered,child_unregistered;
static struct platform_device *platform_device_alloc(const char*n,int id){assert(!strcmp(n,"bt_swr_mstr")&&id==-1);struct platform_device*p=alloc(sizeof(*p));if(p)p->dev.refs=1;return p;}
static void platform_device_put(struct platform_device*p){assert(!p->registered&&p->dev.refs==1);cleanup_from(&p->dev,0);of_node_put(p->dev.of_node);kfree(p);}
static int platform_device_add_data(struct platform_device*p,const void*data,size_t len){p->pdata=devm_kzalloc(&p->dev,len,0);if(!p->pdata)return -ENOMEM;memcpy(p->pdata,data,len);return 0;}
static int platform_device_add(struct platform_device*p);
static void platform_device_unregister(struct platform_device*p);
static int pin_state,pin_get_error,pin_active_error,pin_sleep_error;
static int msm_cdc_pinctrl_get_state(struct device_node*n){assert(n);return pin_get_error?pin_get_error:pin_state;}
static int msm_cdc_pinctrl_select_active_state(struct device_node*n){assert(n);if(pin_active_error)return pin_active_error;pin_state=1;return 0;}
static int msm_cdc_pinctrl_select_sleep_state(struct device_node*n){assert(n);if(pin_sleep_error)return pin_sleep_error;pin_state=0;return 0;}
static int clk_get_step,clk_get_error_at,clk_get_error=-EPROBE_DEFER,clk_enable_step,clk_enable_error_at;static bool optional_missing;
static struct clk *get_clock(const char*n){int id=!strcmp(n,"lpass_core_hw_vote")?0:!strcmp(n,"lpass_audio_hw_vote")?1:!strcmp(n,"bt_swr_mclk_clk")?2:3;if(++clk_get_step==clk_get_error_at)return ERR_PTR(clk_get_error);return &clocks[id];}
static struct clk *devm_clk_get_optional(struct device*d,const char*n){(void)d;struct clk*c=get_clock(n);return optional_missing&&!IS_ERR(c)?NULL:c;}
static struct clk *devm_clk_get(struct device*d,const char*n){(void)d;return get_clock(n);}
static int clk_prepare_enable(struct clk*c){if(!c)return 0;if(++clk_enable_step==clk_enable_error_at)return -EIO;c->refs++;return 0;}
static void clk_disable_unprepare(struct clk*c){if(c){assert(c->refs>0);c->refs--;}}
static int digital_cdc_rsc_mgr_hw_vote_enable(struct clk*c,struct device*d){assert(d);return clk_prepare_enable(c);}
static void digital_cdc_rsc_mgr_hw_vote_disable(struct clk*c,struct device*d){assert(d);clk_disable_unprepare(c);}
static int lpass_bt_swr_runtime_resume(struct device*);
static int lpass_bt_swr_runtime_suspend(struct device*);
static int pm_error;
static int pm_runtime_resume_and_get(struct device*d){assert(d->runtime_enabled);if(pm_error)return pm_error;if(!d->runtime_active){int ret=lpass_bt_swr_runtime_resume(d);if(ret)return ret;d->runtime_active=true;}d->pm_refs++;return 0;}
static void pm_runtime_mark_last_busy(struct device*d){assert(d);}
static void pm_runtime_put_autosuspend(struct device*d){assert(d->pm_refs>0);d->pm_refs--;}
static void pm_runtime_set_autosuspend_delay(struct device*d,int n){assert(d&&n==100);}
static void pm_runtime_use_autosuspend(struct device*d){assert(d);}
static void pm_runtime_set_suspended(struct device*d){assert(!d->runtime_enabled&&!d->pm_refs);d->runtime_active=false;}
static void pm_suspend_ignore_children(struct device*d,bool ignored){assert(d&&ignored);}
static void pm_runtime_enable(struct device*d){assert(!d->runtime_enabled);d->runtime_enabled=true;}
static void pm_runtime_disable(struct device*d){assert(d->runtime_enabled);d->runtime_enabled=false;}
static pthread_mutex_t event_lock=PTHREAD_MUTEX_INITIALIZER;
static const struct snd_event_ops*event_ops;static void*event_data;static struct device*event_dev;
static int event_notify_error,notify_error,notifications,event_deregistrations;
static atomic_bool block_notify,notify_entered,notify_release,remove_done;
static int snd_event_client_register(struct device*d,const struct snd_event_ops*ops,void*data){if(fail())return -ENOMEM;assert(!event_ops);event_ops=ops;event_data=data;event_dev=d;return 0;}
static int snd_event_notify(struct device*d,int up){assert(up==1&&d==event_dev);if(event_notify_error)return event_notify_error;pthread_mutex_lock(&event_lock);int ret=event_ops->enable(d,event_data);pthread_mutex_unlock(&event_lock);return ret;}
static int snd_event_client_deregister(struct device*d){assert(d==event_dev);pthread_mutex_lock(&event_lock);assert(event_ops);event_ops=NULL;event_data=NULL;event_dev=NULL;event_deregistrations++;pthread_mutex_unlock(&event_lock);return 0;}
static int swrm_wcd_notify(struct platform_device*p,u32 id,void*data){assert(p&&p->registered&&(id==1||id==2)&&!data);notifications++;if(block_notify){notify_entered=true;while(!notify_release)usleep(100);}return notify_error;}
/* CONTROLLER_LAYOUT */
/* SOURCE */
_Static_assert(sizeof(struct lpass_bt_swr_ctrl_platform_data)==sizeof(struct swr_ctrl_platform_data),"native parent layout");
_Static_assert(offsetof(struct lpass_bt_swr_ctrl_platform_data,handle_irq)==offsetof(struct swr_ctrl_platform_data,reg_irq),"native IRQ slot");
static int platform_device_add(struct platform_device*p){if(fail())return -EIO;struct lpass_bt_swr_ctrl_platform_data*d=p->pdata;assert(p->dev.parent->runtime_enabled&&p->dev.parent->data==d->handle);assert(d->clk==lpass_bt_swrm_clock&&d->core_vote==lpass_bt_swr_core_vote&&!d->read&&!d->write&&!d->bulk_write&&!d->handle_irq);p->registered=true;child_registered++;return 0;}
static void platform_device_unregister(struct platform_device*p){assert(p->registered&&!event_ops&&p->dev.parent->runtime_enabled);struct lpass_bt_swr_ctrl_platform_data*d=p->pdata;struct lpass_bt_swr_priv*priv=d->handle;while(priv->swr_clk_users)assert(!d->clk(priv,false));p->registered=false;child_registered--;child_unregistered++;platform_device_put(p);}
static struct device_node node,child,extra;static struct platform_device parent;
static void setup(void){assert(!live&&!child_registered&&!event_ops);node=(struct device_node){0};child=(struct device_node){.name="bt_swr_mstr"};extra=(struct device_node){.name="bt_swr_mstr1"};node.child=&child;prop(&node,"qcom,bt-swr-gpios",1,0);parent=(struct platform_device){.dev.of_node=&node};step=fail_step=clk_get_step=clk_get_error_at=clk_enable_step=clk_enable_error_at=pm_error=pin_get_error=pin_active_error=pin_sleep_error=event_notify_error=notify_error=notifications=event_deregistrations=child_unregistered=pin_state=0;optional_missing=false;for(int i=0;i<4;i++)assert(!clocks[i].refs);}
static void clean_parent(void){cleanup_from(&parent.dev,0);assert(!live&&!node.refs&&!child.refs&&!extra.refs&&!child_registered&&!event_ops&&!parent.dev.runtime_enabled&&!parent.dev.pm_refs);for(int i=0;i<4;i++)assert(!clocks[i].refs);}
static struct lpass_bt_swr_priv *probe(void){assert(!lpass_bt_swr_probe(&parent));return parent.dev.data;}
static unsigned probe_failures(void){for(int n=1;n<=6;n++){setup();fail_step=n;assert(lpass_bt_swr_probe(&parent)<0&&!parent.dev.data);clean_parent();}for(int n=1;n<=4;n++){setup();clk_get_error_at=n;assert(lpass_bt_swr_probe(&parent)==-EPROBE_DEFER&&!parent.dev.data);clean_parent();}setup();pin_get_error=-EINVAL;assert(lpass_bt_swr_probe(&parent)==-EPROBE_DEFER);clean_parent();setup();event_notify_error=-EIO;assert(lpass_bt_swr_probe(&parent)==-EIO&&event_deregistrations==1&&child_unregistered==1);clean_parent();return 12;}
static unsigned children(void){setup();node.child=NULL;assert(lpass_bt_swr_probe(&parent)==-ENODEV);clean_parent();setup();child.next=&extra;assert(lpass_bt_swr_probe(&parent)==-EINVAL&&!child_registered);clean_parent();setup();struct lpass_bt_swr_priv*p=probe();assert(p->swr_pdev&&child.refs==1&&node.refs==1);lpass_bt_swr_remove(&parent);assert(child_unregistered==1&&event_deregistrations==1&&!parent.dev.data);clean_parent();return 3;}
static unsigned votes(void){setup();struct lpass_bt_swr_priv*p=probe();for(int n=1;n<=2;n++){clk_enable_step=0;clk_enable_error_at=n;assert(lpass_bt_swr_core_vote(p,true)==-EIO&&!parent.dev.pm_refs&&!clocks[0].refs&&!clocks[1].refs&&!p->core_hw_vote_count&&!p->core_audio_vote_count);}clk_enable_error_at=0;assert(!lpass_bt_swr_core_vote(p,true)&&parent.dev.pm_refs==1);assert(!lpass_bt_swr_core_vote(p,false)&&!parent.dev.pm_refs);assert(!lpass_bt_swr_runtime_suspend(&parent.dev));parent.dev.runtime_active=false;pm_error=-EAGAIN;assert(lpass_bt_swr_core_vote(p,true)==-EAGAIN&&!parent.dev.pm_refs);pm_error=0;p->dev_up=false;assert(lpass_bt_swr_core_vote(p,true)==-ENOTSYNC);p->dev_up=true;lpass_bt_swr_remove(&parent);clean_parent();return 6;}
static unsigned mclk(void){setup();struct lpass_bt_swr_priv*p=probe();pm_error=-EAGAIN;assert(lpass_bt_swrm_clock(p,true)==-EAGAIN&&!parent.dev.pm_refs&&!p->swr_clk_users&&!p->mclk_enabled);pm_error=0;assert(!lpass_bt_swr_core_vote(p,true));assert(!lpass_bt_swr_core_vote(p,false));for(int n=1;n<=2;n++){clk_enable_step=0;clk_enable_error_at=n;assert(lpass_bt_swrm_clock(p,true)==-EIO&&!p->swr_clk_users&&!p->mclk_enabled&&!clocks[2].refs&&!clocks[3].refs&&!parent.dev.pm_refs&&!pin_state);}clk_enable_error_at=0;pin_active_error=-EINVAL;assert(lpass_bt_swrm_clock(p,true)==-EINVAL&&!p->swr_clk_users&&!parent.dev.pm_refs);pin_active_error=0;assert(!lpass_bt_swrm_clock(p,true)&&!lpass_bt_swrm_clock(p,true)&&p->swr_clk_users==2&&clocks[2].refs==1&&clocks[3].refs==1);assert(!lpass_bt_swrm_clock(p,false)&&p->mclk_enabled);assert(!lpass_bt_swrm_clock(p,false)&&!p->mclk_enabled);assert(lpass_bt_swrm_clock(p,false)==-EINVAL);p->swr_clk_users=INT_MAX;assert(lpass_bt_swrm_clock(p,true)==-EOVERFLOW);p->swr_clk_users=0;lpass_bt_swr_remove(&parent);clean_parent();return 9;}
static unsigned ssr(void){setup();struct lpass_bt_swr_priv*p=probe();assert(!lpass_bt_swrm_clock(p,true));lpass_bt_swr_ssr_disable(&parent.dev,p);assert(!p->dev_up&&notifications==1);pin_active_error=-EIO;assert(lpass_bt_swr_ssr_enable(&parent.dev,p)==-EIO&&!p->dev_up&&!p->mclk_enabled&&notifications==1);pin_active_error=0;assert(!lpass_bt_swr_ssr_enable(&parent.dev,p)&&p->dev_up&&p->mclk_enabled&&notifications==2);lpass_bt_swr_ssr_disable(&parent.dev,p);clk_enable_step=0;clk_enable_error_at=2;assert(lpass_bt_swr_ssr_enable(&parent.dev,p)==-EIO&&!p->dev_up&&!p->mclk_enabled&&!clocks[2].refs&&!clocks[3].refs);clk_enable_error_at=0;assert(!lpass_bt_swr_ssr_enable(&parent.dev,p)&&p->dev_up);lpass_bt_swr_remove(&parent);clean_parent();return 5;}
static void *event_thread(void*p){(void)p;pthread_mutex_lock(&event_lock);event_ops->disable(event_dev,event_data);pthread_mutex_unlock(&event_lock);return NULL;}
static void *remove_thread(void*p){(void)p;lpass_bt_swr_remove(&parent);remove_done=true;return NULL;}
static unsigned lifetime(void){setup();struct lpass_bt_swr_priv*p=probe();assert(!lpass_bt_swrm_clock(p,true));block_notify=true;pthread_t a,b;pthread_create(&a,NULL,event_thread,NULL);while(!notify_entered)usleep(100);pthread_create(&b,NULL,remove_thread,NULL);usleep(2000);assert(!remove_done&&child_registered==1&&parent.dev.runtime_enabled);notify_release=true;pthread_join(a,NULL);pthread_join(b,NULL);assert(remove_done&&child_unregistered==1);clean_parent();return 3;}
static unsigned optional(void){setup();optional_missing=true;struct lpass_bt_swr_priv*p=probe();assert(!p->lpass_core_hw_vote&&!p->lpass_audio_hw_vote&&!p->clk_handle_2x);assert(!lpass_bt_swr_core_vote(p,true)&&!lpass_bt_swr_core_vote(p,false));assert(!lpass_bt_swrm_clock(p,true)&&clocks[2].refs==1&&!clocks[3].refs);lpass_bt_swr_remove(&parent);clean_parent();setup();p=probe();parent.dev.runtime_active=true;assert(lpass_bt_swr_core_vote(p,true)==-ENOTSYNC&&!parent.dev.pm_refs);lpass_bt_swr_remove(&parent);clean_parent();return 4;}
static unsigned module_init_test(void){registration_error=-ENOMEM;assert(lpass_bt_swr_init()==-ENOMEM);registration_error=0;assert(!lpass_bt_swr_init());lpass_bt_swr_exit();return 2;}
int main(int argc,char**argv){assert(argc==2);unsigned n;if(!strcmp(argv[1],"probe_failures"))n=probe_failures();else if(!strcmp(argv[1],"children"))n=children();else if(!strcmp(argv[1],"votes"))n=votes();else if(!strcmp(argv[1],"mclk"))n=mclk();else if(!strcmp(argv[1],"ssr"))n=ssr();else if(!strcmp(argv[1],"lifetime"))n=lifetime();else if(!strcmp(argv[1],"optional"))n=optional();else if(!strcmp(argv[1],"module_init"))n=module_init_test();else abort();printf("{\"%s\":%u}\n",argv[1],n);}
