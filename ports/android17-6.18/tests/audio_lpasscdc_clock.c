/* SERVICES */
/* CLOCK_LAYOUT */
/* SOURCE */
static struct device_node node={.compatible="qcom,lpass-cdc"};
static struct device parent={.of_node=&node},child={.parent=&parent},rdev={.parent=&parent};
static struct lpass_cdc_priv cdc;
static struct lpass_cdc_clk_rsc rsc;
static struct clk clocks[MAX_CLK];static u32 mux[3];
static void reset(void){memset(&rsc,0,sizeof(rsc));memset(clocks,0,sizeof(clocks));memset(mux,0,sizeof(mux));init_priv(&cdc,&parent);cdc.clk_dev=&rdev;rdev.data=&rsc;rsc.dev=&rdev;rsc.dev_up=rsc.dev_up_gfmux=true;mutex_init(&rsc.rsc_clk_lock);mutex_init(&rsc.fs_gen_lock);rsc.rx_clk_muxsel=(void*)&mux[0];rsc.wsa_clk_muxsel=(void*)&mux[1];rsc.va_clk_muxsel=(void*)&mux[2];for(int i=0;i<MAX_CLK;i++)rsc.clk[i]=&clocks[i];fault=steps=0;nr_disabled=0;}
static int request(int def,int id,bool en){return lpass_cdc_clk_rsc_request_clock(&child,def,id,en);}
static void empty(void){for(int i=0;i<MAX_CLK;i++)assert(!clocks[i].refs&&!rsc.owned[i]&&!rsc.request_count[i]&&!rsc.clk_cnt[i]);}
int main(void){int cases=0;reset();
for(int i=-1;i<=MAX_CLK;i++){if(i>=0&&i<MAX_CLK)continue;assert(request(0,i,true)==-EINVAL);assert(lpass_cdc_clock_request(&rsc,0,i,true)==-EINVAL);assert(request(i,0,true)==-EINVAL);cases+=3;}
assert(request(0,0,false)==-EINVAL);assert(!request(0,0,true));assert(!request(0,0,true));assert(clocks[0].refs==1&&rsc.clk_cnt[0]==2);assert(request(1,0,true)==-EBUSY);assert(!request(0,0,false));assert(!request(0,0,false));empty();cases+=6;
for(int f=1;f<=2;f++){reset();fault=f;assert(request(0,1,true)==-EIO);empty();assert(!mux[0]);cases++;}
reset();assert(!request(0,1,true));assert(mux[0]==1&&clocks[0].refs==0&&clocks[1].refs==1);fault=steps+1;assert(request(0,1,false)==-EIO);assert(mux[0]==1&&rsc.request_count[1]==1&&rsc.owned[1]==1);fault=0;assert(!request(0,1,false));empty();cases+=3;
reset();assert(!request(0,3,true));assert(clocks[3].refs==1);fault=steps+1;assert(request(0,0,true)==-EIO);assert(rsc.request_count[0]==0&&clocks[3].refs==1);fault=0;assert(!request(0,0,true));assert(clocks[3].refs==0&&clocks[0].refs==1&&rsc.clk_cnt[0]==2);fault=steps+1;assert(request(0,0,false)==-EIO);assert(rsc.clk_cnt[0]==2&&rsc.request_count[0]==1);fault=0;assert(!request(0,0,false));assert(clocks[3].refs==1&&!clocks[0].refs);assert(!request(0,3,false));empty();cases+=5;
reset();assert(!request(0,0,true));clocks[0].refs+=3;assert(!lpass_cdc_rsc_clk_reset(&child,0));assert(clocks[0].refs==4&&clocks[0].disables==1);fault=steps+1;assert(lpass_cdc_rsc_clk_reset(&child,0)==-EIO);assert(clocks[0].refs==3&&!rsc.owned[0]);fault=0;assert(!lpass_cdc_rsc_clk_reset(&child,0));assert(clocks[0].refs==4);assert(!request(0,0,false));assert(clocks[0].refs==3);cases+=4;
for(int f=1;f<=MAX_CLK;f++){reset();fault=f;assert(lpass_cdc_clk_rsc_all_clocks(&child,true)==-EIO);empty();assert(!rsc.all_count);cases++;}
reset();assert(!lpass_cdc_clk_rsc_all_clocks(&child,true));assert(!lpass_cdc_clk_rsc_all_clocks(&child,true));assert(!request(0,0,true));assert(clocks[0].refs==2);assert(!lpass_cdc_clk_rsc_all_clocks(&child,false));assert(!lpass_cdc_clk_rsc_all_clocks(&child,false));assert(clocks[0].refs==1);assert(!request(0,0,false));empty();cases+=5;
reset();rsc.request_count[0]=rsc.clk_cnt[0]=INT_MAX;assert(request(0,0,true)==-EOVERFLOW);rsc.request_count[3]=INT_MAX;rsc.default_clk_id[3]=0;assert(request(0,3,true)==-EOVERFLOW);cases+=2;
reset();assert(!request(RX_TX_CORE_CLK,RX_CORE_CLK,true));nr_disabled=0;assert(!request(RX_TX_CORE_CLK,RX_CORE_CLK,false));assert(nr_disabled==2&&disabled[0]==&clocks[RX_CORE_CLK]&&disabled[1]==&clocks[RX_TX_CORE_CLK]);empty();cases++;
static struct regmap map;u32 seq[]={4,15,5,8,255,23,12,127,55};
for(int f=1;f<=7;f++){reset();memset(&map,0,sizeof(map));map.values[4]=0xa0;map.values[8]=0x81;map.values[12]=0x92;rsc.regmap=&map;rsc.fs_gen_seq=seq;rsc.num_fs_reg=3;fault=f;assert(lpass_cdc_fs_gen_request(&rsc,true)<0);assert(!rsc.reg_seq_en_cnt);assert(map.values[4]==0xa0&&map.values[8]==0x81&&map.values[12]==0x92);assert(!live);cases++;}
reset();rsc.regmap=&map;rsc.fs_gen_seq=seq;rsc.num_fs_reg=3;assert(!lpass_cdc_fs_gen_request(&rsc,true));assert(rsc.reg_seq_en_cnt==1);assert(!lpass_cdc_fs_gen_request(&rsc,true));assert(!lpass_cdc_fs_gen_request(&rsc,false));assert(!lpass_cdc_fs_gen_request(&rsc,false));assert(lpass_cdc_fs_gen_request(&rsc,false)==-EINVAL);assert((map.values[4]&15)==0&&map.values[8]==0&&(map.values[12]&127)==0&&!live);cases+=5;
u32 native_seq[]={0x3000,1,1,0x3004,3,3,0x3004,3,1,0x3080,2,2};reset();memset(&map,0,sizeof(map));rsc.regmap=&map;rsc.fs_gen_seq=native_seq;rsc.num_fs_reg=4;assert(!lpass_cdc_fs_gen_request(&rsc,true));assert(map.values[0x3000]==1&&map.values[0x3004]==1&&map.values[0x3080]==2);assert(!lpass_cdc_fs_gen_request(&rsc,false));assert(!map.values[0x3000]&&!map.values[0x3004]&&!map.values[0x3080]&&!live);cases++;
printf("{\"clock\":%d}\n",cases);}
