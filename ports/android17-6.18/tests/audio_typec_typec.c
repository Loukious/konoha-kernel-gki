#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <string.h>
#define GFP_KERNEL 0
#define __init
#define __exit
#define NOTIFY_DONE 0
#define IS_ENABLED(x) 1
#define SND_JACK_HEADSET 3
#define SND_JACK_VIDEOOUT 16
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex*m){pthread_mutex_init(&m->lock,NULL);}
static void mutex_lock(struct mutex*m){pthread_mutex_lock(&m->lock);}
static void mutex_unlock(struct mutex*m){pthread_mutex_unlock(&m->lock);}
static int live,module_refs;static bool alloc_fail,module_available=true;
static void *kzalloc(size_t n,int g){(void)g;if(alloc_fail)return NULL;void*p=calloc(1,n);if(p)live++;return p;}
static void kfree(void*p){assert(p&&live>0);live--;free(p);}
#define THIS_MODULE ((void*)1)
static bool try_module_get(void*p){assert(p==THIS_MODULE);if(!module_available)return false;module_refs++;return true;}
static void module_put(void*p){assert(p==THIS_MODULE&&module_refs>0);module_refs--;}
struct snd_card {int unused;};struct snd_soc_card {struct snd_card*snd_card;};struct snd_soc_component {struct snd_soc_card*card;};
struct snd_soc_jack {struct snd_soc_card*card;void*jack;int status;};
struct notifier_block {int(*notifier_call)(struct notifier_block*,unsigned long,void*);int priority;};
enum typec_accessory {TYPEC_ACCESSORY_NONE,TYPEC_ACCESSORY_AUDIO,TYPEC_ACCESSORY_DEBUG};
/* UCSI_HEADER */
static pthread_rwlock_t notifier_lock=PTHREAD_RWLOCK_INITIALIZER;
static struct notifier_block*registered;
static int jack_error,notifier_error,jacks,reports;static bool immediate_event;
static atomic_bool block_report,report_entered,report_release,exit_done;
static int snd_soc_card_jack_new(struct snd_soc_card*c,const char*name,int mask,struct snd_soc_jack*j){assert(c&&c->snd_card&&!strcmp(name,"Typec_analog Jack")&&mask==19);if(jack_error)return jack_error;j->card=c;j->jack=j;jacks++;return 0;}
static void snd_device_free(struct snd_card*c,void*j){assert(c&&j&&!registered&&jacks>0);jacks--;}
static void snd_soc_jack_report(struct snd_soc_jack*j,int status,int mask){assert(j&&j->jack&&j->card&&mask==19&&(status==0||status==19));if(block_report){report_entered=true;while(!report_release)usleep(100);}j->status=status;reports++;}
int register_ucsi_glink_notifier(struct notifier_block*nb){if(notifier_error)return notifier_error;assert(!registered);registered=nb;if(immediate_event){struct ucsi_glink_constat_info info={.acc=TYPEC_ACCESSORY_AUDIO};assert(!nb->notifier_call(nb,0,&info));}return 0;}
int unregister_ucsi_glink_notifier(struct notifier_block*nb){pthread_rwlock_wrlock(&notifier_lock);assert(nb==registered);registered=NULL;pthread_rwlock_unlock(&notifier_lock);return 0;}
/* SOURCE */
static struct snd_card sound_card;static struct snd_soc_card card={.snd_card=&sound_card};static struct snd_soc_component component={.card=&card},other={.card=&card};
static void clean(void){assert(!registered&&!acc_priv&&!live&&!module_refs&&!jacks);}
static void event(int value){struct ucsi_glink_constat_info info={.acc=value};pthread_rwlock_rdlock(&notifier_lock);assert(registered);assert(!registered->notifier_call(registered,0,&info));pthread_rwlock_unlock(&notifier_lock);}
static unsigned failures(void){assert(typec_analog_acc_init(NULL)==-EINVAL);module_available=false;assert(typec_analog_acc_init(&component)==-ENODEV);clean();module_available=true;alloc_fail=true;assert(typec_analog_acc_init(&component)==-ENOMEM);clean();alloc_fail=false;jack_error=-EIO;assert(typec_analog_acc_init(&component)==-EIO);clean();jack_error=0;notifier_error=-ENODEV;assert(typec_analog_acc_init(&component)==-ENODEV);clean();return 5;}
static unsigned lifecycle(void){immediate_event=true;assert(!typec_analog_acc_init(&component)&&module_refs==1&&reports==1&&acc_priv->typec_analog_jack.status==19);assert(typec_analog_acc_init(&component)==-EBUSY&&module_refs==1&&jacks==1);assert(typec_analog_acc_init(&other)==-EBUSY);typec_analog_acc_exit(&other);assert(registered);typec_analog_acc_exit(&component);clean();typec_analog_acc_exit(&component);assert(!typec_analog_acc_init(&other));typec_analog_acc_exit(&other);clean();return 6;}
static unsigned events(void){assert(!typec_analog_acc_init(&component));assert(!registered->notifier_call(registered,0,NULL));event(TYPEC_ACCESSORY_NONE);assert(!reports);event(TYPEC_ACCESSORY_AUDIO);event(TYPEC_ACCESSORY_AUDIO);assert(reports==1&&acc_priv->typec_analog_jack.status==19);event(TYPEC_ACCESSORY_DEBUG);assert(reports==1&&acc_priv->typec_analog_jack.status==19);event(TYPEC_ACCESSORY_NONE);event(TYPEC_ACCESSORY_NONE);assert(reports==2&&!acc_priv->typec_analog_jack.status);typec_analog_acc_exit(&component);clean();return 5;}
static void *event_thread(void*p){(void)p;event(TYPEC_ACCESSORY_AUDIO);return NULL;}
static void *exit_thread(void*p){(void)p;typec_analog_acc_exit(&component);exit_done=true;return NULL;}
static unsigned drain(void){assert(!typec_analog_acc_init(&component));block_report=true;pthread_t a,b;pthread_create(&a,NULL,event_thread,NULL);while(!report_entered)usleep(100);pthread_create(&b,NULL,exit_thread,NULL);usleep(2000);assert(!exit_done&&module_refs==1&&jacks==1);report_release=true;pthread_join(a,NULL);pthread_join(b,NULL);assert(exit_done&&reports==1);clean();return 3;}
static unsigned duplicate(void){assert(!typec_analog_acc_init(&component));block_report=true;pthread_t a,b;pthread_create(&a,NULL,event_thread,NULL);while(!report_entered)usleep(100);pthread_create(&b,NULL,event_thread,NULL);usleep(2000);report_release=true;pthread_join(a,NULL);pthread_join(b,NULL);assert(reports==1);typec_analog_acc_exit(&component);clean();return 2;}
int main(int argc,char**argv){assert(argc==2);unsigned n;if(!strcmp(argv[1],"failures"))n=failures();else if(!strcmp(argv[1],"lifecycle"))n=lifecycle();else if(!strcmp(argv[1],"events"))n=events();else if(!strcmp(argv[1],"drain"))n=drain();else if(!strcmp(argv[1],"duplicate"))n=duplicate();else abort();printf("{\"%s\":%u}\n",argv[1],n);}
