/* SERVICES */
#define str_has_prefix(s,p) (!strncmp(s,p,strlen(p)))
static bool notify_block;static atomic_int entered,release,removed;
static int swrm_wcd_notify(struct platform_device*p,u32 event,void*d){assert(p->registered);if(notify_block){atomic_store(&entered,1);while(!atomic_load(&release))usleep(100);}return -EIO;}
static struct platform_device *platform_device_alloc(const char*n,int id){return alloc(sizeof(struct platform_device));}
static int platform_device_add_data(struct platform_device*p,void*d,size_t n){return fail()?-ENOMEM:0;}
static int platform_device_add(struct platform_device*p){if(fail())return -EIO;p->registered=true;return 0;}
static void platform_device_put(struct platform_device*p){assert(!p->registered);of_node_put(p->dev.of_node);kfree(p);}
static void platform_device_unregister(struct platform_device*p){assert(p->registered);atomic_fetch_add(&removed,1);of_node_put(p->dev.of_node);kfree(p);}
/* SOURCE */
static struct lpass_cdc_va_macro_priv p;
static void *notifier(void*x){assert(lpass_cdc_va_macro_notify(&p,1,NULL)==-EIO);return NULL;}
static void *remove_thread(void*x){lpass_cdc_va_macro_remove_children(&p);return NULL;}
int main(void){struct device_node root={0},node={.refs=1,.name="va_swr_master"};root.child=&node;struct device d={.of_node=&root};int cases=0;
for(int f=1;f<=4;f++){init_va(&p,&d);fault=f;steps=0;assert(lpass_cdc_va_macro_add_child_devices(&p)<0);assert(!p.child_count&&!p.swr_ctrl_data&&!live&&node.refs==1);cases++;}
fault=0;init_va(&p,&d);assert(!lpass_cdc_va_macro_add_child_devices(&p));assert(p.child_count==1&&node.refs==2);notify_block=true;atomic_store(&removed,0);pthread_t a,b;pthread_create(&a,NULL,notifier,NULL);while(!atomic_load(&entered))usleep(100);pthread_create(&b,NULL,remove_thread,NULL);usleep(20000);assert(!atomic_load(&removed));assert(lpass_cdc_va_macro_notify(&p,1,NULL)==-ENODEV);atomic_store(&release,1);pthread_join(a,NULL);pthread_join(b,NULL);assert(node.refs==1&&!live&&!p.swr_ctrl_data&&atomic_load(&removed)==1);cases+=3;
struct device_node duplicate={.name="va_swr_master_duplicate",.refs=1};node.next=&duplicate;init_va(&p,&d);assert(lpass_cdc_va_macro_add_child_devices(&p)==-EINVAL);assert(!live&&node.refs==1&&duplicate.refs==1);cases++;node.next=NULL;struct device_node extra[4];memset(extra,0,sizeof(extra));for(int i=0;i<4;i++){extra[i].name="aux";extra[i].refs=1;if(i<3)extra[i].next=&extra[i+1];}root.child=extra;init_va(&p,&d);assert(lpass_cdc_va_macro_add_child_devices(&p)==-E2BIG&&!p.child_count&&!live);for(int i=0;i<4;i++)assert(extra[i].refs==1);cases++;printf("{\"children\":%d}\n",cases);}
