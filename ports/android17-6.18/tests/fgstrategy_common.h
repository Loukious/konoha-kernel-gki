#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef long long s64,time64_t,ktime_t;typedef unsigned long long u64;typedef unsigned u32;
struct mutex{pthread_mutex_t lock;};struct device{void *data;};struct platform_device{struct device dev;};struct work_struct{};struct workqueue_struct{int live;};struct delayed_work{struct work_struct work;int initialized,pending,disabled;};struct notifier_block{int (*notifier_call)(struct notifier_block*,unsigned long,void*);};struct power_supply{};struct timespec64{time64_t tv_sec;};
#define FG_IC_MASTER 0
#define FG_IC_SLAVE 1
#define FG_IC_MAX 2
#define POWER_SUPPLY_STATUS_UNKNOWN 0
#define POWER_SUPPLY_STATUS_CHARGING 1
#define POWER_SUPPLY_STATUS_FULL 4
#define POWER_SUPPLY_HEALTH_UNKNOWN 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define min(a,b) ((a)<(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define clamp(v,l,h) min((h),((v)<(l)?(l):(v)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define div_s64(a,b) ((s64)(a)/(b))
#define mca_log_info(...) ((void)0)
#define mca_log_err(...) ((void)0)
#define mca_log_debug(...) ((void)0)
#define mca_log_warn(...) ((void)0)
#define EPROBE_DEFER 517
/* FG_HEADER */
