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
typedef uint8_t u8, __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
struct mutex { pthread_mutex_t raw; };
typedef struct { int unused; } wait_queue_head_t;
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->raw)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->raw)); }
static bool mutex_trylock(struct mutex *m) { return !pthread_mutex_trylock(&m->raw); }
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
#define __user
#define GFP_DMA 0
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define NCI_HDR_LEN 3
#define NCI_PAYLOAD_IDX 3
#define NCI_CMD_RSP_TIMEOUT_MS 2000
#define MAX_RETRY_COUNT 3
#define msecs_to_jiffies(x) (x)
#define ERESTARTSYS 512
static int wait_error;
static atomic_int waits, wakeups, allocated, payload_entered, release_payload;
#define wait_event_interruptible_timeout(q, condition, timeout) ({ \
    int result = 0; (void)(q); assert((timeout) == 2000); atomic_fetch_add(&waits, 1); \
    for (int tick = 0; tick < 100; tick++) { \
        if (condition) { result = 1; break; } \
        if (wait_error) { result = wait_error; break; } usleep(100); \
    } result; })
#define wake_up_interruptible(q) do { (void)(q); atomic_fetch_add(&wakeups, 1); } while (0)
static bool alloc_fail, copy_fail;
static void *kzalloc(size_t n, int gfp) { (void)gfp; if (alloc_fail) return NULL; atomic_fetch_add(&allocated, 1); return calloc(1, n); }
static void kfree(void *p) { if(p) { atomic_fetch_sub(&allocated, 1); free(p); } }
static int copy_from_user(void *d, const void *s, size_t n) { if (!s || copy_fail) return 1; memcpy(d,s,n); return 0; }
static int copy_struct_from_user(void *d, size_t dn, const void *s, size_t sn) {
    if (!s || copy_fail) return -EFAULT;
    memcpy(d,s,dn < sn ? dn : sn);
    for(size_t i=dn;i<sn;i++) if (((const u8 *)s)[i]) return -E2BIG;
    return 0;
}
#define u64_to_user_ptr(x) ((void *)(uintptr_t)(x))
/* TYPES */
struct nfc_dev {
    struct cold_reset cold_reset;
    struct mutex write_mutex, read_mutex;
    struct { void *client; } i2c_dev;
    bool removing;
    int (*nfc_write)(struct nfc_dev *, const char *, size_t, int);
    int (*nfc_disable_intr)(struct nfc_dev *);
};
static int state_error, write_result, available_result, header_result, payload_result, offs;
static u8 response[4];
static bool early_response, block_payload;
static int validate_nfc_state_nci(struct nfc_dev *d) { (void)d; return state_error; }
static int is_nfc_data_available_for_read(struct nfc_dev *d) { (void)d; return available_result; }
static int i2c_master_recv(void *client, void *buf, size_t n) {
    (void)client; assert(n==3 || n==1);
    int r = n==3 ? header_result : payload_result;
    if(n==1 && block_payload) {
        atomic_store(&payload_entered, 1);
        while(!atomic_load(&release_payload)) usleep(100);
    }
    if(r>0) { assert((size_t)r<=n); memcpy(buf, response+(n==1?3:0), r); }
    return r;
}
bool nfc_cold_reset_response(struct nfc_dev *, const u8 *, size_t);
static int write_mock(struct nfc_dev *d, const char *buf, size_t count, int retries) {
    assert(count==3 || count==4); assert(retries==3);
    assert((u8)buf[0]==0x2f && (buf[1]==0x1e || buf[1]==0x1f));
    assert(READ_ONCE(d->cold_reset.rsp_pending));
    response[1]=buf[1];
    if(early_response) {
        mutex_lock(&d->read_mutex);
        assert(nfc_cold_reset_response(d,response,4));
        mutex_unlock(&d->read_mutex);
    }
    return write_result==99 ? (int)count : write_result;
}
static int disable_mock(struct nfc_dev *d) { (void)d; offs++; return 0; }
/* FUNCTIONS */
static void reset(struct nfc_dev *d) {
    memset(d,0,sizeof(*d));
    assert(!pthread_mutex_init(&d->write_mutex.raw,NULL));
    assert(!pthread_mutex_init(&d->read_mutex.raw,NULL));
    assert(!pthread_mutex_init(&d->cold_reset.lock.raw,NULL));
    d->cold_reset.last_src_ese_prot=ESE_COLD_RESET_ORIGIN_NONE;
    d->nfc_write=write_mock; d->nfc_disable_intr=disable_mock;
    d->cold_reset.is_nfc_enabled=true;
    response[0]=0x4f; response[1]=0x1e; response[2]=1; response[3]=0;
    wait_error=state_error=0; write_result=99; available_result=1;
    header_result=3; payload_result=1; offs=0;
    early_response=true; block_payload=alloc_fail=copy_fail=false;
    atomic_store(&payload_entered,0); atomic_store(&release_payload,0);
    atomic_store(&waits,0); atomic_store(&wakeups,0);
    assert(!atomic_load(&allocated));
}
static void destroy(struct nfc_dev *d) {
    assert(!d->cold_reset.cmd_buf && !READ_ONCE(d->cold_reset.rsp_pending));
    assert(!atomic_load(&allocated));
    assert(!pthread_mutex_destroy(&d->write_mutex.raw));
    assert(!pthread_mutex_destroy(&d->read_mutex.raw));
    assert(!pthread_mutex_destroy(&d->cold_reset.lock.raw));
}
static int run(struct nfc_dev *d, struct ese_cold_reset_arg *a) {
    struct ese_ioctl_arg e={.buf=(uintptr_t)a,.buf_size=sizeof(*a),.type=0};
    return ese_cold_reset_ioctl(d,(unsigned long)&e);
}
struct job { struct nfc_dev *dev; struct ese_cold_reset_arg request; int result; atomic_int done; };
static void *ioctl_thread(void *p) { struct job *j=p; j->result=run(j->dev,&j->request); atomic_store(&j->done,1); return NULL; }
static void *response_thread(void *p) {
    struct nfc_dev *d=p;
    mutex_lock(&d->read_mutex);
    assert(nfc_cold_reset_response(d,response,3));
    mutex_unlock(&d->read_mutex);
    return NULL;
}
int main(void) {
    struct nfc_dev d; unsigned exchanges=0, faults=0;
    for(unsigned enabled=0;enabled<2;enabled++) for(unsigned cmd=0;cmd<3;cmd++) for(unsigned status=0;status<256;status++) {
        reset(&d); d.cold_reset.is_nfc_enabled=enabled; response[3]=status;
        struct ese_cold_reset_arg a={.src=1,.sub_cmd=cmd};
        assert(run(&d,&a)==(int)status);
        assert(d.cold_reset.is_crp_en==(cmd==1 && status==0));
        assert(d.cold_reset.last_src_ese_prot==(cmd==1 && status==0?1:255));
        assert(atomic_load(&wakeups)==1); exchanges++; destroy(&d);
    }
    for(int w=-EIO;w<4;w++) {
        reset(&d); write_result=w; early_response=false;
        struct ese_cold_reset_arg a={.sub_cmd=1};
        assert(run(&d,&a)==(w<0?w:-EIO)); assert(!d.cold_reset.is_crp_en);
        faults++; destroy(&d);
    }
    for(int h=-1;h<=3;h++) for(int p=-1;p<=1;p++) {
        reset(&d); d.cold_reset.is_nfc_enabled=false; early_response=false;
        header_result=h; payload_result=p; struct ese_cold_reset_arg a={0};
        int want=h!=3?(h<0?h:-EIO):(p!=1?(p<0?p:-EIO):0);
        assert(run(&d,&a)==want); faults++; destroy(&d);
    }
    for(unsigned payload=0;payload<256;payload++) {
        reset(&d); response[2]=payload; struct ese_cold_reset_arg a={.sub_cmd=1};
        assert(run(&d,&a)==(payload==1?0:-EPROTO)); faults++; destroy(&d);
    }
    for(unsigned src=0;src<256;src++) for(unsigned cmd=0;cmd<4;cmd++) {
        reset(&d); struct ese_cold_reset_arg a={.src=src,.sub_cmd=cmd};
        bool valid=(src==0 || src==1 || src==32) && cmd<=2;
        assert(run(&d,&a)==(valid?0:-EINVAL)); faults++; destroy(&d);
    }
    reset(&d); struct ese_cold_reset_arg a={0};
    struct ese_ioctl_arg e={.buf=(uintptr_t)&a,.buf_size=sizeof(a)};
    assert(ese_cold_reset_ioctl(&d,0)==-EFAULT);
    for(unsigned i=0;i<4;i++) { e.buf_size=i; assert(ese_cold_reset_ioctl(&d,(unsigned long)&e)==-EINVAL); }
    e.buf_size=4097; assert(ese_cold_reset_ioctl(&d,(unsigned long)&e)==-EINVAL);
    e.buf_size=sizeof(a); e.type=1; assert(ese_cold_reset_ioctl(&d,(unsigned long)&e)==-EINVAL);
    a.rfu=1; assert(run(&d,&a)==-EINVAL); a.rfu=0;
    copy_fail=true; assert(run(&d,&a)==-EFAULT); copy_fail=false;
    alloc_fail=true; assert(run(&d,&a)==-ENOMEM); alloc_fail=false;
    state_error=-EBUSY; assert(run(&d,&a)==-EBUSY); state_error=0;
    d.cold_reset.is_crp_en=true; d.cold_reset.last_src_ese_prot=1;
    for(unsigned c=0;c<3;c++) { a.sub_cmd=c; assert(run(&d,&a)==-EACCES); }
    a.src=1; a.sub_cmd=1; assert(run(&d,&a)==0 && !atomic_load(&wakeups));
    a.sub_cmd=2; response[3]=0x88; assert(run(&d,&a)==0x88 && d.cold_reset.is_crp_en);
    response[3]=0; assert(run(&d,&a)==0 && !d.cold_reset.is_crp_en); destroy(&d);
    for(unsigned enabled=0;enabled<2;enabled++) {
        reset(&d); early_response=false; d.cold_reset.is_nfc_enabled=enabled;
        a=(struct ese_cold_reset_arg){0}; available_result=0;
        assert(run(&d,&a)==-ETIMEDOUT); destroy(&d);
        reset(&d); early_response=false; d.cold_reset.is_nfc_enabled=enabled;
        wait_error=available_result=-ERESTARTSYS;
        assert(run(&d,&a)==-ERESTARTSYS); destroy(&d);
        reset(&d); early_response=false; d.cold_reset.is_nfc_enabled=enabled; d.removing=true;
        assert(run(&d,&a)==-ENODEV); destroy(&d);
    }
    /* An existing blocked reader must not turn no-NFC reset into an unbounded lock wait. */
    reset(&d); early_response=false; d.cold_reset.is_nfc_enabled=false;
    mutex_lock(&d.read_mutex); assert(run(&d,&a)==-ETIMEDOUT); mutex_unlock(&d.read_mutex); destroy(&d);
    /* Simultaneous timeout and header-only response: pointer stays alive until payload finishes. */
    reset(&d); early_response=false; block_payload=true;
    struct job j={.dev=&d,.request={.sub_cmd=1}}; pthread_t t,r;
    assert(!pthread_create(&t,NULL,ioctl_thread,&j));
    while(!atomic_load(&waits)) usleep(100);
    assert(!pthread_create(&r,NULL,response_thread,&d));
    while(!atomic_load(&payload_entered)) usleep(100);
    usleep(30000); assert(!atomic_load(&j.done) && atomic_load(&allocated)==1);
    atomic_store(&release_payload,1); assert(!pthread_join(r,NULL)); assert(!pthread_join(t,NULL));
    assert(j.result==-ETIMEDOUT && !d.cold_reset.is_crp_en);
    /* Late replies cannot touch the freed command. */
    assert(!nfc_cold_reset_response(&d,response,4));
    for(unsigned len=0;len<3;len++) assert(!nfc_cold_reset_response(&d,response,len));
    destroy(&d);
    printf("{\"early_response_firmware_status_cases\":%u,\"transport_and_request_fault_cases\":%u,\"wait_and_lifetime_scenarios\":9}\n",exchanges,faults);
}
