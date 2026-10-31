#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define PAGE_SIZE 4096
#define MAX_TOUCH_PANEL_COUNT 2
#define MAX_TOUCH_ID 10
#define IS_TOUCH_ID_INVALID(i) ((i)<0||(i)>=2)
#define LOG_INFO(...)
#define LOG_ERROR(...)
#define cond_resched()
#define mutex_lock(p) assert(!(*(p))++)
#define mutex_unlock(p) assert((*(p))--)
typedef int8_t s8;typedef uint8_t u8;typedef int32_t s32;
typedef enum {ACTION_DOWN,ACTION_MOVE,ACTION_UP,ACTION_HOVER} point_action_t;
typedef enum {INPUT_STYLE_FINGER,INPUT_STYLE_STYLUS,INPUT_STYLE_INVALID} input_style_t;
enum {FINGER_SLOT,FINGER_MAJOR,FINGER_MINOR,MAX_FINGER_POINT_PROP=10};
enum {STYLUS_SLOT,STYLUS_TILT_X,STYLUS_TILT_Y,STYLUS_DISTANCE,STYLUS_PRESSURE,MAX_STYLUS_POINT_PROP=10};
typedef struct {input_style_t input_style;union{struct{s32 x,y,prop[10];point_action_t point_action;}finger;struct{s32 x,y,prop[10];point_action_t point_action;}stylus;};} hal_report_piont_t;
struct input_dev {int unused;};static struct input_dev input;
static struct input_dev *input_devs[2]={&input,&input},*stylus_input_devs[2]={&input,&input};
static int ts_report_mutex[2];static void *report_point_mmap_base;
enum {BTN_TOUCH,BTN_TOOL_FINGER,MT_TOOL_FINGER,ABS_MT_POSITION_X,ABS_MT_POSITION_Y,ABS_MT_TOUCH_MAJOR,ABS_MT_TOUCH_MINOR,BTN_TOOL_PEN,ABS_X,ABS_Y,ABS_PRESSURE,ABS_DISTANCE,ABS_TILT_X,ABS_TILT_Y};
static unsigned events,slots;
static void input_report_key(struct input_dev *d,int type,int value){assert(d==&input);events++;}
static void input_report_abs(struct input_dev *d,int type,int value){assert(d==&input);events++;}
static void input_mt_slot(struct input_dev *d,int slot){assert(slot>=0&&slot<MAX_TOUCH_ID);slots++;}
static void input_mt_report_slot_state(struct input_dev *d,int tool,int value){assert(d==&input);}
static void input_sync(struct input_dev *d){assert(d==&input);}
static void __set_bit(int slot,unsigned long *p){assert(slot>=0&&slot<10);*p|=1UL<<slot;}
static void __clear_bit(int slot,unsigned long *p){assert(slot>=0&&slot<10);*p&=~(1UL<<slot);}
/* FUNCTIONS */
int main(void){report_point_mmap_base=calloc(1,PAGE_SIZE);assert(report_point_mmap_base);hal_report_piont_t *p=report_point_mmap_base;unsigned cases=0;for(int n=0;n<256;n++){memset(p,0,PAGE_SIZE);for(unsigned i=0;i<PAGE_SIZE/sizeof(*p);i++){p[i].input_style=INPUT_STYLE_FINGER;p[i].finger.prop[FINGER_SLOT]=i%10;}int ret=report_touch_event(0,n);assert((ret==0)==(n<=PAGE_SIZE/sizeof(*p)));cases++;}
for(int slot=-200;slot<=200;slot++){memset(p,0,PAGE_SIZE);p->input_style=INPUT_STYLE_FINGER;p->finger.prop[FINGER_SLOT]=slot;unsigned old=slots;assert(!report_touch_event(0,1));assert((slots>old)==(slot>=0&&slot<10));cases++;}
for(int i=0;i<10;i++){p->finger.prop[FINGER_SLOT]=i;p->finger.point_action=ACTION_UP;assert(!report_touch_event(1,1));cases++;}
assert(report_touch_event(-1,1)<0);assert(report_touch_event(2,1)<0);free(report_point_mmap_base);printf("{\"report_point_cases\":%u,\"slot_bounds_checked_before_bit_operations\":true,\"word_sized_per_panel_bitmap\":true}\n",cases+2);}
