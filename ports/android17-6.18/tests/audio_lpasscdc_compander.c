/* SERVICES */
#define check_sub_overflow(a,b,p) __builtin_sub_overflow(a,b,p)
/* COMP_LAYOUT */
/* SOURCE */
int main(void){struct device d={0};struct lpass_cdc_priv p;struct snd_soc_component c={.dev=&d};static struct regmap map;init_priv(&p,&d);p.regmap=&map;struct comp_coeff_val coeff[]={{0x12,0x34},{0x56,0x78}};struct lpass_cdc_comp_setting setting={.upper_gain_int=30,.lower_gain_int=-12,.ana_addr_map=7};int cases=0;
assert(lpass_cdc_load_compander_coeff(NULL,4,8,coeff,2)==-EINVAL);assert(lpass_cdc_load_compander_coeff(&c,4,8,NULL,2)==-EINVAL);assert(!lpass_cdc_load_compander_coeff(&c,4,8,coeff,2)&&map.values[4]==0x56&&map.values[8]==0x78);cases+=3;
for(int f=1;f<=4;f++){steps=0;fault=f;assert(lpass_cdc_load_compander_coeff(&c,4,8,coeff,2)==-EIO&&steps==f);cases++;}
for(int f=1;f<=9;f++){steps=0;fault=f;assert(lpass_cdc_update_compander_setting(&c,0,&setting)==-EIO&&steps==f);cases++;}
fault=0;assert(!lpass_cdc_update_compander_setting(&c,0,&setting));assert(map.values[8]==6&&map.values[12]==18&&map.values[16]==30&&map.values[20]==36&&map.values[24]==42&&map.values[28]==42&&map.values[32]==244&&map.values[36]==30&&map.values[40]==7);assert(lpass_cdc_update_compander_setting(&c,65532,&setting)==-EINVAL);setting.lower_gain_int=INT_MIN;setting.upper_gain_int=INT_MIN;assert(lpass_cdc_update_compander_setting(&c,0,&setting)==-EINVAL);setting.upper_gain_int=INT_MAX;assert(lpass_cdc_update_compander_setting(&c,0,&setting)==-EINVAL);cases+=4;printf("{\"compander\":%d}\n",cases);}
