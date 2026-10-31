/* SERVICES */
static int elems=-1;static u32 factors[4]={2,3,4,6};
static bool of_property_present(struct device_node*n,const char*name){return elems!=-1;}
static int of_property_count_u32_elems(struct device_node*n,const char*name){return elems;}
static int of_property_read_u32_array(struct device_node*n,const char*name,u32*out,int count){if(fail())return -EIO;assert(count==4);memcpy(out,factors,sizeof(factors));return 0;}
/* SOURCE */
int main(void){struct device d={0};struct lpass_cdc_va_macro_priv p;init_va(&p,&d);assert(!lpass_cdc_va_macro_parse_divs(&p,"x",false));int cases=1;for(int n=0;n<8;n++){if(n==4)continue;elems=n;assert(lpass_cdc_va_macro_parse_divs(&p,"x",false)==-EINVAL);cases++;}elems=4;fault=steps+1;assert(lpass_cdc_va_macro_parse_divs(&p,"x",false)==-EIO);fault=0;factors[3]=17;assert(lpass_cdc_va_macro_parse_divs(&p,"x",false)==-EINVAL&&p.dmic_clk_div[1]==0);factors[3]=6;assert(!lpass_cdc_va_macro_parse_divs(&p,"x",false));for(int i=0;i<4;i++)assert(p.dmic_clk_div[i]==i);factors[0]=16;assert(!lpass_cdc_va_macro_parse_divs(&p,"x",true)&&p.dmic_override_clk_div[0]==5);cases+=4;printf("{\"divs\":%d}\n",cases);}
