#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define U8_MAX 255
#define MAX_NL_MSG_LEN 16
#define NLMSG_HDRLEN 16
#define GFP_ATOMIC 0
#define MSG_DONTWAIT 0
#define READ_ONCE(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
#define rcu_access_pointer(p) READ_ONCE(p)
#define srcu_dereference(p,l) READ_ONCE(p)
#define rcu_assign_pointer(p,v) WRITE_ONCE(p,v)
#define rcu_replace_pointer(p,v,l) __atomic_exchange_n(&(p),(v),__ATOMIC_ACQ_REL)
#define lockdep_is_held(p) 1
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
struct sock { unsigned generation; };
struct nlmsghdr { u32 nlmsg_len,nlmsg_type,nlmsg_seq,nlmsg_pid; };
struct sk_buff { size_t len,capacity; unsigned char *data; struct sock *sk; struct { u32 portid,dst_group; } cb; };
struct fp_device { unsigned fp_netlink_num; };
struct netlink_kernel_cfg { void (*input)(struct sk_buff *); };
static int init_net;
static struct sock *nl_sk;
static u32 fp_nl_portid;
static pthread_mutex_t fp_nl_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t fp_nl_srcu=PTHREAD_RWLOCK_INITIALIZER;
static atomic_int entered,allow_exit,draining,destroyed;
static bool block_send, fail_alloc,fail_put,fail_create;
static unsigned generations,live_sockets,live_skb,sends;
static int srcu_read_lock(pthread_rwlock_t *p) { pthread_rwlock_rdlock(p);return 0; }
static void srcu_read_unlock(pthread_rwlock_t *p,int i) { (void)i;pthread_rwlock_unlock(p); }
static void synchronize_srcu(pthread_rwlock_t *p) { atomic_store(&draining,1);pthread_rwlock_wrlock(p);pthread_rwlock_unlock(p); }
static struct sk_buff *nlmsg_new(size_t n,int flags) { (void)flags;if(fail_alloc)return NULL;struct sk_buff *s=calloc(1,sizeof(*s));assert(s);s->capacity=n+NLMSG_HDRLEN;s->data=malloc(s->capacity);assert(s->data);memset(s->data,0xa5,s->capacity);live_skb++;return s; }
static struct nlmsghdr *nlmsg_put(struct sk_buff *s,int a,int b,int c,size_t n,int f) { (void)a;(void)b;(void)c;(void)f;if(fail_put || s->capacity<n+NLMSG_HDRLEN)return NULL;s->len=n+NLMSG_HDRLEN;struct nlmsghdr *h=(void *)s->data;memset(h,0,sizeof(*h));h->nlmsg_len=s->len;return h; }
static void *nlmsg_data(struct nlmsghdr *h) { return (unsigned char *)h+NLMSG_HDRLEN; }
static struct nlmsghdr *nlmsg_hdr(struct sk_buff *s) { return (void *)s->data; }
static bool nlmsg_ok(struct nlmsghdr *h,size_t len) { return h->nlmsg_len>=NLMSG_HDRLEN && h->nlmsg_len<=len; }
#define NETLINK_CB(s) ((s)->cb)
static void kfree_skb(struct sk_buff *s) { live_skb--;free(s->data);free(s); }
static int expected_command;
static int netlink_unicast(struct sock *sk,struct sk_buff *s,u32 portid,int flags) {
 (void)flags;assert(sk && portid==51);assert(s->len==32 && s->cb.portid==0 && s->cb.dst_group==0);
 unsigned char *data=nlmsg_data(nlmsg_hdr(s));assert(data[0]==expected_command);for(int i=1;i<16;i++)assert(data[i]==0);
 if(block_send) { atomic_store(&entered,1);while(!atomic_load(&allow_exit))usleep(100);assert(sk->generation); }
 sends++;kfree_skb(s);return 0;
}
static struct sock *netlink_kernel_create(int *net,int n,struct netlink_kernel_cfg *cfg) { (void)net;assert(n==25 && cfg->input);if(fail_create)return NULL;struct sock *p=malloc(sizeof(*p));assert(p);p->generation=++generations;live_sockets++;return p; }
static void netlink_kernel_release(struct sock *sk) { assert(sk);live_sockets--;free(sk); }
/* FUNCTIONS */
static struct fp_device fp={.fp_netlink_num=25};
static void *sender(void *unused) { (void)unused;fp_netlink_send(&fp,7);return NULL; }
static void *destroyer(void *unused) { (void)unused;fp_netlink_destroy(&fp);atomic_store(&destroyed,1);return NULL; }
int main(void) {
 fail_create=true;assert(fp_netlink_init(&fp)==-ENOMEM);fail_create=false;assert(!fp_netlink_init(&fp));assert(!fp_netlink_init(&fp) && live_sockets==1);
 struct sk_buff *s=nlmsg_new(16,0);struct nlmsghdr *h=nlmsg_put(s,0,0,0,16,0);s->sk=nl_sk;s->cb.portid=51;h->nlmsg_pid=999;
 for(unsigned size=0;size<64;size++) for(unsigned hdr=0;hdr<64;hdr++) { s->len=size;h->nlmsg_len=hdr;fp_nl_portid=0;fp_netlink_recv(s);assert((fp_nl_portid==51)==(size>=16 && hdr>=16 && hdr<=size)); }
 s->len=32;h->nlmsg_len=32;fp_netlink_recv(s);assert(fp_nl_portid==51);
 for(int i=0;i<=255;i++) { expected_command=i;fp_netlink_send(&fp,i); } assert(sends==256);
 fp_netlink_send(&fp,-1);fp_netlink_send(&fp,256);assert(sends==256);
 fail_alloc=true;fp_netlink_send(&fp,7);fail_alloc=false;fail_put=true;fp_netlink_send(&fp,7);fail_put=false;assert(sends==256);
 expected_command=7;block_send=true;pthread_t a,b;pthread_create(&a,NULL,sender,NULL);while(!atomic_load(&entered))usleep(100);
 pthread_create(&b,NULL,destroyer,NULL);while(!atomic_load(&draining))usleep(100);assert(!atomic_load(&destroyed));atomic_store(&allow_exit,1);pthread_join(a,NULL);pthread_join(b,NULL);assert(!live_sockets);
 assert(!fp_netlink_init(&fp));fp_netlink_recv(s);assert(fp_nl_portid==0);s->sk=nl_sk;fp_netlink_recv(s);assert(fp_nl_portid==51);
 kfree_skb(s);assert(!fp_netlink_destroy(&fp));assert(!fp_netlink_destroy(&fp));assert(!live_sockets && !live_skb);
 puts("{\"header_bounds_cases\":4096,\"payload_commands\":256,\"authenticated_portid\":true,\"stale_socket_rejected\":true,\"destroy_waits_for_sender\":true}");
}
