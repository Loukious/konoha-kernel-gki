struct prop {const char *name,*string;u32 values[12];int count;};
struct device_node {const char *full_name;struct prop props[24];int count,refs;struct device_node *children[12];int nchildren;};
static int resource_step,fail_resource,gpio_rc=-ENOENT,irq_rc=-ENOENT;
static int published,released,retained_nodes;
struct resource {void (*release)(void *);void *data;};
static struct resource resources[128];static int nresources;
static int resource_fault(void) {return ++resource_step==fail_resource?-ENOMEM:0;}
static void add_resource(void (*fn)(void *),void *p) {assert(nresources<128);resources[nresources++]=(struct resource){fn,p};}
static void release_resources(void) {while(nresources){struct resource r=resources[--nresources];r.release(r.data);}}
static void *devm_kzalloc(struct device *d,size_t n,int flags) {if(resource_fault())return NULL;void *p=calloc(1,n);assert(p);add_resource(free,p);return p;}
static void *devm_kcalloc(struct device *d,size_t n,size_t size,int flags) {return devm_kzalloc(d,n*size,flags);}
static int devm_add_action_or_reset(struct device *d,void (*fn)(void *),void *p) {if(resource_fault()){fn(p);return -ENOMEM;}add_resource(fn,p);return 0;}
static struct device_node *of_node_get(struct device_node *p) {p->refs++;retained_nodes++;return p;}
static void of_node_put(struct device_node *p) {if(p){assert(p->refs>0);p->refs--;retained_nodes--;}}
static struct device_node *next_child(struct device_node *parent,struct device_node *prev) {int i=0;if(prev){for(;i<parent->nchildren&&parent->children[i]!=prev;i++);assert(i<parent->nchildren);i++;of_node_put(prev);}if(i==parent->nchildren)return NULL;return of_node_get(parent->children[i]);}
#define for_each_available_child_of_node(p,c) for(c=next_child(p,NULL);c;c=next_child(p,c))
static struct prop *property(struct device_node *n,const char *name) {for(int i=0;i<n->count;i++)if(!strcmp(n->props[i].name,name))return &n->props[i];return NULL;}
static bool of_property_present(struct device_node *n,const char *name) {return property(n,name)!=NULL;}
static bool of_property_read_bool(struct device_node *n,const char *name) {return of_property_present(n,name);}
static int of_property_read_string(struct device_node *n,const char *name,const char **out) {struct prop *p=property(n,name);if(!p||!p->string)return -EINVAL;*out=p->string;return 0;}
static int of_property_read_u32(struct device_node *n,const char *name,u32 *out) {struct prop *p=property(n,name);if(!p||p->count<1)return -EINVAL;*out=p->values[0];return 0;}
static int of_property_count_u32_elems(struct device_node *n,const char *name) {struct prop *p=property(n,name);return p?p->count:-EINVAL;}
static int of_property_read_u32_array(struct device_node *n,const char *name,u32 *out,int count) {struct prop *p=property(n,name);if(!p||p->count<count)return -EINVAL;memcpy(out,p->values,count*sizeof(*out));return 0;}
static int of_get_named_gpio(struct device_node *n,const char *name,int i) {return gpio_rc;}
static int of_irq_get_byname(struct device_node *n,const char *name) {return irq_rc==-EPROBE_DEFER&&strcmp(name,"all-ramp-up-done-irq")?10:irq_rc;}
static int devm_gpio_request_one(struct device *d,int n,int flags,const char *name) {return resource_fault();}
static int scnprintf(char *s,size_t size,const char *fmt,...) {va_list ap;va_start(ap,fmt);int n=vsnprintf(s,size,fmt,ap);va_end(ap);return n;}
static struct regmap device_map;
static void *dev_get_regmap(struct device *d,void *x) {return &device_map;}
static const void *of_device_get_match_data(struct device *d) {return (void *)4;}
static void release_led(void *data) {struct led_classdev *c=data;assert(c->brightness_set_blocking(c,0)==-ENODEV);released++;free(c->dev);c->dev=NULL;}
static int devm_led_classdev_register(struct device *d,struct led_classdev *c) {if(resource_fault())return -ENOMEM;int before=io_calls;assert(c->brightness_set_blocking(c,1)==-ENODEV);assert(before==io_calls);c->dev=calloc(1,sizeof(*c->dev));assert(c->dev);c->dev->data=c;add_resource(release_led,c);published++;return 0;}
static int devm_led_classdev_flash_register(struct device *d,struct led_classdev_flash *f) {return devm_led_classdev_register(d,&f->led_cdev);}

struct fake_irq {irqreturn_t (*thread)(int,void *);void *data;};
static void release_irq(void *data) {struct fake_irq *irq=data;int before=io_calls;irq->thread(0,irq->data);assert(before==io_calls);free(irq);}
static int request_irq(irqreturn_t (*thread)(int,void *),void *data) {if(resource_fault())return -ENOMEM;struct fake_irq *irq=malloc(sizeof(*irq));*irq=(struct fake_irq){thread,data};add_resource(release_irq,irq);return 0;}
