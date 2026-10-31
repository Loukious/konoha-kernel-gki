static struct platform_device new_pdev(void) { struct platform_device p={0};p.res.size=4096;p.dev.of_node=(void *)1;return p; }
static void buffers(void) {
 skip_config=1;msm_rng_ready=true;struct platform_device p=new_pdev();assert(!msm_rng_probe(&p));struct msm_rng_device *r=platform_get_drvdata(&p);u8 b[1050],want[4];memcpy(want,&word,4);
 for(unsigned len=0;len<=1025;++len)for(int off=0;off<8;++off){reset_io();memset(b,0xa5,sizeof(b));unsigned n=len/4*4;assert(msm_rng_read(&r->hwrng,b+8+off,len,true)==(int)n);for(unsigned i=0;i<n;++i)assert(b[8+off+i]==want[i%4]);for(int i=0;i<8+off;++i)assert(b[i]==0xa5);for(unsigned i=8+off+n;i<sizeof(b);++i)assert(b[i]==0xa5);assert(!r->prng_clk->enabled&&!r->icc_path->bw);if(len<4)assert(!reads&&!clock_enables&&!high_votes);}
 reset_io();clock_error=-EIO;assert(msm_rng_read(&r->hwrng,b,4,true)==-EIO);assert(!reads&&!clock_disables&&high_votes==1&&low_votes==1);reset_io();
 high_error=-EIO;assert(msm_rng_read(&r->hwrng,b,4,true)==-EIO);assert(!reads&&!clock_enables&&!low_votes);reset_io();
 low_error=-EIO;assert(msm_rng_read(&r->hwrng,b,4,true)==-EIO);assert(clock_disables==1);reset_io();r->icc_path->bw=0;
 default_status=0;assert(msm_rng_read(&r->hwrng,b,4,false)==0);assert(reads==1&&!delays);reset_io();
 default_status=0;assert(msm_rng_read(&r->hwrng,b,4,true)==0);assert(reads==6&&delays==5);reset_io();
 int sequence[]={0,1,0,1,0,0,0,0};memcpy(status_seq,sequence,sizeof(sequence));status_len=8;default_status=0;assert(msm_rng_read(&r->hwrng,b,16,true)==8);assert(delays==5);reset_io();
 word=0;assert(msm_rng_read(&r->hwrng,b,4,true)==4);reset_io();
 assert(qrng_get_random(NULL,NULL,0,b,3)==-EFAULT);assert(!qrng_get_random(NULL,NULL,0,b,4));assert(qrng_get_random(NULL,NULL,0,NULL,4)==-EINVAL);assert(qrng_get_random(NULL,NULL,0,b,UINT_MAX)==-EOVERFLOW);interrupted=1;assert(qrng_get_random(NULL,NULL,0,b,4)==-ERESTARTSYS);interrupted=0;
 struct inode inode;inode_get(&inode,&r->cdev);struct file file={0};assert(!msm_rng_open(&inode,&file));reset_io();assert(!msm_rng_ioctl(&file,QRNG_IOCTL_RESET_BUS_BANDWIDTH,0));assert(low_votes==1);assert(!msm_rng_ioctl(&file,0,0));
 msm_rng_suspend(&p.dev);reset_io();assert(msm_rng_read(&r->hwrng,b,4,true)==-EAGAIN);assert(qrng_get_random(NULL,NULL,0,b,4)==-EFAULT);assert(msm_rng_ioctl(&file,QRNG_IOCTL_RESET_BUS_BANDWIDTH,0)==-EAGAIN);assert(!reads&&!clock_enables&&!high_votes&&!low_votes);msm_rng_resume(&p.dev);assert(msm_rng_read(&r->hwrng,b,4,true)==4);
 msm_rng_remove(&p);assert(allocations==frees+1);assert(qrng_get_random(NULL,NULL,0,b,4)==-ENODEV);reset_io();assert(msm_rng_ioctl(&file,QRNG_IOCTL_RESET_BUS_BANDWIDTH,0)==-ENODEV);assert(!low_votes);struct file stale={0};assert(msm_rng_open(&inode,&stale)==-ENODEV);
 struct platform_device newer=new_pdev();assert(!msm_rng_probe(&newer));assert(msm_rng_open(&inode,&stale)==-ENODEV);assert(msm_rng_ioctl(&file,QRNG_IOCTL_RESET_BUS_BANDWIDTH,0)==-ENODEV);msm_rng_release_file(&inode,&file);inode_put(&inode);msm_rng_remove(&newer);clean();
}
static void lifecycle(void) {
 int *faults[]={&fail_alloc,&fail_res,&fail_map,&fail_clk,&fail_icc,&fail_hwrng,&fail_chrdev,&fail_class,&fail_name,&fail_cdev};
 for(unsigned i=0;i<sizeof(faults)/sizeof(*faults);++i){reset_io();*faults[i]=1;struct platform_device p=new_pdev();assert(msm_rng_probe(&p)<0);*faults[i]=0;clean();assert(!msm_rng_dev_cached);}
 skip_config=0;int *errors[]={&high_error,&clock_error,&low_error};
 for(unsigned i=0;i<3;++i){reset_io();*errors[i]=-EIO;struct platform_device p=new_pdev();assert(msm_rng_probe(&p)==-EIO);assert(!msm_rng_dev_cached);if(i==0)assert(!reads&&!clock_enables);if(i==1)assert(high_votes==1&&low_votes==1&&!clock_disables);clean();}
 reset_io();struct platform_device p=new_pdev();assert(!msm_rng_probe(&p));struct msm_rng_device *r=platform_get_drvdata(&p);assert(writes==2);assert(*(u32 *)(r->base+0x100)==0xdddd);assert(*(u32 *)(r->base+0x104)==2);struct platform_device duplicate=new_pdev();assert(msm_rng_probe(&duplicate)==-EBUSY);assert(msm_rng_dev_cached==r);msm_rng_remove(&p);clean();
 no_clock=1;reset_io();p=new_pdev();assert(!msm_rng_probe(&p));r=platform_get_drvdata(&p);u8 b[4];assert(msm_rng_read(&r->hwrng,b,4,true)==4);msm_rng_remove(&p);no_clock=0;skip_config=1;clean();
}
static struct msm_rng_device *race_rng;
static struct platform_device *race_pdev;
static atomic_int stopped;
static void *reader(void *unused) { u8 b[4];assert(msm_rng_read(&race_rng->hwrng,b,4,true)==4);return NULL; }
static void *remover(void *unused) { msm_rng_remove(race_pdev);atomic_store(&stopped,1);return NULL; }
static void races(void) {
 for(int i=0;i<100;++i){reset_io();struct platform_device p=new_pdev();assert(!msm_rng_probe(&p));race_rng=platform_get_drvdata(&p);race_pdev=&p;struct inode inode;inode_get(&inode,&race_rng->cdev);struct file file={0};assert(!msm_rng_open(&inode,&file));block_read=true;read_entered=read_continue=false;atomic_store(&stopped,0);pthread_t a,b;assert(!pthread_create(&a,NULL,reader,NULL));pthread_mutex_lock(&block_mutex);while(!read_entered)pthread_cond_wait(&block_cond,&block_mutex);pthread_mutex_unlock(&block_mutex);assert(!pthread_create(&b,NULL,remover,NULL));usleep(100);assert(!atomic_load(&stopped));pthread_mutex_lock(&block_mutex);read_continue=true;pthread_cond_broadcast(&block_cond);pthread_mutex_unlock(&block_mutex);pthread_join(a,NULL);pthread_join(b,NULL);block_read=false;assert(msm_rng_ioctl(&file,QRNG_IOCTL_RESET_BUS_BANDWIDTH,0)==-ENODEV);msm_rng_release_file(&inode,&file);inode_put(&inode);clean();}
}
int main(void) { buffers();lifecycle();races();puts("{\"buffer_cases\":8208,\"removal_races\":100,\"probe_faults\":13,\"stale_file_rebind\":true,\"suspend_clock_icc_balance\":true}"); }
