/* SERVICES */
struct swr_mstr_port_map {u32 id,uc;void*swr_port_params;};
#define SWR_MSTR_PORT_LEN 8
/* SOURCE */
static int calls,bad;static int cb(struct snd_soc_component*c,u32 uc,u32 size,void*d){assert(size==8);calls++;return bad?-EIO:0;}
int main(void){struct device_node n={.compatible="qcom,lpass-cdc"};struct device d={.of_node=&n};struct lpass_cdc_priv p;struct snd_soc_component c={.dev=&d};struct swr_mstr_port_map map[]={{0,1,NULL},{MAX_MACRO,2,NULL}};init_priv(&p,&d);p.macro_params[0].set_port_map=cb;assert(lpass_cdc_set_port_map(&c,2,map)==-EINVAL&&!calls);map[1].id=0;assert(!lpass_cdc_set_port_map(&c,2,map)&&calls==2);bad=1;assert(lpass_cdc_set_port_map(&c,2,map)==-EIO&&calls==3);lpass_cdc_quiesce(&p);assert(lpass_cdc_set_port_map(&c,1,map)==-ENODEV&&calls==3);printf("{\"ports\":4}\n");}
