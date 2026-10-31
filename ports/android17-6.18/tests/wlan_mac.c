#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_NO_OF_MAC_ADDR 4
#define MAC_PREFIX_LEN 2
#define ETH_ALEN 6
#define U16_MAX 65535
#define __user
#define pr_err(...) do {} while(0)
#define pr_debug(...) do {} while(0)
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
#define ether_addr_copy(d,s) memcpy(d,s,6)
typedef uint8_t u8;
typedef uint8_t __u8;
typedef uint32_t u32;
struct device {int unused;};
struct file {void *private_data;};
struct cnss_wlan_mac_addr {u8 mac_addr[4][6];u32 no_of_mac_addr_set;};
struct cnss_utils_priv {struct cnss_wlan_mac_addr wlan_mac_addr,wlan_der_mac_addr;pthread_mutex_t mac_lock,mailbox_lock;int smem_mailbox_id;bool smem_mailbox_initialized;};
static struct cnss_utils_priv object,*cnss_utils_priv=&object;
enum mac_type {CNSS_MAC_PROVISIONED,CNSS_MAC_DERIVED};
static int copy_fault,mail_start,mail_calls,mail_ret=41;
static unsigned copy_from_user(void *d,const void *s,size_t n){if(copy_fault)return 1;memcpy(d,s,n);return 0;}
static int kstrtou8(const char *s,int base,u8 *v){char *end;unsigned long value=strtoul(s,&end,base);if(!*s||*end||value>255)return -EINVAL;*v=value;return 0;}
static int smem_mailbox_start(int id,void *cb){assert(!id&&!cb);return mail_start;}
static int smem_mailbox_write(int id,int flags,u8 *data,uint16_t len){assert(!id&&flags==1&&data&&len==5);mail_calls++;return mail_ret;}
/* FUNCTIONS */
static void clear(void){memset(&object.wlan_mac_addr,0,sizeof(object.wlan_mac_addr));memset(&object.wlan_der_mac_addr,0,sizeof(object.wlan_der_mac_addr));}
int main(void){
 pthread_mutex_init(&object.mac_lock,NULL);pthread_mutex_init(&object.mailbox_lock,NULL);u32 count=73;assert(!get_wlan_mac_address(NULL,NULL,CNSS_MAC_PROVISIONED));cnss_utils_priv=NULL;assert(!get_wlan_mac_address(NULL,&count,CNSS_MAC_PROVISIONED)&&!count);cnss_utils_priv=&object;
 assert(set_wlan_mac_address(NULL,6,CNSS_MAC_PROVISIONED)==-EINVAL);u8 raw[30]={0};assert(set_wlan_mac_address(raw,0,CNSS_MAC_PROVISIONED)==-EINVAL);assert(set_wlan_mac_address(raw,7,CNSS_MAC_PROVISIONED)==-EINVAL);assert(set_wlan_mac_address(raw,30,CNSS_MAC_PROVISIONED)==-EINVAL);assert(set_wlan_mac_address(raw,6,8)==-EINVAL);
 int successes=0,invalid=0;
 for(int type=0;type<2;type++)for(int n=1;n<=4;n++)for(int seed=0;seed<256;seed++){
  clear();char text[128],*p=text;const char *name=type?"derived":"provisioned";p+=sprintf(p,"%s\n0x",name);
  u8 bytes[24];for(int j=0;j<n*6;j++){bytes[j]=(seed+j)&255;p+=sprintf(p,"%02x",bytes[j]);}*p++='\n';size_t len=p-text;
  assert(cnss_utils_mac_write(NULL,text,len,NULL)==(long)len);u8 *got=get_wlan_mac_address(NULL,&count,type);assert(count==(unsigned)n&&got&&!memcmp(got,bytes,n*6));
  u8 other[24];memset(other,0xff,sizeof(other));assert(!set_wlan_mac_address(other,n*6,type));assert(!memcmp(got,bytes,n*6));successes++;
 }
 clear();char command[128]="provisioned\n0x000102030405\n";size_t len=strlen(command);copy_fault=1;assert(cnss_utils_mac_write(NULL,command,len,NULL)==-EFAULT);copy_fault=0;assert(!object.wlan_mac_addr.no_of_mac_addr_set);
 const char *bad[]={"","provisioned","provisioned\n","provisioned\n0x","provisioned\n0x0001020304","provisioned\n0x00010203040506","provisioned\n0z000102030405","other\n0x000102030405","provisioned\n0x00010203040g","derived\n0x00010203040500010203040500010203040500010203040500"};
 for(unsigned j=0;j<sizeof(bad)/sizeof(bad[0]);j++){clear();assert(cnss_utils_mac_write(NULL,bad[j],strlen(bad[j]),NULL)==-EINVAL);assert(!object.wlan_mac_addr.no_of_mac_addr_set&&!object.wlan_der_mac_addr.no_of_mac_addr_set);invalid++;}
 for(int n=128;n<=8192;n++){assert(cnss_utils_mac_write(NULL,command,n,NULL)==-EINVAL);invalid++;}
 char buffer[5]={0};mail_start=-EAGAIN;assert(cnss_utils_smem_mailbox_write(NULL,1,(u8 *)buffer,5)==-EAGAIN&&!mail_calls);mail_start=1;assert(cnss_utils_smem_mailbox_write(NULL,1,(u8 *)buffer,5)==41&&object.smem_mailbox_initialized&&mail_calls==1);assert(cnss_utils_smem_mailbox_write(NULL,1,(u8 *)buffer,65536)==-EMSGSIZE&&mail_calls==1);assert(cnss_utils_smem_mailbox_write(NULL,1,NULL,5)==-EMSGSIZE&&mail_calls==1);
 for(int i=0;i<3;i++){int errors[]={-ENODEV,-EAGAIN,-EPERM};mail_ret=errors[i];assert(cnss_utils_smem_mailbox_write(NULL,1,(u8 *)buffer,5)==errors[i]&&!object.smem_mailbox_initialized);mail_ret=41;assert(cnss_utils_smem_mailbox_write(NULL,1,(u8 *)buffer,5)==41&&object.smem_mailbox_initialized);}
 pthread_mutex_destroy(&object.mac_lock);pthread_mutex_destroy(&object.mailbox_lock);
 printf("{\"valid_mac_lists\":%d,\"malformed_and_copy_bounds\":%d,\"failed_parse_preserves_state\":true,\"immutable_publication\":true,\"mailbox_length_and_error_propagation\":true}\n",successes,invalid);
}
