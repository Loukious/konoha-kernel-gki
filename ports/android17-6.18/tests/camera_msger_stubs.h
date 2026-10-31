static int sched_enable_error;
int cam_sched_init(void){return failure()?-ENOMEM:0;}
int cam_sched_enable(void){return sched_enable_error;}
int cam_sched_w_pids(struct file *fp,char *s,int n,u8 *hit){*hit=0;return -1;}
void cam_sched_ioctl(struct file *f,unsigned c,unsigned long a,u8 *hit){*hit=0;}
int get_tgid_by_cmdline(struct file *f,char *s,int n,u8 *hit){*hit=!strncmp(s,"ctp|",4);return *hit?99:-1;}
