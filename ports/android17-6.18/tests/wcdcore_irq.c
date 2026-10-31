/* SERVICES */
#define IRQF_ONESHOT 1
#define IRQF_TRIGGER_RISING 2
struct irq_data {int unused;};struct lock_class_key {int unused;};
struct irq_chip {const char*name;void(*irq_disable)(struct irq_data*);void(*irq_enable)(struct irq_data*);};
struct irq_domain {void*host_data;bool mapping,regmap;};
struct irq_domain_ops {int(*map)(struct irq_domain*,unsigned,irq_hw_number_t);};
struct regmap {int unused;};struct regmap_irq_chip {int unused;};struct regmap_irq_chip_data {struct irq_domain*parent;};
static struct irq_domain*domains[4];static int enabled,disabled,requested,freed,map_result=32;
static int handle_simple_irq;
static void irq_set_chip_and_handler(unsigned v,struct irq_chip*c,int handler){(void)handler;assert(v&&c->name);}
static void irq_set_lockdep_class(unsigned v,struct lock_class_key*a,struct lock_class_key*b){assert(v&&a&&b);}
static void irq_set_nested_thread(unsigned v,int n){assert(v&&n==1);}
static void irq_set_noprobe(unsigned v){assert(v);}
static struct irq_domain *irq_domain_add_linear(void*np,int n,const struct irq_domain_ops*ops,void*ctx){assert(!np&&n==1&&ops);struct irq_domain*d=alloc(sizeof(*d));if(!d)return NULL;d->host_data=ctx;for(int i=0;i<4;i++)if(!domains[i]){domains[i]=d;return d;}abort();}
static unsigned irq_create_mapping(struct irq_domain*d,int hw){assert(d&&!hw);if(fail())return 0;d->mapping=true;for(int i=0;i<4;i++)if(domains[i]==d)return i+1;abort();}
static void irq_dispose_mapping(unsigned v){assert(v&&domains[v-1]&&domains[v-1]->mapping&&!domains[v-1]->regmap);domains[v-1]->mapping=false;}
static void irq_domain_remove(struct irq_domain*d){assert(d&&!d->mapping&&!d->regmap);for(int i=0;i<4;i++)if(domains[i]==d)domains[i]=NULL;kfree(d);}
static void regmap_release(void*p){struct regmap_irq_chip_data*c=p;assert(c->parent->mapping&&c->parent->regmap);c->parent->regmap=false;kfree(c);}
static int devm_regmap_add_irq_chip(struct device*d,struct regmap*m,unsigned v,int flags,int base,struct regmap_irq_chip*chip,struct regmap_irq_chip_data**out){assert(m&&v&&flags==1&&!base&&chip);struct regmap_irq_chip_data*c=alloc(sizeof(*c));if(!c)return -EIO;c->parent=domains[v-1];assert(c->parent->mapping);c->parent->regmap=true;*out=c;add_resource(d,c,regmap_release);return 0;}
static void devm_regmap_del_irq_chip(struct device*d,unsigned v,struct regmap_irq_chip_data*c){assert(v&&domains[v-1]==c->parent);devm_remove_action(d,regmap_release,c);regmap_release(c);}
static int regmap_irq_get_virq(struct regmap_irq_chip_data*c,int irq){assert(c);(void)irq;return map_result;}
static void enable_irq(int n){assert(n>0);enabled++;}
static void disable_irq_nosync(int n){assert(n>0);disabled++;}
static int devm_request_threaded_irq(struct device*d,int n,void*p,irq_handler_t h,int flags,const char*name,void*data){assert(d&&n>0&&!p&&h&&flags==3&&name&&data);requested++;return 0;}
static void devm_free_irq(struct device*d,int n,void*p){assert(d&&n>0&&p);freed++;}
/* HEADER */
/* SOURCE */
static struct device dev;static struct regmap map;static struct regmap_irq_chip chip;
static struct wcd_irq_info info={.dev=&dev,.regmap=&map,.wcd_regmap_irq_chip=&chip,.codec_name="codec-a"};
static unsigned init_failure(void){for(int n=1;n<=5;n++){step=0;fail_step=n;struct irq_domain*d=(void*)1;int ret=wcd_irq_init(&info,&d);assert(ret<0&&!d&&!info.irq_chip&&!dev.nr&&!live);for(int i=0;i<4;i++)assert(!domains[i]);}fail_step=0;struct irq_domain*d=NULL;assert(wcd_irq_init(NULL,&d)==-EINVAL&&wcd_irq_init(&info,NULL)==-EINVAL);return 7;}
static unsigned cleanup(void){struct irq_domain*a,*b;struct device other={0};struct wcd_irq_info second=info;second.dev=&other;second.codec_name="codec-b";assert(!wcd_irq_init(&info,&a));assert(!wcd_irq_init(&second,&b));assert(!wcd_irq_chip_map(a,1,0)&&!wcd_irq_chip_map(b,2,0));struct wcd_irq_domain*ca=a->host_data,*cb=b->host_data;assert(ca->chip.name==info.codec_name&&cb->chip.name==second.codec_name&&&ca->chip!=&cb->chip);cleanup_from(&other,0);assert(!domains[1]);assert(!wcd_irq_exit(&info,a)&&!info.irq_chip&&!dev.nr);assert(!live);return 5;}
static int handler(int n,void*p){(void)n;(void)p;return 0;}
static unsigned mapping(void){struct irq_domain*d;assert(!wcd_irq_init(&info,&d));for(int n=-2;n<=0;n++){map_result=n;wcd_enable_irq(&info,0);wcd_disable_irq(&info,0);assert(!enabled&&!disabled);assert(wcd_request_irq(&info,0,"x",handler,&info)<0&&!requested);wcd_free_irq(&info,0,&info);assert(!freed);}map_result=32;wcd_enable_irq(&info,0);wcd_disable_irq(&info,0);assert(enabled==1&&disabled==1);assert(!wcd_request_irq(&info,0,"x",handler,&info)&&requested==1);wcd_free_irq(&info,0,&info);assert(freed==1);assert(!wcd_irq_exit(&info,d));return 6;}
int main(int argc,char**argv){assert(argc==2);unsigned n;if(!strcmp(argv[1],"init_failure"))n=init_failure();else if(!strcmp(argv[1],"cleanup"))n=cleanup();else if(!strcmp(argv[1],"mapping"))n=mapping();else abort();printf("{\"irq_%s\":%u}\n",argv[1],n);}
