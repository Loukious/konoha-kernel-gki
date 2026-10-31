#define __init
#define __exit
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static int rng_driver,fail_driver,module_registered,module_crypto;
static struct platform_device module_pdev;
static int platform_driver_register(int *driver) { if(fail_driver)return -EIO;module_pdev=new_pdev();assert(!msm_rng_probe(&module_pdev));struct msm_rng_device *r=platform_get_drvdata(&module_pdev);struct inode inode;struct file file={0};inode_get(&inode,&r->cdev);assert(msm_rng_open(&inode,&file)==-ENODEV);inode_put(&inode);module_registered=1;return 0; }
static void platform_driver_unregister(int *driver) { assert(module_registered);module_registered=0;msm_rng_remove(&module_pdev); }
static int crypto_register_rngs(struct rng_alg *algs,int count) { if(fail_crypto)return -EIO;module_crypto=1;return 0; }
static void crypto_unregister_rngs(struct rng_alg *algs,int count) { assert(module_crypto);module_crypto=0; }
/* PRODUCTION MODULE FUNCTIONS */
static void module_cases(void) {
 msm_rng_ready=false;fail_driver=1;assert(msm_rng_init()==-EIO);fail_driver=0;clean();
 fail_crypto=1;assert(msm_rng_init()==-EIO);fail_crypto=0;assert(!module_registered);clean();
 assert(!msm_rng_init());assert(module_registered&&module_crypto&&msm_rng_ready);msm_rng_exit();assert(!msm_rng_ready);assert(!module_registered&&!module_crypto);clean();
}
