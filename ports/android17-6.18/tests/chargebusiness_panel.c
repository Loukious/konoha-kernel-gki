#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EPROBE_DEFER 517
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define MCA_EVENT_TYPE_PANEL 1
#define MCA_EVENT_PANEL_SCREEN_STATE_CHANGE 2
#define MCA_EVENT_PANEL_HBM_STATE_CHANGE 3
#define PANEL_EVENT_NOTIFICATION_PRIMARY 1
#define PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER 1
struct mca_panel_dev {int screen_state,hbm_state;void *notifier_cookie;};
static struct mca_panel_dev panel_dev,*g_panel=&panel_dev;
static pthread_mutex_t panel_state_lock=PTHREAD_MUTEX_INITIALIZER;
struct mutex_guard {pthread_mutex_t *lock;};
static struct mutex_guard guard_init(pthread_mutex_t *p){pthread_mutex_lock(p);return(struct mutex_guard){p};}
static void guard_cleanup(struct mutex_guard *g){pthread_mutex_unlock(g->lock);}
#define guard(type) struct mutex_guard g __attribute__((cleanup(guard_cleanup)))=guard_init
struct device_node {int id;};
struct drm_panel {int id;};
enum panel_event_notifier_tag {TAG};
enum { DRM_PANEL_EVENT_BLANK,DRM_PANEL_EVENT_UNBLANK,DRM_PANEL_EVENT_HBM_ON,DRM_PANEL_EVENT_HBM_OFF };
struct panel_event_notification {int notif_type;};
static struct device_node nodes[3];static struct drm_panel panel;
static int refs,fault,notifications;
static struct device_node *of_find_node_by_name(void *from,const char *name){(void)from;(void)name;if(fault==1)return NULL;refs++;return &nodes[0];}
static int of_count_phandle_with_args(void *node,const char *name,void *args){(void)node;(void)name;(void)args;return fault==2?0:2;}
static struct device_node *of_parse_phandle(void *node,const char *name,int i){(void)node;(void)name;if(fault==3)return NULL;refs++;nodes[i+1].id=i+1;return &nodes[i+1];}
static void of_node_put(struct device_node *node){if(node)refs--;}
static struct drm_panel *of_drm_find_panel(struct device_node *node){if(node->id==1||fault==4)return ERR_PTR(-EPROBE_DEFER);return &panel;}
static void *panel_event_notifier_register(int tag,int client,struct drm_panel *p,void (*callback)(enum panel_event_notifier_tag,struct panel_event_notification *,void *),void *data){(void)tag;(void)client;(void)callback;assert(p==&panel&&data==&panel_dev);return fault==5?ERR_PTR(-ENOMEM):&panel;}
static int mca_panel_get_screen_state(void);
static int mca_panel_get_hbm_state(void);
static void mca_event_block_notify(int type,unsigned int event,void *data){assert(type==MCA_EVENT_TYPE_PANEL);assert(*(int *)data==(event==MCA_EVENT_PANEL_SCREEN_STATE_CHANGE?mca_panel_get_screen_state():mca_panel_get_hbm_state()));notifications++;}
/* STAGED_DEFINITIONS */
int main(void){
 for(fault=1;fault<=5;fault++){assert(mca_panel_register_panel_notifier(&panel_dev)<0);assert(!refs&&!panel_dev.notifier_cookie);}
 fault=0;assert(!mca_panel_register_panel_notifier(&panel_dev)&&!refs&&panel_dev.notifier_cookie==&panel);
 struct panel_event_notification n;
 for(int i=0;i<4;i++){n.notif_type=i;mca_panel_event_notifier_callback(TAG,&n,&panel_dev);}
 assert(notifications==4&&!panel_dev.hbm_state&&panel_dev.screen_state);
 n.notif_type=10;mca_panel_event_notifier_callback(TAG,&n,&panel_dev);assert(notifications==4);
 puts("{\"passed\":true,\"five_registration_failures_balance_OF_refs\":true,\"panel_listener_can_reenter_getters\":true}");
}
