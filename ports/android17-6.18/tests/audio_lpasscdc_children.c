/* SERVICES */
static int phases[10],removed;static struct lpass_cdc_priv *owner;
static struct platform_device *platform_device_alloc(const char*n,int id){assert(id==-1);return alloc(sizeof(struct platform_device));}
static int platform_device_add(struct platform_device*p){if(fail())return -EIO;p->registered=true;return 0;}
static void platform_device_put(struct platform_device*p){assert(!p->registered);of_node_put(p->dev.of_node);kfree(p);}
static void platform_device_unregister(struct platform_device*p){assert(p->registered);assert(p->dev.parent->platform_data==&owner->plat_data);struct device_node*n=p->dev.of_node;phases[removed++]=n->split?0:of_device_is_compatible(n,"qcom,lpass-cdc-clk-rsc-mngr")?2:1;of_node_put(n);kfree(p);}
/* SOURCE */
int main(void){struct device_node root={0},nodes[7]={0};struct device d={.of_node=&root};struct lpass_cdc_priv p;owner=&p;for(int i=0;i<7;i++){nodes[i].name="child";nodes[i].refs=1;nodes[i].next=i<5?&nodes[i+1]:NULL;}nodes[0].compatible="qcom,lpass-cdc-clk-rsc-mngr";nodes[4].split=true;root.child=nodes;int cases=0;
for(int f=1;f<=12;f++){init_priv(&p,&d);steps=0;fault=f;removed=0;assert(lpass_cdc_add_child_devices(&p)<0);assert(!p.child_count&&!d.platform_data&&!live);for(int i=0;i<7;i++)assert(nodes[i].refs==1);cases++;}
fault=0;init_priv(&p,&d);assert(!lpass_cdc_add_child_devices(&p));assert(p.child_count==6&&live==6);removed=0;lpass_cdc_remove_child_devices(&p);assert(removed==6&&phases[0]==0&&phases[5]==2&&!live);for(int i=0;i<7;i++)assert(nodes[i].refs==1);cases+=2;
nodes[5].next=&nodes[6];init_priv(&p,&d);removed=0;assert(lpass_cdc_add_child_devices(&p)==-E2BIG);assert(!live&&!p.child_count);for(int i=0;i<7;i++)assert(nodes[i].refs==1);cases++;printf("{\"children\":%d}\n",cases);}
