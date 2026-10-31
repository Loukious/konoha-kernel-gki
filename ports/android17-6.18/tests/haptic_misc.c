#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint8_t u8;
typedef long loff_t;
#define __user
#define GFP_KERNEL 0
#define AW8697_HAPTIC_CMD_READ_REG 1
#define AW8697_HAPTIC_CMD_WRITE_REG 2
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#include <stddef.h>
struct mutex { pthread_mutex_t lock; };
#define DEFINE_MUTEX(n) struct mutex n = { PTHREAD_MUTEX_INITIALIZER }
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->lock, NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->lock)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->lock)); }
struct kref { int refs; };
static void kref_init(struct kref *r) { r->refs=1; }
static void kref_get(struct kref *r) { assert(__atomic_fetch_add(&r->refs,1,__ATOMIC_RELAXED)>0); }
static void kref_put(struct kref *r,void (*release)(struct kref *)) { if(__atomic_sub_fetch(&r->refs,1,__ATOMIC_ACQ_REL)==0)release(r); }
static int live, fail_alloc, fail_copy, fail_register, fail_i2c, writes;
static void *kmalloc(size_t size,int flags) { (void)flags; if(fail_alloc)return NULL;void *p=malloc(size);assert(p);__atomic_add_fetch(&live,1,__ATOMIC_RELAXED);return p; }
static void *kzalloc(size_t size,int flags) { void *p=kmalloc(size,flags);if(p)memset(p,0,size);return p; }
static void kfree(void *p) { if(p){assert(__atomic_sub_fetch(&live,1,__ATOMIC_RELAXED)>=0);free(p);} }
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((long)(p))
static void *memdup_user(const void *p,size_t n) { if(fail_copy)return(void *)-EFAULT;void *d=kmalloc(n,0);if(!d)return(void *)-ENOMEM;memcpy(d,p,n);return d; }
static int copy_to_user(void *to,const void *from,size_t n) { if(fail_copy)return 1;memcpy(to,from,n);return 0; }
struct aw8697 { struct mutex lock; bool stopping; };
struct file { void *private_data; };
struct inode { int unused; };
struct miscdevice { int unused; };
static struct miscdevice aw8697_haptic_misc;
static int registered;
static int misc_register(struct miscdevice *m) { (void)m;if(fail_register)return -EIO;assert(!registered);registered=1;return 0; }
static void misc_deregister(struct miscdevice *m) { (void)m;assert(registered);registered=0; }
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int block_i2c, i2c_started, unblock_i2c;
static int aw8697_i2c_read(struct aw8697 *aw,u8 reg,u8 *value) {
 (void)aw;assert(!pthread_mutex_lock(&gate));if(block_i2c){i2c_started=1;assert(!pthread_cond_broadcast(&condition));while(!unblock_i2c)assert(!pthread_cond_wait(&condition,&gate));}assert(!pthread_mutex_unlock(&gate));
 if(fail_i2c)return -EIO;*value=reg;return 0;
}
static int aw8697_i2c_write(struct aw8697 *aw,u8 reg,u8 value) { (void)aw;(void)reg;(void)value;if(fail_i2c)return -EIO;writes++;return 0; }
/* STRUCTURES */
/* OPERATIONS */
/* REGISTRATION */
static int read_done, remove_done;
static void *reading(void *arg) { struct file *file=arg;u8 data;assert(aw8697_file_read(file,(char *)&data,1,NULL)==1&&data==7);__atomic_store_n(&read_done,1,__ATOMIC_RELEASE);return NULL; }
static void *removing(void *arg) { (void)arg;aw8697_misc_unregister();__atomic_store_n(&remove_done,1,__ATOMIC_RELEASE);return NULL; }
int main(void) {
 alarm(20);struct aw8697 aw={0}, next={0};mutex_init(&aw.lock);mutex_init(&next.lock);struct file a={0},b={0},old={0};struct inode node={0};u8 data[258]={1,7};int faults=0,bounds=0;
 fail_alloc=1;assert(aw8697_misc_register(&aw)==-ENOMEM&&!live);fail_alloc=0;fail_register=1;assert(aw8697_misc_register(&aw)==-EIO&&!live&&!aw8697_misc_context);fail_register=0;assert(!aw8697_misc_register(&aw));
 fail_alloc=1;assert(aw8697_file_open(&node,&a)==-ENOMEM);fail_alloc=0;assert(!aw8697_file_open(&node,&a));assert(!aw8697_file_open(&node,&b));assert(aw8697_file_write(&a,(char *)data,2,NULL)==2);data[1]=33;assert(aw8697_file_write(&b,(char *)data,2,NULL)==2);data[0]=0;assert(aw8697_file_read(&a,(char *)data,4,NULL)==4&&data[0]==7&&data[3]==10);assert(aw8697_file_read(&b,(char *)data,1,NULL)==1&&data[0]==33);
 assert(aw8697_file_unlocked_ioctl(&a,0,0)==-ENOTTY);fail_alloc=1;assert(aw8697_file_read(&a,(char *)data,1,NULL)==-ENOMEM);assert(aw8697_file_write(&a,(char *)data,2,NULL)==-ENOMEM);fail_alloc=0;fail_copy=1;assert(aw8697_file_read(&a,(char *)data,1,NULL)==-EFAULT);assert(aw8697_file_write(&a,(char *)data,2,NULL)==-EFAULT);fail_copy=0;fail_i2c=1;assert(aw8697_file_read(&a,(char *)data,1,NULL)==-EIO);data[0]=2;assert(aw8697_file_write(&a,(char *)data,3,NULL)==-EIO);fail_i2c=0;faults=6;
 for(int n=0;n<259;n++) { data[0]=2;data[1]=255;ssize_t r=aw8697_file_write(&a,(char *)data,n,NULL);assert(r==(n==3?3:-EINVAL));bounds++; }
 data[0]=1;data[1]=255;assert(aw8697_file_write(&a,(char *)data,2,NULL)==2);for(int n=1;n<259;n++){ssize_t r=aw8697_file_read(&a,(char *)data,n,NULL);assert(r==(n==1?1:-EINVAL));bounds++;}
 data[0]=1;data[1]=7;assert(aw8697_file_write(&a,(char *)data,2,NULL)==2);block_i2c=1;pthread_t reader,remover;assert(!pthread_create(&reader,NULL,reading,&a));assert(!pthread_mutex_lock(&gate));while(!i2c_started)assert(!pthread_cond_wait(&condition,&gate));assert(!pthread_mutex_unlock(&gate));assert(!pthread_create(&remover,NULL,removing,NULL));usleep(20000);assert(!READ_ONCE(remove_done));assert(!pthread_mutex_lock(&gate));unblock_i2c=1;assert(!pthread_cond_broadcast(&condition));assert(!pthread_mutex_unlock(&gate));assert(!pthread_join(reader,NULL)&&!pthread_join(remover,NULL)&&read_done&&remove_done);
 assert(aw8697_file_read(&a,(char *)data,1,NULL)==-ENODEV);assert(aw8697_file_write(&a,(char *)data,2,NULL)==-ENODEV);assert(aw8697_file_unlocked_ioctl(&a,0,0)==-ENODEV);assert(aw8697_file_open(&node,&old)==-ENODEV);assert(!aw8697_misc_register(&next));assert(aw8697_file_unlocked_ioctl(&a,0,0)==-ENODEV);assert(!aw8697_file_open(&node,&old));assert(aw8697_file_unlocked_ioctl(&old,0,0)==-ENOTTY);assert(!aw8697_file_release(&node,&a)&&!aw8697_file_release(&node,&b));aw8697_misc_unregister();assert(!aw8697_file_release(&node,&old)&&!live);
 printf("{\"allocation_registration_faults\":2,\"io_faults\":%d,\"bounds_cases\":%d,\"independent_file_registers\":true,\"removal_waits_for_io\":true,\"stale_handles_disconnected_after_rebind\":true}\n",faults,bounds);
}
