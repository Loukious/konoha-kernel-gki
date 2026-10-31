/* SERVICES */
static int init_step,init_fail,pinctrl_live,irq_live,core_live;
static int msm_cdc_pinctrl_drv_init(void){if(++init_step==init_fail)return -EIO;pinctrl_live++;return 0;}
static int wcd9xxx_irq_drv_init(void){if(++init_step==init_fail)return -EIO;irq_live++;return 0;}
static int wcd9xxx_init(void){if(++init_step==init_fail)return -EIO;core_live++;return 0;}
static void msm_cdc_pinctrl_drv_exit(void){assert(pinctrl_live==1&&!irq_live&&!core_live);pinctrl_live--;}
static void wcd9xxx_irq_drv_exit(void){assert(irq_live==1&&!core_live);irq_live--;}
static void wcd9xxx_exit(void){assert(core_live==1);core_live--;}
/* SOURCE */
int main(int argc,char**argv){(void)argv;assert(argc==2);for(int i=1;i<=3;i++){init_step=0;init_fail=i;assert(wcd9xxx_core_init()==-EIO&&!pinctrl_live&&!irq_live&&!core_live);}init_fail=0;assert(!wcd9xxx_core_init()&&pinctrl_live&&irq_live&&core_live);wcd9xxx_core_exit();assert(!pinctrl_live&&!irq_live&&!core_live);puts("{\"core_init\":4}");}
