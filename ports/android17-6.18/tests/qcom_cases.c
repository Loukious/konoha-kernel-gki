static struct platform_device new_pdev(const void *match) {
 struct platform_device p={0};p.res.size=4096;p.dev.match=(void *)match;return p;
}
static void hold_tfm(struct qcom_rng *r,struct crypto_tfm *tfm,struct qcom_rng_ctx *ctx) {
 ++r->alg.base.refs;tfm->__crt_alg=&r->alg.base;tfm->ctx=ctx;assert(!qcom_rng_init(tfm));
}
static void drop_tfm(struct crypto_tfm *tfm) { struct crypto_alg *a=tfm->__crt_alg;if(!--a->refs)a->cra_destroy(a); }
static void buffers(void) {
 struct platform_device p=new_pdev(&qcom_trng_of_data);assert(!qcom_rng_probe(&p));struct qcom_rng *r=platform_get_drvdata(&p);
 struct crypto_tfm tfm;struct qcom_rng_ctx ctx;hold_tfm(r,&tfm,&ctx);struct crypto_rng cr={&ctx};u8 b[1050],want[4];memcpy(want,&word,4);
 for(unsigned len=0;len<=1025;++len)for(int off=0;off<8;++off){reset_io();memset(b,0xa5,sizeof(b));assert(qcom_hwrng_read(&r->hwrng,b+8+off,len,true)==(int)len);for(unsigned i=0;i<len;++i)assert(b[8+off+i]==want[i%4]);for(int i=0;i<8+off;++i)assert(b[i]==0xa5);for(unsigned i=8+off+len;i<sizeof(b);++i)assert(b[i]==0xa5);assert(!r->clk->enabled);if(!len)assert(!reads&&!clock_enables);}
 reset_io();assert(qcom_rng_read(r,b,0,true)==0);assert(!reads);assert(!qcom_rng_generate(&cr,NULL,0,b,0));assert(!clock_enables);
 assert(qcom_rng_generate(&cr,NULL,0,b,UINT_MAX)==-EOVERFLOW);assert(!reads);
 clock_error=-EIO;assert(qcom_hwrng_read(&r->hwrng,b,4,true)==-EIO);assert(!reads&&!clock_disables);reset_io();
 default_status=0;assert(qcom_hwrng_read(&r->hwrng,b,8,false)==0);assert(!delays&&reads==1);reset_io();
 status_seq[0]=1;status_seq[1]=0;status_len=2;default_status=0;assert(qcom_hwrng_read(&r->hwrng,b,8,false)==4);assert(!delays);reset_io();
 default_status=0;assert(qcom_rng_generate(&cr,NULL,0,b,8)==-ETIMEDOUT);assert(delays==51&&!r->clk->enabled);reset_io();
 word=0;assert(qcom_hwrng_read(&r->hwrng,b,4,true)==-EINVAL);assert(!r->clk->enabled);reset_io();
 qcom_rng_remove(&p);devres_release(&p);assert(allocations==frees+1);assert(qcom_rng_generate(&cr,NULL,0,b,4)==-ENODEV);assert(qcom_rng_init(&tfm)==-ENODEV);drop_tfm(&tfm);clean();
}
static void lifecycle(void) {
 int *faults[]={&fail_alloc,&fail_map,&fail_clk,&fail_hwrng,&fail_crypto};
 for(unsigned i=0;i<sizeof(faults)/sizeof(*faults);++i){reset_io();*faults[i]=1;struct platform_device p=new_pdev(&qcom_trng_of_data);assert(qcom_rng_probe(&p)<0);devres_release(&p);*faults[i]=0;clean();}
 fail_crypto=2;struct platform_device p=new_pdev(&qcom_trng_of_data);assert(qcom_rng_probe(&p)==-EEXIST);devres_release(&p);fail_crypto=0;clean();
 p=new_pdev(NULL);assert(qcom_rng_probe(&p)==-ENODEV);clean();
 reset_io();p=new_pdev(&qcom_prng_of_data);assert(!qcom_rng_probe(&p));struct qcom_rng *r=platform_get_drvdata(&p);struct crypto_tfm oldtfm;struct qcom_rng_ctx oldctx;hold_tfm(r,&oldtfm,&oldctx);assert(writes==2);assert(*(u32 *)(r->base+0x100)==0xdddd);assert(*(u32 *)(r->base+0x104)==2);assert(!qcom_rng_init(&oldtfm));assert(writes==2);
 qcom_rng_remove(&p);devres_release(&p);assert(allocations==frees+1);
 struct platform_device newer=new_pdev(&qcom_trng_of_data);assert(!qcom_rng_probe(&newer));struct crypto_rng cr={&oldctx};u8 b[4];reset_io();assert(qcom_rng_generate(&cr,NULL,0,b,4)==-ENODEV);assert(!reads);drop_tfm(&oldtfm);qcom_rng_remove(&newer);devres_release(&newer);clean();
 no_clock=1;p=new_pdev(&qcom_trng_of_data);assert(!qcom_rng_probe(&p));r=platform_get_drvdata(&p);assert(qcom_hwrng_read(&r->hwrng,b,4,true)==4);qcom_rng_remove(&p);devres_release(&p);no_clock=0;clean();
}
static struct qcom_rng *race_rng;
static struct platform_device *race_pdev;
static atomic_int stopped;
static void *reader(void *unused) { u8 b[4];assert(qcom_hwrng_read(&race_rng->hwrng,b,4,true)==4);return NULL; }
static void *remover(void *unused) { qcom_rng_remove(race_pdev);devres_release(race_pdev);atomic_store(&stopped,1);return NULL; }
static void races(void) {
 for(int i=0;i<100;++i){reset_io();struct platform_device p=new_pdev(i%2?&qcom_trng_of_data:&qcom_prng_of_data);assert(!qcom_rng_probe(&p));race_rng=platform_get_drvdata(&p);race_pdev=&p;struct crypto_tfm tfm;struct qcom_rng_ctx ctx;hold_tfm(race_rng,&tfm,&ctx);struct crypto_rng cr={&ctx};
  block_read=true;read_entered=read_continue=false;atomic_store(&stopped,0);pthread_t a,b;assert(!pthread_create(&a,NULL,reader,NULL));pthread_mutex_lock(&block_mutex);while(!read_entered)pthread_cond_wait(&block_cond,&block_mutex);pthread_mutex_unlock(&block_mutex);assert(!pthread_create(&b,NULL,remover,NULL));usleep(100);assert(!atomic_load(&stopped));pthread_mutex_lock(&block_mutex);read_continue=true;pthread_cond_broadcast(&block_cond);pthread_mutex_unlock(&block_mutex);pthread_join(a,NULL);pthread_join(b,NULL);block_read=false;u8 out[4];assert(qcom_rng_generate(&cr,NULL,0,out,4)==-ENODEV);drop_tfm(&tfm);clean();
 }
}
int main(void) { buffers();lifecycle();races();puts("{\"buffer_cases\":8208,\"removal_races\":100,\"probe_faults\":7,\"stale_transform_rebind\":true,\"clock_poll_balance\":true}"); }
