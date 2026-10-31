#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFP_KERNEL 0
#define __GFP_ZERO 1
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) > -4096)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define pr_err_ratelimited(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define SND_EVENT_DOWN 0
#define SND_EVENT_UP 1
struct device { int id; atomic_int refs; };
struct snd_event_ops {
    int (*enable)(struct device *, void *);
    void (*disable)(struct device *, void *);
};
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(n) struct list_head n = { &(n), &(n) }
#define list_empty(h) ((h)->next == (h))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define list_for_each_entry(p,h,m) \
    for (p=container_of((h)->next,__typeof__(*p),m); &(p)->m!=(h); p=container_of((p)->m.next,__typeof__(*p),m))
static void list_add_tail(struct list_head *n, struct list_head *h) {
    n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n;
}
static void list_del(struct list_head *n) { n->next->prev=n->prev; n->prev->next=n->next; }
struct mutex { pthread_mutex_t lock; };
#define DEFINE_MUTEX(n) struct mutex n = { PTHREAD_MUTEX_INITIALIZER }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->lock)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->lock)); }
static struct device *get_device(struct device *d) { atomic_fetch_add(&d->refs,1); return d; }
static void put_device(struct device *d) { assert(atomic_fetch_sub(&d->refs,1)>0); }
struct allocation { size_t size; max_align_t align; };
static atomic_int live, steps;
static int fail_step;
static bool gated;
static size_t gated_size;
static pthread_barrier_t allocation_barrier;
static bool failure(void) { return atomic_fetch_add(&steps,1)+1==fail_step; }
static void *kzalloc(size_t size,int flags) {
    (void)flags;
    if(gated && size==gated_size) {
        int ret=pthread_barrier_wait(&allocation_barrier);
        assert(ret==0 || ret==PTHREAD_BARRIER_SERIAL_THREAD);
    }
    if(failure())return NULL;
    struct allocation *p=calloc(1,sizeof(*p)+size);
    assert(p); p->size=size; atomic_fetch_add(&live,1); return p+1;
}
static void kfree(void *ptr) { if(ptr){assert(!IS_ERR(ptr));free((struct allocation *)ptr-1);assert(atomic_fetch_sub(&live,1)>0);} }
static void *krealloc_array(void *old,size_t count,size_t size,int flags) {
    if(count>SIZE_MAX/size)return NULL;
    if(!old)return kzalloc(count*size,flags);
    if(failure())return NULL;
    struct allocation *p=(struct allocation *)old-1;
    size_t previous=p->size;
    p=realloc(p,sizeof(*p)+count*size); assert(p); p->size=count*size;
    if(p->size>previous)memset((char *)(p+1)+previous,0,p->size-previous);
    return p+1;
}
static void *krealloc(void *old,size_t size,int flags) { return krealloc_array(old,1,size,flags); }

/* SOURCE */

static struct device clients[3]={{.id=0},{.id=1},{.id=2}}, masters[2]={{.id=3},{.id=4}};
static int enabled[5], enables, disables, fail_id=-1;
static int enable(struct device *d,void *p) { (void)p;enables++;if(d->id==fail_id)return -EIO;assert(!enabled[d->id]);enabled[d->id]=1;return 0; }
static void disable(struct device *d,void *p) { (void)p;assert(enabled[d->id]);enabled[d->id]=0;disables++; }
static const struct snd_event_ops ops={enable,disable};
static int compare(struct device *d,void *p) { return d==p; }
static struct snd_event_clients *array(unsigned n) {
    struct snd_event_clients *a=NULL;
    for(unsigned i=0;i<n;i++)snd_event_mstr_add_client(&a,compare,&clients[i]);
    assert(a&&!IS_ERR(a));return a;
}
static void free_array(struct snd_event_clients *a) { kfree(a->cl_arr); kfree(a); }
static void clean(void) {
    assert(!master&&list_empty(&snd_event_client_list)&&!atomic_load(&live));
    for(unsigned i=0;i<3;i++)assert(!atomic_load(&clients[i].refs));
    for(unsigned i=0;i<2;i++)assert(!atomic_load(&masters[i].refs));
    for(unsigned i=0;i<5;i++)assert(!enabled[i]);
}
static unsigned lifecycle(void) {
    unsigned cases=0;
    for(unsigned count=1;count<=3;count++)for(unsigned remove=0;remove<count;remove++)for(unsigned late=0;late<=count;late++) {
        struct snd_event_clients *a=array(count);
        for(unsigned i=0;i<late;i++)assert(!snd_event_client_register(&clients[i],&ops,NULL));
        assert(!snd_event_master_register(&masters[0],&ops,a,NULL));
        /* Removal from a partially discovered master must tolerate empty slots. */
        if(late<count && late){assert(!snd_event_client_deregister(&clients[0]));assert(!snd_event_client_register(&clients[0],&ops,NULL));}
        for(unsigned i=late;i<count;i++)assert(!snd_event_client_register(&clients[i],&ops,NULL));
        assert(master->clients_found);
        assert(snd_event_client_register(&clients[0],&ops,NULL)==-EALREADY);
        assert(!snd_event_notify(&masters[0],SND_EVENT_UP));
        for(unsigned i=0;i<count;i++)assert(!snd_event_notify(&clients[i],SND_EVENT_UP));
        assert(master->fwk_state);
        assert(!snd_event_client_deregister(&clients[remove]));
        assert(!master->fwk_state&&!master->clients_found);
        assert(!master->clients->cl_arr[remove].clnt);
        assert(!snd_event_client_register(&clients[remove],&ops,NULL));
        assert(!snd_event_notify(&clients[remove],SND_EVENT_UP));
        assert(master->fwk_state);
        assert(!snd_event_master_deregister(&masters[0]));
        a=array(count);
        assert(!snd_event_master_register(&masters[1],&ops,a,NULL));
        assert(master->clients_found);
        assert(!snd_event_notify(&masters[1],SND_EVENT_UP));
        assert(master->fwk_state);
        assert(!snd_event_master_deregister(&masters[1]));
        for(unsigned i=0;i<count;i++)assert(!snd_event_client_deregister(&clients[i]));
        clean();cases++;
    }
    return cases;
}
static unsigned faults(void) {
    unsigned cases=0;
    for(unsigned populated=0;populated<=3;populated++) {
        struct snd_event_clients *a=populated?array(populated):NULL;
        for(int f=1;f<=(populated?1:2);f++) {
            if(f>1)a=NULL;
            steps=0;fail_step=f;snd_event_mstr_add_client(&a,compare,&clients[0]);fail_step=0;
            assert(IS_ERR(a));assert(!atomic_load(&live));cases++;
        }
    }
    struct snd_event_clients *a=array(1);a->num_clients=SIZE_MAX;
    snd_event_mstr_add_client(&a,compare,&clients[0]);assert(IS_ERR(a));clean();cases++;
    a=array(1);steps=0;fail_step=1;assert(snd_event_master_register(&masters[0],&ops,a,NULL)==-ENOMEM);fail_step=0;free_array(a);clean();cases++;
    steps=0;fail_step=1;assert(snd_event_client_register(&clients[0],&ops,NULL)==-ENOMEM);fail_step=0;clean();cases++;
    return cases;
}
static unsigned partial(void) {
    assert(!snd_event_client_register(&clients[0],&ops,NULL));
    assert(!snd_event_client_register(&clients[2],&ops,NULL));
    assert(!snd_event_master_register(&masters[0],&ops,array(2),NULL));
    assert(!master->clients_found);
    assert(!snd_event_client_deregister(&clients[2]));
    assert(!snd_event_client_deregister(&clients[0]));
    assert(!snd_event_master_deregister(&masters[0]));clean();return 1;
}
static unsigned stale(void) {
    for(int i=0;i<2;i++)assert(!snd_event_client_register(&clients[i],&ops,NULL));
    assert(!snd_event_master_register(&masters[0],&ops,array(2),NULL));
    for(int i=0;i<2;i++)assert(!snd_event_client_deregister(&clients[i]));
    assert(!snd_event_master_deregister(&masters[0]));clean();return 1;
}
static unsigned detach(void) {
    assert(!snd_event_client_register(&clients[0],&ops,NULL));
    for(int i=0;i<2;i++){
        assert(!snd_event_master_register(&masters[i],&ops,array(1),NULL));
        assert(master->clients_found);
        assert(!snd_event_master_deregister(&masters[i]));
    }
    assert(!snd_event_client_deregister(&clients[0]));clean();return 1;
}
static unsigned rollback(void) {
    for(int id=0;id<4;id++) {
        for(int i=0;i<3;i++)assert(!snd_event_client_register(&clients[i],&ops,NULL));
        assert(!snd_event_master_register(&masters[0],&ops,array(3),NULL));
        assert(!snd_event_notify(&masters[0],SND_EVENT_UP));
        for(int i=0;i<2;i++)assert(!snd_event_notify(&clients[i],SND_EVENT_UP));
        fail_id=id;assert(snd_event_notify(&clients[2],SND_EVENT_UP)==-EIO);assert(!master->fwk_state);
        for(int i=0;i<5;i++)assert(!enabled[i]);
        fail_id=-1;assert(!snd_event_notify(&clients[2],SND_EVENT_UP));assert(master->fwk_state);
        assert(!snd_event_master_deregister(&masters[0]));
        for(int i=0;i<3;i++)assert(!snd_event_client_deregister(&clients[i]));
        clean();
    }
    return 4;
}
struct request { struct device *dev;struct snd_event_clients *array;int result; };
static void *register_master(void *p) { struct request *r=p;r->result=snd_event_master_register(r->dev,&ops,r->array,NULL);return NULL; }
static unsigned races(void) {
    assert(!snd_event_client_register(&clients[0],&ops,NULL));
    struct request req[2]={{&masters[0],array(1),-99},{&masters[1],array(1),-99}};
    pthread_t threads[2];assert(!pthread_barrier_init(&allocation_barrier,NULL,2));
    gated_size=sizeof(struct snd_master);gated=true;
    for(int i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,register_master,&req[i]));
    for(int i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));
    gated=false;assert(!pthread_barrier_destroy(&allocation_barrier));
    assert((req[0].result==0&&req[1].result==-EALREADY)||(req[1].result==0&&req[0].result==-EALREADY));
    for(int i=0;i<2;i++)if(req[i].result)free_array(req[i].array);
    assert(!snd_event_master_deregister(master->dev));assert(!snd_event_client_deregister(&clients[0]));clean();return 1;
}
int main(int argc,char **argv) {
    assert(argc==2);unsigned n=0;
    if(!strcmp(argv[1],"lifecycle"))n=lifecycle();
    else if(!strcmp(argv[1],"partial"))n=partial();
    else if(!strcmp(argv[1],"stale"))n=stale();
    else if(!strcmp(argv[1],"detach"))n=detach();
    else if(!strcmp(argv[1],"faults"))n=faults();
    else if(!strcmp(argv[1],"rollback"))n=rollback();
    else if(!strcmp(argv[1],"races"))n=races();
    else if(!strcmp(argv[1],"nulls")){struct snd_event_clients *a=array(1);assert(snd_event_client_register(&clients[0],NULL,NULL)==-EINVAL);assert(snd_event_master_register(&masters[0],NULL,a,NULL)==-EINVAL);free_array(a);clean();n=2;}
    else abort();
    printf("{\"snd_event_%s_cases\":%u}\n",argv[1],n);
}
