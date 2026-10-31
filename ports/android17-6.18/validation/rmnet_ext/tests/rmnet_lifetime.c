#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define __init
#define __exit
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static int fail_at, init_step, tcp_live, udp_live, genl_live, hooks_live, barriers;
struct delayed_work { int pending, entries, purges; };
struct flow_work { struct delayed_work DATARMNET190b4452e8; bool DATARMNETcd94e0d3c7; };
static struct flow_work DATARMNETac6535da80, DATARMNET3ed0b852e9, DATARMNETa41953a37b, DATARMNET0b04fde883;
static void cancel_delayed_work_sync(struct delayed_work *w) { w->pending=0; }
static void schedule_delayed_work(struct delayed_work *w, int delay) { assert(!delay); w->pending=1; }
static void flush_delayed_work(struct delayed_work *w) { assert(w->pending); w->pending=0; w->entries=0; w->purges++; }
#include "drains.inc"
static int acquire(int *live) { if (++init_step==fail_at) return -17; assert(!*live); *live=1; return 0; }
static int DATARMNET7e9995246e(void) { return acquire(&tcp_live); }
static int DATARMNETe80a33d544(void) { return acquire(&udp_live); }
static int DATARMNET31ff8cdc40(void) { return acquire(&genl_live); }
static void DATARMNET49c17a32bc(void) { assert(tcp_live && udp_live && genl_live); hooks_live=1; }
static void DATARMNET8e1548cd2e(void) { assert(genl_live); genl_live=0; }
static void DATARMNET41e8cc085c(void) { assert(!genl_live); hooks_live=0; }
static void tcp_release(void) { assert(tcp_live && !hooks_live); tcp_live=0; DATARMNET38bb6f2b7a(); }
static void udp_release(void) { assert(udp_live && !hooks_live); udp_live=0; DATARMNET4b5170a1ef(); }
static void rcu_barrier(void) { assert(!tcp_live && !udp_live && !hooks_live); barriers++; }
#define DATARMNET38bb6f2b7a tcp_release
#define DATARMNET4b5170a1ef udp_release
#include "perf_lifecycle.inc"
#undef DATARMNET38bb6f2b7a
#undef DATARMNET4b5170a1ef
static void reset(void) {
 init_step=tcp_live=udp_live=genl_live=hooks_live=barriers=0;
 memset(&DATARMNETac6535da80,0,sizeof(DATARMNETac6535da80));
 memset(&DATARMNET3ed0b852e9,0,sizeof(DATARMNET3ed0b852e9));
}
int main(void) {
 for (fail_at=1;fail_at<=3;fail_at++) {
  reset(); assert(DATARMNET63abbdc3d3()==-17);
  assert(!tcp_live && !udp_live && !genl_live && !hooks_live);
 }
 fail_at=0; reset(); assert(!DATARMNET63abbdc3d3());
 assert(tcp_live && udp_live && genl_live && hooks_live);
 assert(!DATARMNETac6535da80.DATARMNETcd94e0d3c7 && !DATARMNET3ed0b852e9.DATARMNETcd94e0d3c7);
 DATARMNETac6535da80.DATARMNET190b4452e8.entries=7;
 DATARMNET3ed0b852e9.DATARMNET190b4452e8.entries=11;
 DATARMNETa343229e33();
 assert(barriers==1 && !DATARMNETac6535da80.DATARMNET190b4452e8.entries && !DATARMNET3ed0b852e9.DATARMNET190b4452e8.entries);
 DATARMNETa41953a37b.DATARMNET190b4452e8.entries=5;
 DATARMNET0b04fde883.DATARMNET190b4452e8.entries=9;
 DATARMNET8c0e010dfb(); DATARMNETf4e1a29dbc();
 assert(!DATARMNETa41953a37b.DATARMNET190b4452e8.entries && !DATARMNET0b04fde883.DATARMNET190b4452e8.entries);
 puts("{\"lifetime\":\"PASS\",\"init_failures\":3,\"purge_paths\":4}");
}
