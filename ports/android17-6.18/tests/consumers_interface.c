static pthread_mutex_t callback_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_cond=PTHREAD_COND_INITIALIZER;
static bool callback_entered,callback_release,unregistered;
static int callback_data;
static int set_enable(const char *user,unsigned int value,void *data) {
 assert(!strcmp(user,"tester") && value==1 && data==&callback_data);
 pthread_mutex_lock(&callback_lock);callback_entered=true;pthread_cond_broadcast(&callback_cond);
 while(!callback_release)pthread_cond_wait(&callback_cond,&callback_lock);pthread_mutex_unlock(&callback_lock);return 0;
}
static void *call_provider(void *unused) {(void)unused;assert(!mca_charge_if_op_set("tester",MCA_CHARGE_IF_CHG_TYPE_QC,MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE,"1"));return NULL;}
static void *withdraw_provider(void *unused) {(void)unused;mca_charge_if_ops_unregister("quick",&callback_data);WRITE_ONCE(unregistered,true);return NULL;}
static void bounded_join(pthread_t t) {struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);ts.tv_sec+=2;assert(!pthread_timedjoin_np(t,NULL,&ts));}
int main(void) {
 struct mca_charge_if_ops ops={.type_name="quick",.data=&callback_data,.set_charge_enable=set_enable};
 assert(mca_charge_if_ops_register(NULL)==-EINVAL);assert(mca_charge_if_ops_register(&(struct mca_charge_if_ops){.type_name="all",.data=&callback_data})==-EINVAL);
 fail_at=1;assert(mca_charge_if_ops_register(&ops)==-ENOMEM && allocations==0);fail_at=0;
 assert(!mca_charge_if_ops_register(&ops));assert(allocations==1);
 ops.data=NULL;ops.set_charge_enable=NULL;
 assert(g_mca_charge_if_ops[MCA_CHARGE_IF_CHG_TYPE_QC]->data==&callback_data);
 ops.data=&callback_data;ops.set_charge_enable=set_enable;assert(mca_charge_if_ops_register(&ops)==-EBUSY && allocations==1);
 mca_charge_if_ops_unregister("quick",(void *)1);assert(allocations==1);
 assert(mca_charge_if_op_set("tester",MCA_CHARGE_IF_CHG_TYPE_QC,MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE,"invalid")==-EINVAL);
 assert(mca_charge_if_op_set("tester",MCA_CHARGE_IF_CHG_TYPE_ALL,MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE,"1")==-ENODEV);
 pthread_t caller,remover;assert(!pthread_create(&caller,NULL,call_provider,NULL));
 pthread_mutex_lock(&callback_lock);while(!callback_entered)pthread_cond_wait(&callback_cond,&callback_lock);pthread_mutex_unlock(&callback_lock);
 assert(!pthread_create(&remover,NULL,withdraw_provider,NULL));
 while(rcu_access_pointer(g_mca_charge_if_ops[MCA_CHARGE_IF_CHG_TYPE_QC]))sched_yield();assert(!READ_ONCE(unregistered));
 pthread_mutex_lock(&callback_lock);callback_release=true;pthread_cond_broadcast(&callback_cond);pthread_mutex_unlock(&callback_lock);
 bounded_join(caller);bounded_join(remover);assert(allocations==0 && READ_ONCE(unregistered));
 assert(mca_charge_if_op_set("tester",MCA_CHARGE_IF_CHG_TYPE_QC,MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE,"1")==-ENODEV);
 puts("{\"immutable_registration\":true,\"duplicate_owner\":true,\"allocation_failure\":true,\"callback_withdrawal_drain\":true,\"missing_provider\":true,\"numeric_parse_error\":true}");
}
