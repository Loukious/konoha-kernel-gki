/* SERVICES */
/* CONSTANTS */
const u16 macro_id_base_offset[MAX_MACRO]={TX_START_OFFSET,RX_START_OFFSET,WSA_START_OFFSET,VA_START_OFFSET,WSA2_START_OFFSET};
/* SOURCE */
static int calls,fail_io;static u16 last_offset;static u8 last_value;
static int rd(struct lpass_cdc_priv*p,u16 m,u16 r,u8*v){calls++;last_offset=r;if(calls==fail_io)return -EIO;*v=0x39;return 0;}
static int wr(struct lpass_cdc_priv*p,u16 m,u16 r,u8 v){last_value=v;return rd(p,m,r,&v);}
int main(void){struct device d={0};struct lpass_cdc_priv p;init_priv(&p,&d);p.read_dev=rd;p.write_dev=wr;u8 out[8]={0},encoded[4];u16 addr=TX_START_OFFSET;memcpy(encoded+1,&addr,2);int cases=0;
assert(regmap_bus_read(&d,encoded+1,2,out,1)==-ENODEV);p.macros_supported[0]=true;p.macro_params[0].io_base=(void*)1;assert(!regmap_bus_read(&d,encoded+1,2,out,1)&&out[0]==0x39);cases+=2;
for(int i=1;i<=3;i++){addr=TX_START_OFFSET+i;memcpy(encoded+1,&addr,2);assert(regmap_bus_read(&d,encoded+1,2,out,1)==-EINVAL);cases++;}
addr=TX_MAX_OFFSET;memcpy(encoded+1,&addr,2);assert(regmap_bus_read(&d,encoded+1,2,out,2)==-EINVAL);assert(regmap_bus_gather_write(&d,encoded+1,2,out,2)==-EINVAL);cases+=2;
addr=TX_START_OFFSET;memcpy(encoded+1,&addr,2);calls=0;fail_io=2;assert(regmap_bus_read(&d,encoded+1,2,out,3)==-EIO&&calls==2);calls=0;assert(regmap_bus_gather_write(&d,encoded+1,2,out,3)==-EIO&&calls==2);cases+=2;
fail_io=0;assert(regmap_bus_read(&d,encoded+1,2,out,SIZE_MAX)==-EINVAL);assert(regmap_bus_read(&d,encoded+1,1,out,1)==-EINVAL);assert(regmap_bus_read(&d,encoded+1,2,out,0)==-EINVAL);lpass_cdc_quiesce(&p);assert(regmap_bus_read(&d,encoded+1,2,out,1)==-ENODEV);lpass_cdc_reopen(&p);encoded[2]=0x44;memcpy(encoded,&addr,2);assert(!regmap_bus_write(&d,encoded,3)&&last_value==0x44);assert(!atomic_read(&p.active_ops));cases+=5;
printf("{\"io\":%d}\n",cases);}
