/* SERVICES */
static int pmrefs,gets,pmputs;static int pm_runtime_resume_and_get(struct device*d){gets++;if(fail())return -EIO;pmrefs++;return 0;}
static void pm_runtime_mark_last_busy(struct device*d){}
static void pm_runtime_put_autosuspend(struct device*d){pmputs++;assert(pmrefs-->0);}
/* SOURCE */
int main(void){struct device d={0},va={.parent=&d};struct lpass_cdc_priv p;u32 mem[4]={0x123456ab};struct clk core={0},audio={0};u8 val=0;int cases=0;init_priv(&p,&d);p.macro_params[0].io_base=(void*)mem;p.macro_params[VA_MACRO].dev=&va;p.lpass_core_hw_vote=&core;p.lpass_audio_hw_vote=&audio;fault=1;assert(__lpass_cdc_reg_read(&p,0,0,&val)==-EIO&&!pmputs&&!pmrefs&&val==0);fault=steps+1;assert(__lpass_cdc_reg_write(&p,0,0,0xff)==-EIO&&mem[0]==0x123456ab&&!pmrefs);cases+=2;
fault=0;assert(__lpass_cdc_reg_read(&p,0,0,&val)==-ENOTSYNC&&!pmrefs&&val==0);p.core_hw_vote_count=1;assert(__lpass_cdc_reg_write(&p,0,0,0xff)==-ENOTSYNC&&mem[0]==0x123456ab&&!pmrefs);p.core_audio_vote_count=1;assert(!__lpass_cdc_reg_read(&p,0,0,&val)&&val==0xab&&!pmrefs);assert(!__lpass_cdc_reg_write(&p,0,4,0x25)&&mem[1]==0x25&&!pmrefs);p.dev_up=false;int before=gets;assert(__lpass_cdc_reg_read(&p,0,0,&val)<0&&gets==before);cases+=5;printf("{\"lowio\":%d}\n",cases);}
