#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
typedef uint32_t u32;
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define CONFIG_SYSFS 1
#define ARRAY_SIZE(v) (sizeof(v)/sizeof(v[0]))
#define mca_log_err(...) ((void)0)
#define mca_log_info(...) ((void)0)
struct mutex {pthread_mutex_t value;};
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->value,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->value));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->value));}
static struct mutex *take_guard(struct mutex *m){mutex_lock(m);return m;}
static void drop_guard(struct mutex **m){mutex_unlock(*m);}
#define guard(kind) struct mutex *__attribute__((cleanup(drop_guard))) held_guard=take_guard
struct device {void *data;};struct platform_device {struct device dev;};
struct attribute {const char *name;};struct device_attribute {struct attribute attr;};
struct mca_sysfs_attr_info {struct device_attribute attr;int sysfs_attr_name;};
struct attribute_group {struct attribute **attrs;};
#define mca_sysfs_attr_ro(prefix,mode,id,name) {{{#name}},id}
#define mca_sysfs_attr_rw(prefix,mode,id,name) mca_sysfs_attr_ro(prefix,mode,id,name)
/* HEADER */
/* CONSTANTS */
/* TABLE */
/* DECLARATIONS */
static int sequence,fail_at,group,provider_error,provider_value,port_value,vdm_cmd=USBPD_UVDM_AUTHENTICATION,vdm_requests,notifications,last_notify,last_port,last_cmd,last_size;
static bool bad_state,stop_hook;static struct usbpd_vdm_data vdm;
static unsigned char received[40];static struct pd_auth_strategy *allocation;
static pthread_barrier_t reached_provider,release_provider,reached_remove;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;allocation=calloc(1,n);assert(allocation);return allocation;}
static void platform_set_drvdata(struct platform_device *d,void *p){d->dev.data=p;}
static void *platform_get_drvdata(struct platform_device *d){return d->dev.data;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static int sysfs_emit(char *buf,const char *fmt,...){va_list ap;va_start(ap,fmt);int ret=vsnprintf(buf,PAGE_SIZE,fmt,ap);va_end(ap);return ret;}
static int sysfs_emit_at(char *buf,int at,const char *fmt,...){va_list ap;va_start(ap,fmt);int ret=vsnprintf(buf+at,PAGE_SIZE-at,fmt,ap);va_end(ap);return ret;}
static int sysfs_streq(const char *a,const char *b){size_t n=strlen(a);if(n&&a[n-1]=='\n')n--;return n==strlen(b)&&!strncmp(a,b,n);}
static int kstrtoint(const char *s,int base,int *out){char *end;errno=0;long v=strtol(s,&end,base);if(errno||end==s||(*end&&strcmp(end,"\n"))||v<INT_MIN||v>INT_MAX)return -EINVAL;*out=v;return 0;}
static char *strim(char *s){while(isspace(*s))s++;char *e=s+strlen(s);while(e>s&&isspace(e[-1]))*--e=0;return s;}
static void *kmemdup_nul(const char *s,size_t n,int flags){(void)flags;char *p=malloc(n+1);assert(p);memcpy(p,s,n);p[n]=0;return p;}
static void memzero_explicit(void *p,size_t n){memset(p,0,n);}
static void kfree_sensitive(void *p){free(p);}
static int digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int hex2bin(unsigned char *out,const char *s,size_t n){for(size_t i=0;i<n;i++){int hi=digit(s[2*i]),lo=digit(s[2*i+1]);if(hi<0||lo<0)return -EINVAL;out[i]=(hi<<4)|lo;}return 0;}
static void mca_sysfs_init_attrs(struct attribute **attrs,struct mca_sysfs_attr_info *table,int n){(void)attrs;(void)table;assert(n==10);}
static int mca_sysfs_create_link_group(const char *device,const char *name,struct device *d,const struct attribute_group *attrs){(void)attrs;assert(!strcmp(device,"typec")&&!strcmp(name,"strategy_pd_auth")&&d->data);if(fault())return -EIO;group++;return 0;}
static void mca_sysfs_remove_link_group(const char *device,const char *name,struct device *d,const struct attribute_group *attrs){(void)device;(void)name;(void)d;(void)attrs;if(stop_hook)pthread_barrier_wait(&reached_remove);assert(group==1);group--;}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,struct mca_sysfs_attr_info *table,int count){for(int n=0;n<count;n++)if(!strcmp(name,table[n].attr.attr.name))return table+n;return NULL;}
static int protocol_class_pd_get_port_num(void){return port_value;}
static int protocol_class_pd_get_vdm_cmd(int port,int *cmd,struct usbpd_vdm_data *out){last_port=port;if(provider_error)return provider_error;*cmd=vdm_cmd;*out=vdm;return 0;}
static int protocol_class_pd_get_current_state(int port,char *state,size_t n){last_port=port;if(stop_hook){pthread_barrier_wait(&reached_provider);pthread_barrier_wait(&release_provider);}if(provider_error)return provider_error;if(bad_state)memset(state,'x',n);else snprintf(state,n,"SNK_Ready");return 0;}
#define GET_UNSIGNED(name) static int protocol_class_pd_get_##name(int port,unsigned int *out){last_port=port;if(provider_error)return provider_error;*out=provider_value;return 0;}
GET_UNSIGNED(adapter_id) GET_UNSIGNED(adapter_svid)
#define GET_FLAG(name) static int protocol_class_pd_get_##name(int port,int *out){last_port=port;if(provider_error)return provider_error;*out=provider_value;return 0;}
GET_FLAG(verify_process) GET_FLAG(pd_verifed)
#define GET_ROLE(name) static int protocol_class_pd_get_##name(int port,unsigned char *out){last_port=port;if(provider_error)return provider_error;*out=provider_value;return 0;}
GET_ROLE(power_role) GET_ROLE(data_role)
static int protocol_class_pd_get_pdos(int port,struct pd_pdo *out,int n){last_port=port;assert(n==PROTOCOL_PD_MAX_PDO_NUMS);if(provider_error)return provider_error;out[1].max_volt=provider_value;return 0;}
static int protocol_class_pd_request_vdm_cmd(int port,int cmd,unsigned int *data,unsigned int n){assert(((uintptr_t)data%sizeof(unsigned int))==0&&n<=40);last_port=port;last_cmd=cmd;last_size=n;memcpy(received,data,n);vdm_requests++;return provider_error;}
static int protocol_class_pd_set_verify_process(int port,int value){last_port=port;assert(value==0||value==1);return provider_error;}
static int protocol_class_pd_set_pd_verifed(int port,int value){last_port=port;assert(value==0||value==1);return provider_error;}
static int platform_class_buckchg_ops_get_online(int role,int *out){(void)role;if(provider_error)return provider_error;*out=provider_value;return 0;}
static void mca_event_block_notify(int type,int event,void *data){(void)type;(void)data;notifications++;last_notify=event;}
static void mca_charge_mievent_report(int event,void *data,int n){(void)event;(void)data;(void)n;}
/* FUNCTIONS */
static struct device_attribute attr(const char *name){return(struct device_attribute){{name}};}
static void *read_state(void *arg){char buf[PAGE_SIZE];struct device_attribute state=attr("current_state");assert(strategy_pd_auth_sysfs_show(arg,&state,buf)>0);return NULL;}
static void *remove_dev(void *arg){strategy_pd_auth_remove(arg);return NULL;}
int main(void){
 alarm(25);struct platform_device dev={0};for(int fail=1;fail<=2;fail++){sequence=0;fail_at=fail;assert(strategy_pd_auth_probe(&dev)<0&&!dev.dev.data&&!group);free(allocation);allocation=NULL;}fail_at=0;sequence=0;assert(!strategy_pd_auth_probe(&dev));struct pd_auth_strategy *info=dev.dev.data;
 unsigned char out[42];unsigned int len;int hex_cases=0;for(int n=1;n<=100;n++){char text[101];memset(text,'a',n);text[n]=0;memset(out,0x55,sizeof(out));int ret=strategy_pd_auth_string2hex(text,out,&len);assert(ret==((n%2==0&&n<=80)?0:-EINVAL));if(!ret)assert(len==(unsigned)n/2&&out[len]==0x55);hex_cases++;}const char *bad[]={"0g","++","x0","Aa-1"};for(unsigned n=0;n<ARRAY_SIZE(bad);n++){assert(strategy_pd_auth_string2hex(bad[n],out,&len)==-EINVAL);hex_cases++;}
 assert(!strategy_pd_auth_string2hex("aAbB01",out,&len)&&len==3&&out[0]==0xaa&&out[1]==0xbb&&out[2]==1);hex_cases++;
 const char *invalid[]={"", "5", "5,", "-1,aa", "256,aa", "999999999999999999,aa", "5,aa", "5,0123g4", "1,aaa", "1,00,01"};int bad_commands=0;for(unsigned n=0;n<ARRAY_SIZE(invalid);n++){int before=vdm_requests;assert(strategy_pd_auth_set_vdm_cmd(invalid[n],strlen(invalid[n]),0)==-EINVAL&&vdm_requests==before);bad_commands++;}
 const char *auth="5,00112233445566778899aAbBcCdDeEfF\n";assert(!strategy_pd_auth_set_vdm_cmd(auth,strlen(auth),1)&&last_port==1&&last_size==16&&received[0]==0&&received[15]==0xff);provider_error=-EIO;assert(strategy_pd_auth_set_vdm_cmd(auth,strlen(auth),0)==-EIO);provider_error=0;
 char buf[PAGE_SIZE];int property_cases=0;provider_value=1;for(unsigned n=0;n<ARRAY_SIZE(strategy_pd_auth_sysfs_field_tbl);n++){struct device_attribute field=strategy_pd_auth_sysfs_field_tbl[n].attr;assert(strategy_pd_auth_sysfs_show(&dev.dev,&field,buf)>0);property_cases++;if(strategy_pd_auth_sysfs_field_tbl[n].sysfs_attr_name!=PD_AUTH_NAME){provider_error=-EAGAIN;assert(strategy_pd_auth_sysfs_show(&dev.dev,&field,buf)==-EAGAIN);provider_error=0;property_cases++;}}
 struct device_attribute state=attr("current_state"),verified=attr("verified"),verify=attr("verify_process"),role=attr("data_role");bad_state=true;assert(strategy_pd_auth_sysfs_show(&dev.dev,&state,buf)==-EOVERFLOW);bad_state=false;provider_value=255;struct device_attribute pr=attr("current_pr");assert(strategy_pd_auth_sysfs_show(&dev.dev,&pr,buf)>0&&!strcmp(buf,"none\n"));provider_value=1;
 provider_error=-EIO;int before=notifications;assert(strategy_pd_auth_sysfs_store(&dev.dev,&verified,"1",1)==-EIO&&notifications==before&&info->pd_verified_type==0);assert(strategy_pd_auth_sysfs_store(&dev.dev,&verify,"0",1)==-EIO&&info->verify_porcess_end==1);provider_error=0;assert(strategy_pd_auth_sysfs_store(&dev.dev,&verify,"0",1)==1&&notifications==before+1);assert(strategy_pd_auth_sysfs_store(&dev.dev,&verified,"1",1)==1&&info->pd_verified_type==XM_CHARGER_TYPE_PD_VERIFY);assert(strategy_pd_auth_sysfs_store(&dev.dev,&verified,"0",1)==1&&info->pd_verified_type==XM_CHARGER_TYPE_UNKNOW);
 assert(strategy_pd_auth_sysfs_store(&dev.dev,&role,"ufpjunk",7)==-EINVAL);port_value=1;assert(strategy_pd_auth_sysfs_store(&dev.dev,&role,"dfp\n",4)==4&&last_port==1&&last_cmd==USBPD_UVDM_REQUEST_PD_DR);assert(strategy_pd_auth_sysfs_store(&dev.dev,&verified,"2",1)==-EINVAL);port_value=-1;assert(strategy_pd_auth_sysfs_show(&dev.dev,&state,buf)==-ENODEV);port_value=0;
 stop_hook=true;pthread_barrier_init(&reached_provider,NULL,2);pthread_barrier_init(&release_provider,NULL,2);pthread_barrier_init(&reached_remove,NULL,2);pthread_t reader,removing;assert(!pthread_create(&reader,NULL,read_state,&dev.dev));pthread_barrier_wait(&reached_provider);assert(!pthread_create(&removing,NULL,remove_dev,&dev));pthread_barrier_wait(&release_provider);pthread_barrier_wait(&reached_remove);assert(!pthread_join(reader,NULL));assert(!pthread_join(removing,NULL));assert(info->stopping&&!group&&!dev.dev.data);strategy_pd_auth_shutdown(&dev);free(allocation);
 printf("{\"probe_failure_points\":2,\"hex_boundary_cases\":%d,\"invalid_vdm_commands\":%d,\"property_success_error_cases\":%d,\"aligned_payload_and_provider_errors\":true,\"state_change_after_success\":true,\"revoke_verified_state\":true,\"exact_role_and_active_port\":true,\"bounded_state_and_role_output\":true,\"active_sysfs_reader_removal\":true}\n",hex_cases,bad_commands,property_cases);
}
