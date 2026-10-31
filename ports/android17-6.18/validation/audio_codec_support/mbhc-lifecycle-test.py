#!/usr/bin/env python3
"""Exercise extracted MBHC lifecycle functions against faulting host services.

The kernel driver is not executed as a complete module. Framework calls and
hardware helpers are mocked; the lifecycle function bodies come from the
selected source tree on every run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def function(source, name):
    m = re.search(r"^(?:static )?(?:inline )?(?:int|void|bool|irqreturn_t) " + name + r"\([^;]*?\n\{", source, re.M)
    if not m:
        raise ValueError(name)
    start = source.index("{", m.start())
    depth = 1
    i = start + 1
    while depth:
        if source.startswith("/*", i):
            i = source.index("*/", i + 2) + 2
            continue
        if source.startswith("//", i):
            i = source.index("\n", i + 2)
            continue
        if source[i] in "\"'":
            quote = source[i]
            i += 1
            while source[i] != quote:
                i += 2 if source[i] == "\\" else 1
        elif source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    return source[m.start():i] + "\n"


SERVICES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
typedef uint32_t u32;
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define IRQ_NONE 0
#define CONFIG_QCOM_WCD_USBSS_I2C 1
#define CONFIG_QCOM_FSA4480_I2C 1
#define CONFIG_AUDIO_UART_DEBUG 1
#define IS_ENABLED(x) (x)
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
#define smp_load_acquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#define unlikely(x) (x)
#define MBHC_PLUG_TYPE_NONE 0
#define WCD_MBHC_HPH_NONE 0
#define GND_MIC_SWAP_THRESHOLD 4
#define GND_MIC_USBC_SWAP_THRESHOLD 8
#define HPHL_CROSS_CONN_THRESHOLD 1
#define HPHR_CROSS_CONN_THRESHOLD 1
#define V_45_MV 1
#define I_3P0_UA 2
#define R_24_KOHM 3
#define WCD_MBHC_JACK_MASK 15
#define WCD_MBHC_JACK_BUTTON_MASK 7
#define SND_JACK_BTN_0 1
#define KEY_MEDIA 226
#define WCD_DETECTION_LEGACY 0
#define WCD_DETECTION_ADC 1
#define WCD_MBHC_ELEC_HS_INS 0
#define WCD_MBHC_ELEC_HS_REM 1
#define FW_READ_TIMEOUT 4000000
#define FW_READ_ATTEMPTS 3
#define WCD_MBHC_USLEEP_RANGE_MARGIN_US 10
#define WCD9XXX_MBHC_CAL 0
#define NOTIFY_DONE 0
struct mutex { bool ready, locked; };
struct work_struct { bool ready, pending; void (*fn)(struct work_struct *); };
struct delayed_work { struct work_struct work; };
struct completion { bool ready; };
struct wait_queue { bool ready; };
struct device_node { int id; const char *full_name; int refs; };
struct device { struct device_node *of_node; };
struct snd_soc_card { struct device *dev; };
struct snd_soc_component { struct snd_soc_card *card; struct device *dev; };
struct snd_soc_jack { void *jack; };
struct extcon_dev { bool allocated, registered; };
struct firmware { const unsigned char *data; size_t size; };
struct firmware_cal { void *data; size_t size; };
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); int priority; };
struct wcd_mbhc_config {
    unsigned enable_usbc_analog;
    int uart_audio_switch_gpio;
    struct device_node *uart_audio_switch_gpio_p;
    bool moisture_en, moisture_duty_cycle_en, read_fw_bin;
    void *calibration;
};
struct wcd_mbhc;
struct wcd_mbhc_intr {
    int mbhc_sw_intr, mbhc_btn_press_intr, mbhc_btn_release_intr;
    int mbhc_hs_ins_intr, mbhc_hs_rem_intr, hph_left_ocp, hph_right_ocp;
};
struct wcd_mbhc_register { int unused; };
struct wcd_mbhc_cb {
    int (*request_irq)(struct snd_soc_component *, int, irqreturn_t (*)(int,void *), const char *, void *);
    void (*irq_control)(struct snd_soc_component *, int, bool);
    int (*free_irq)(struct snd_soc_component *, int, void *);
    int (*map_btn_code_to_num)(struct snd_soc_component *);
    bool (*lock_sleep)(struct wcd_mbhc *, bool);
    void (*mbhc_bias)(struct snd_soc_component *, bool);
    void (*set_btn_thr)(void);
    int (*register_notifier)(struct wcd_mbhc *, struct notifier_block *, bool);
    void (*skip_imped_detect)(struct snd_soc_component *);
    struct firmware_cal *(*get_hwdep_fw_cal)(struct wcd_mbhc *, int);
};
struct wcd_mbhc_fn {
    irqreturn_t (*wcd_mbhc_hs_ins_irq)(int,void *);
    irqreturn_t (*wcd_mbhc_hs_rem_irq)(int,void *);
    void (*wcd_mbhc_detect_plug_type)(struct wcd_mbhc *);
    void (*wcd_cancel_hs_detect_plug)(struct wcd_mbhc *, struct work_struct *);
};
struct wcd_mbhc {
    bool mbhc_initialized, mbhc_started, notifier_registered, usbc_notifier_registered;
    bool deinit_in_progress, in_swch_irq_handler, is_btn_press, impedance_detect;
    bool micbias_enable, btn_press_intr, is_hs_recording, is_extn_cable;
    bool extn_cable_hph_rem, is_btn_already_regd, hs_detect_work_stop;
    int hs_thr, hph_thr, moist_vref, moist_iref, moist_rref, current_plug;
    int hphl_swh, gnd_swh, hph_type, swap_thr, hphl_cross_conn_thr, hphr_cross_conn_thr;
    int hph_status, mbhc_detection_logic;
    unsigned long intr_status;
    struct snd_soc_component *component;
    const struct wcd_mbhc_intr *intr_ids;
    const struct wcd_mbhc_cb *mbhc_cb;
    struct wcd_mbhc_register *wcd_mbhc_regs;
    struct wcd_mbhc_config *mbhc_cfg;
    void *default_calibration;
    struct device_node *wcd_usbss_aatc_dev_np, *fsa_aatc_dev_np, *uart_audio_node;
    struct snd_soc_jack headset_jack, button_jack;
    struct delayed_work mbhc_firmware_dwork, mbhc_btn_dwork;
    struct work_struct correct_plug_swch;
    struct completion btn_press_compl;
    struct wait_queue wait_btn_press;
    struct mutex codec_resource_lock;
    struct notifier_block nblock, aatc_dev_nb;
    struct wcd_mbhc_fn *mbhc_fn;
    struct extcon_dev *extdev;
    const struct firmware *mbhc_fw;
    struct firmware_cal *mbhc_cal;
};
static struct wcd_mbhc *cur;
static struct device_node nodes[] = {{0,"root",0},{1,"usbss",0},{2,"fsa",0},{3,"uart",0}};
static struct device dev = {&nodes[0]};
static struct snd_soc_card card = {&dev};
static struct snd_soc_component component = {&card,&dev};
static struct wcd_mbhc_intr intr = {1,2,3,4,5,6,7};
static struct wcd_mbhc_register regs;
static struct extcon_dev extdev;
static int provider, fail_irq, irq_calls, jack_calls, fail_jack, key_calls, fail_key;
static int fail_ext_alloc, fail_ext_reg, fail_notifier, fail_hardware, fail_usb;
static int fail_prop, irq_owned[8], codec_notifier, usb_notifier, wake_votes;
static int cancel_fw, cancel_btn, cancel_detect, fw_released, hardware_calls;
static int irq_disabled, uart_present, irq_in_notifier, switches;
static int skip_calls;
static bool refuse_wake, finish_firmware_on_cancel, complete_fw_after_stop;
static const unsigned mbhc_ext_dev_supported_table[] = {0};
static void wcd_mbhc_stop(struct wcd_mbhc *);
static void wcd_mbhc_fw_read(struct work_struct *);
static irqreturn_t wcd_mbhc_mech_plug_detect_irq(int, void *);
static void wcd_schedule_hs_detect_plug(struct wcd_mbhc *, struct work_struct *);
static void wcd_cancel_hs_detect_plug(struct wcd_mbhc *, struct work_struct *);
static void mutex_init(struct mutex *m) { assert(!m->ready); m->ready=true; }
static void mutex_lock(struct mutex *m) { assert(m->ready && !m->locked); m->locked=true; }
static void mutex_unlock(struct mutex *m) { assert(m->ready && m->locked); m->locked=false; }
static void mutex_destroy(struct mutex *m) { assert(m->ready && !m->locked); m->ready=false; }
#define WCD_MBHC_RSC_LOCK(m) mutex_lock(&(m)->codec_resource_lock)
#define WCD_MBHC_RSC_UNLOCK(m) mutex_unlock(&(m)->codec_resource_lock)
#define WCD_MBHC_RSC_ASSERT_LOCKED(m) assert((m)->codec_resource_lock.ready && (m)->codec_resource_lock.locked)
#define INIT_DELAYED_WORK(w,f) do { (w)->work.ready=true; (w)->work.pending=false; (w)->work.fn=(f); } while(0)
#define INIT_WORK(w,f) do { (w)->ready=true; (w)->pending=false; (w)->fn=(f); } while(0)
static void init_completion(struct completion *c) { c->ready=true; }
static void init_waitqueue_head(struct wait_queue *w) { w->ready=true; }
static void clear_bit(int b,unsigned long *p) { *p &= ~(1UL<<b); }
static int of_property_read_u32(struct device_node *n,const char *p,void *v) {
    if (fail_prop && strstr(p,"-swh")) return -EINVAL;
    *(unsigned *)v = strstr(p,"usbc-audio") ? (provider!=0) : 1;
    return 0;
}
static int of_property_read_u32_array(struct device_node *n,const char *p,u32 *v,int len) { return -ENOENT; }
static void *of_find_property(struct device_node *n,const char *p,void *x) {
    if (strstr(p,"uart-audio")) return uart_present ? n : NULL;
    return provider ? n : NULL;
}
static struct device_node *of_parse_phandle(struct device_node *n,const char *p,int x) {
    int id=0;
    if (strstr(p,"wcd939x") && (provider==1 || provider==3)) id=1;
    if (strstr(p,"fsa4480") && (provider==2 || provider==3)) id=2;
    if (strstr(p,"uart-audio") && uart_present) id=3;
    if (!id) return NULL;
    nodes[id].refs++;
    return &nodes[id];
}
static void of_node_put(struct device_node *n) { if (n) { assert(n->refs>0); n->refs--; } }
static int of_get_named_gpio(struct device_node *n,const char *p,int x) { return -ENOENT; }
static bool gpio_is_valid(int gpio) { return gpio>=0; }
static int snd_soc_card_jack_new(struct snd_soc_card *c,const char *name,int mask,struct snd_soc_jack *j) {
    if (++jack_calls==fail_jack) return -ENOMEM;
    j->jack=j; return 0;
}
static int snd_jack_set_key(void *j,int b,int key) { assert(j); key_calls++; return fail_key ? -EINVAL : 0; }
static struct extcon_dev *devm_extcon_dev_allocate(struct device *d,const unsigned *t) {
    if (fail_ext_alloc) return ERR_PTR(-ENOMEM);
    assert(!extdev.allocated); extdev.allocated=true; return &extdev;
}
static int devm_extcon_dev_register(struct device *d,struct extcon_dev *e) {
    assert(e->allocated);
    if (fail_ext_reg) return -EIO;
    e->registered=true; return 0;
}
static void devm_extcon_dev_free(struct device *d,struct extcon_dev *e) { assert(e->allocated && !e->registered); e->allocated=false; }
static void ready(void) {
    assert(cur->codec_resource_lock.ready && cur->wait_btn_press.ready && cur->btn_press_compl.ready);
    assert(cur->mbhc_firmware_dwork.work.ready && cur->mbhc_btn_dwork.work.ready && cur->correct_plug_swch.ready);
    assert(cur->mbhc_fn && cur->extdev && cur->extdev->registered);
}
static irqreturn_t irq_stub(int n,void *p) { return IRQ_HANDLED; }
static void detect_stub(struct wcd_mbhc *m) {}
static void work_stub(struct work_struct *w) {}
static void wcd_btn_lpress_fn(struct work_struct *w) {}
static int wcd_event_notify(struct notifier_block *n,unsigned long e,void *p) { return NOTIFY_DONE; }
static int wcd_mbhc_usbc_ana_event_handler(struct notifier_block *n,unsigned long e,void *p) { assert(cur->mbhc_started && cur->mbhc_cfg); return 0; }
static struct wcd_mbhc_fn fns = {irq_stub,irq_stub,detect_stub,wcd_cancel_hs_detect_plug};
static void wcd_mbhc_legacy_init(struct wcd_mbhc *m) {}
static void wcd_mbhc_adc_init(struct wcd_mbhc *m) { m->mbhc_fn=&fns; INIT_WORK(&m->correct_plug_swch,work_stub); }
static irqreturn_t wcd_mbhc_btn_press_handler(int n,void *p) { return IRQ_HANDLED; }
static irqreturn_t wcd_mbhc_release_handler(int n,void *p) { return IRQ_HANDLED; }
static irqreturn_t wcd_mbhc_hphl_ocp_irq(int n,void *p) { return IRQ_HANDLED; }
static irqreturn_t wcd_mbhc_hphr_ocp_irq(int n,void *p) { return IRQ_HANDLED; }
static void wcd_mbhc_swch_irq_handler(struct wcd_mbhc *m) { switches++; }
static int request_irq_stub(struct snd_soc_component *c,int n,irqreturn_t (*fn)(int,void*),const char *s,void *p) {
    ready(); assert(!cur->mbhc_started && !irq_owned[n]);
    if (++irq_calls==fail_irq) return -EIO;
    irq_owned[n]=1; return 0;
}
static void irq_control_stub(struct snd_soc_component *c,int n,bool enable) { assert(irq_owned[n]); if(!enable)irq_disabled++; }
static int free_irq_stub(struct snd_soc_component *c,int n,void *p) { assert(irq_owned[n] && !cur->mbhc_started); irq_owned[n]=0; return 0; }
static int map_btn_stub(struct snd_soc_component *c) { return 0; }
static bool lock_sleep_stub(struct wcd_mbhc *m,bool acquire) {
    if(acquire && refuse_wake) return false;
    wake_votes += acquire ? 1 : -1; assert(wake_votes>=0); return true;
}
static void bias_stub(struct snd_soc_component *c,bool on) {}
static void btn_thr_stub(void) {}
static int notifier_stub(struct wcd_mbhc *m,struct notifier_block *nb,bool enable) {
    if(enable) { ready(); assert(!codec_notifier); if(fail_notifier)return -EIO; codec_notifier=1; }
    else {
        assert(codec_notifier && !m->mbhc_started && !m->codec_resource_lock.locked);
        if(irq_in_notifier) {
            WCD_MBHC_RSC_LOCK(m);
            wcd_schedule_hs_detect_plug(m,&m->correct_plug_swch);
            WCD_MBHC_RSC_UNLOCK(m);
            assert(!m->correct_plug_swch.pending);
        }
        codec_notifier=0;
    }
    return 0;
}
static void skip_impedance_stub(struct snd_soc_component *c) {
    assert(!cur->codec_resource_lock.locked && !cancel_detect);
    skip_calls++;
}
static const struct wcd_mbhc_cb cb = {request_irq_stub,irq_control_stub,free_irq_stub,map_btn_stub,lock_sleep_stub,bias_stub,btn_thr_stub,notifier_stub,skip_impedance_stub,NULL};
static int register_usb(struct notifier_block *nb,struct device_node *n,bool fsa) {
    assert(!usb_notifier && n->refs>0 && cur->mbhc_started);
    usb_notifier=1;
    nb->notifier_call(nb,0,NULL);
    if(fail_usb && fsa)usb_notifier=0;
    return fail_usb ? -EIO : 0;
}
static int wcd_usbss_reg_notifier(struct notifier_block *n,struct device_node *d) { return register_usb(n,d,false); }
static int fsa4480_reg_notifier(struct notifier_block *n,struct device_node *d) { return register_usb(n,d,true); }
static int unregister_usb(struct notifier_block *n,struct device_node *d) {
    assert(!cur->mbhc_started && !cur->codec_resource_lock.locked);
    if(!usb_notifier)return -ENOENT;
    usb_notifier=0; return 0;
}
static int wcd_usbss_unreg_notifier(struct notifier_block *n,struct device_node *d) { return unregister_usb(n,d); }
static int fsa4480_unreg_notifier(struct notifier_block *n,struct device_node *d) { return unregister_usb(n,d); }
static int wcd_mbhc_set_keycode(struct wcd_mbhc *m) { return 0; }
static int wcd_mbhc_initialise(struct wcd_mbhc *m) { assert(m->mbhc_started && m->mbhc_cfg); hardware_calls++; return fail_hardware ? -EIO : 0; }
static unsigned usecs_to_jiffies(unsigned x) { return x; }
static bool schedule_delayed_work(struct delayed_work *w,unsigned delay) { assert(w->work.ready); bool old=w->work.pending; w->work.pending=true; return !old; }
static bool schedule_work(struct work_struct *w) { assert(w->ready); bool old=w->pending; w->pending=true; return !old; }
static const struct firmware *new_fw(void) {
    struct firmware *fw=malloc(sizeof(*fw)); assert(fw);
    fw->data=malloc(32); assert(fw->data); fw->size=32; return fw;
}
static int cancel_delayed_work_sync(struct delayed_work *w) {
    assert(w->work.ready);
    bool was=w->work.pending;
    if(w==&cur->mbhc_firmware_dwork) {
        cancel_fw++;
        assert(!cur->mbhc_started && !codec_notifier && !usb_notifier);
        if(finish_firmware_on_cancel) {
            assert(!cur->mbhc_fw);
            cur->mbhc_fw=new_fw(); cur->mbhc_cfg->calibration=(void*)cur->mbhc_fw->data;
            finish_firmware_on_cancel=false;
        }
    } else { assert(w==&cur->mbhc_btn_dwork); cancel_btn++; }
    w->work.pending=false; return was;
}
static bool cancel_work_sync(struct work_struct *w) { assert(w->ready && !cur->codec_resource_lock.locked); cancel_detect++; bool old=w->pending; w->pending=false; return old; }
static int wcd_cancel_btn_work(struct wcd_mbhc *m) { int old=cancel_delayed_work_sync(&m->mbhc_btn_dwork); if(old)lock_sleep_stub(m,false); return old; }
static void release_firmware(const struct firmware *fw) {
    if(!fw)return;
    assert(!cur->mbhc_firmware_dwork.work.pending);
    if(cur->mbhc_cfg) assert(cur->mbhc_cfg->calibration!=fw->data);
    fw_released++; free((void*)fw->data); free((void*)fw);
}
static int request_firmware(const struct firmware **fw,const char *name,struct device *d) {
    *fw=new_fw();
    if(complete_fw_after_stop)WRITE_ONCE(cur->mbhc_started,false);
    return 0;
}
static void usleep_range(unsigned a,unsigned b) {}
static bool wcd_mbhc_fw_validate(const void *p,size_t len) { return true; }
static struct delayed_work *to_delayed_work(struct work_struct *w) { return container_of(w,struct delayed_work,work); }
'''

TESTS = r'''
static int cases;
static void reset(struct wcd_mbhc *m) {
    memset(m,0,sizeof(*m)); cur=m;
    for(int i=1;i<4;i++)assert(!nodes[i].refs);
    assert(!codec_notifier && !usb_notifier && !wake_votes);
    for(int i=1;i<8;i++)assert(!irq_owned[i]);
    memset(&extdev,0,sizeof(extdev));
    provider=fail_irq=irq_calls=jack_calls=fail_jack=key_calls=fail_key=0;
    fail_ext_alloc=fail_ext_reg=fail_notifier=fail_hardware=fail_usb=fail_prop=0;
    cancel_fw=cancel_btn=cancel_detect=fw_released=hardware_calls=0;
    irq_disabled=uart_present=irq_in_notifier=switches=skip_calls=0;
    refuse_wake=finish_firmware_on_cancel=complete_fw_after_stop=false;
    m->mbhc_detection_logic=WCD_DETECTION_ADC;
}
static int init(struct wcd_mbhc *m) { return wcd_mbhc_init(m,&component,&cb,&intr,&regs,true); }
static void clean(struct wcd_mbhc *m) {
    assert(!m->mbhc_initialized && !m->mbhc_started && !m->codec_resource_lock.ready);
    assert(!codec_notifier && !usb_notifier && !wake_votes);
    for(int i=1;i<8;i++)assert(!irq_owned[i]);
    for(int i=1;i<4;i++)assert(!nodes[i].refs);
}
int main(void) {
    struct wcd_mbhc m;
    unsigned char calibration[32]={0};
    struct wcd_mbhc_config cfg={.calibration=calibration};
    for(int f=0;f<14;f++) {
        reset(&m); provider=1;
        int expected=-EIO;
        if(f==0) { fail_prop=1; expected=-EINVAL; }
        else if(f<3) { fail_jack=f; expected=-ENOMEM; }
        else if(f==3) { fail_key=1; expected=-EINVAL; }
        else if(f==4) { fail_ext_alloc=1; expected=-ENOMEM; }
        else if(f==5) fail_ext_reg=1;
        else if(f==6) fail_notifier=1;
        else fail_irq=f-6;
        assert(init(&m)==expected);
        clean(&m);
        fail_prop=fail_jack=fail_key=fail_ext_alloc=fail_ext_reg=fail_notifier=fail_irq=0;
        assert(init(&m)==0); ready(); assert(m.mbhc_initialized);
        wcd_mbhc_deinit(&m); clean(&m); cases++;
    }
    reset(&m); m.mbhc_detection_logic=WCD_DETECTION_LEGACY;
    assert(init(&m)==-EINVAL); clean(&m); cases++;
    reset(&m); assert(wcd_mbhc_init(NULL,&component,&cb,&intr,&regs,true)==-EINVAL);
    assert(wcd_mbhc_init(&m,NULL,&cb,&intr,&regs,true)==-EINVAL); cases++;
    for(int p=0;p<4;p++) {
        reset(&m); provider=p; uart_present=1;
        cfg=(struct wcd_mbhc_config){.calibration=calibration};
        assert(init(&m)==0); assert(init(&m)==-EBUSY);
        assert(wcd_mbhc_mech_plug_detect_irq(1,&m)==IRQ_HANDLED && switches==0);
        int rc=wcd_mbhc_start(&m,&cfg);
        if(p==3)assert(rc==-EINVAL && !m.mbhc_started && !codec_notifier && !usb_notifier);
        else {
            assert(rc==0 && m.mbhc_started);
            int count=hardware_calls;
            assert(wcd_mbhc_start(&m,&cfg)==0 && hardware_calls==count);
            struct wcd_mbhc_config other=cfg;
            assert(wcd_mbhc_start(&m,&other)==-EBUSY);
            irq_in_notifier=1;
            wcd_mbhc_stop(&m);
            int disabled=irq_disabled;
            assert(cancel_fw && cancel_btn && cancel_detect);
            assert(!m.mbhc_started && !codec_notifier && !usb_notifier);
            assert(!nodes[3].refs && cfg.calibration==calibration);
            wcd_mbhc_stop(&m); assert(irq_disabled==disabled);
            assert(wcd_mbhc_start(&m,&cfg)==0 && m.mbhc_started);
        }
        wcd_mbhc_deinit(&m); clean(&m); wcd_mbhc_deinit(&m); clean(&m); cases++;
    }
    for(int p=1;p<=2;p++) {
        reset(&m); provider=p; uart_present=1;
        cfg=(struct wcd_mbhc_config){.calibration=calibration,.read_fw_bin=true};
        assert(init(&m)==0); fail_usb=1;
        assert(wcd_mbhc_start(&m,&cfg)==-EIO);
        assert(!m.mbhc_started && !codec_notifier && !usb_notifier && !m.mbhc_firmware_dwork.work.pending);
        assert(!nodes[3].refs);
        fail_usb=0; assert(wcd_mbhc_start(&m,&cfg)==0);
        assert(m.mbhc_firmware_dwork.work.pending && !m.mbhc_fw);
        finish_firmware_on_cancel=true;
        wcd_mbhc_stop(&m);
        assert(fw_released==1 && cfg.calibration==calibration && !m.mbhc_fw);
        wcd_mbhc_deinit(&m); clean(&m); cases++;
    }
    reset(&m); assert(init(&m)==0);
    cfg=(struct wcd_mbhc_config){.calibration=calibration};
    fail_hardware=1; assert(wcd_mbhc_start(&m,&cfg)==-EIO);
    assert(!m.mbhc_started && !codec_notifier);
    fail_hardware=0; fail_notifier=1;
    assert(wcd_mbhc_start(&m,&cfg)==-EIO);
    fail_notifier=0; assert(wcd_mbhc_start(&m,&cfg)==0);
    WCD_MBHC_RSC_LOCK(&m);
    refuse_wake=true; wcd_schedule_hs_detect_plug(&m,&m.correct_plug_swch);
    assert(!m.correct_plug_swch.pending && !wake_votes);
    refuse_wake=false; wcd_schedule_hs_detect_plug(&m,&m.correct_plug_swch);
    assert(m.correct_plug_swch.pending && wake_votes==1);
    wcd_schedule_hs_detect_plug(&m,&m.correct_plug_swch); assert(wake_votes==1);
    WCD_MBHC_RSC_UNLOCK(&m);
    m.mbhc_btn_dwork.work.pending=true; wake_votes++;
    wcd_mbhc_stop(&m); assert(!wake_votes);
    wcd_mbhc_deinit(&m); clean(&m); cases++;
    reset(&m); assert(init(&m)==0);
    cfg=(struct wcd_mbhc_config){.calibration=calibration};
    assert(wcd_mbhc_start(&m,&cfg)==0);
    complete_fw_after_stop=true;
    wcd_mbhc_fw_read(&m.mbhc_firmware_dwork.work);
    assert(fw_released==1 && !m.mbhc_fw && cfg.calibration==calibration);
    wcd_mbhc_deinit(&m); clean(&m); cases++;
    reset(&m); assert(init(&m)==0);
    cfg=(struct wcd_mbhc_config){.calibration=calibration};
    assert(wcd_mbhc_start(&m,&cfg)==0);
    wcd_mbhc_fw_read(&m.mbhc_firmware_dwork.work);
    assert(m.mbhc_fw && cfg.calibration==m.mbhc_fw->data);
    wcd_mbhc_deinit(&m); clean(&m);
    assert(fw_released==1 && cfg.calibration==calibration); cases++;
    reset(&m); assert(init(&m)==0);
    cfg=(struct wcd_mbhc_config){.calibration=calibration};
    assert(wcd_mbhc_start(&m,&cfg)==0); m.current_plug=1;
    wcd_mbhc_stop(&m); assert(skip_calls==1);
    wcd_mbhc_deinit(&m); clean(&m); assert(skip_calls==1); cases++;
    printf("MBHC lifecycle: %d scenarios passed\n",cases);
    return 0;
}
'''


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("source",nargs="?",type=Path,default=Path(__file__).parent/"private-source")
    ap.add_argument("--out",type=Path,default=Path(__file__).with_suffix(".json"))
    ap.add_argument("--cc",default=shutil.which("clang") or '/home/loukious/Android/konoha-android17-6.18-port-20261031/prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang')
    args=ap.parse_args()
    base=args.source/"qcom/opensource/audio-kernel/asoc/codecs"
    v2=(base/"wcd-mbhc-v2.c").read_text()
    adc=(base/"wcd-mbhc-adc.c").read_text()
    names=["wcd_mbhc_init_gpio","wcd_mbhc_unregister_notifiers","wcd_mbhc_start","wcd_mbhc_stop","wcd_mbhc_init","wcd_mbhc_deinit","wcd_mbhc_mech_plug_detect_irq","wcd_mbhc_fw_read"]
    header=(args.source/"qcom/opensource/audio-kernel/include/asoc/wcd-mbhc-v2.h").read_text()
    bodies={n:function(header,n) for n in ["wcd_mbhc_is_started","wcd_mbhc_set_started"]}
    bodies.update({n:function(v2,n) for n in names})
    bodies.update({n:function(adc,n) for n in ["wcd_schedule_hs_detect_plug","wcd_cancel_hs_detect_plug"]})
    cc=args.cc
    if not Path(cc).is_file(): raise RuntimeError("clang is required")
    with tempfile.TemporaryDirectory(prefix="mbhc-lifecycle-") as td:
        td=Path(td)
        src=td/"test.c"
        src.write_text(SERVICES+"\n"+"\n".join(bodies.values())+TESTS)
        build=subprocess.run([cc,"-std=gnu11","-g","-O1","-fsanitize=address,undefined","-fno-omit-frame-pointer",str(src),"-o",str(td/"test")],capture_output=True,text=True)
        if build.returncode:
            print(build.stderr)
            (Path(__file__).parent/"mbhc-lifecycle-failed.c").write_text(src.read_text())
            raise SystemExit(build.returncode)
        run=subprocess.run([str(td/"test")],capture_output=True,text=True,timeout=20)
        print(run.stdout,end="")
        if run.returncode:
            print(run.stderr)
            (Path(__file__).parent/"mbhc-lifecycle-failed.c").write_text(src.read_text())
            raise SystemExit(run.returncode)
        scenarios=int(re.search(r"(\d+) scenarios passed",run.stdout).group(1))
        mutations={
            "pending-firmware-not-cancelled":("wcd_mbhc_stop","\tcancel_delayed_work_sync(&mbhc->mbhc_firmware_dwork);","\tif (mbhc->mbhc_fw)\n\t\tcancel_delayed_work_sync(&mbhc->mbhc_firmware_dwork);"),
            "no-enqueue-gate":("wcd_schedule_hs_detect_plug","\tif (!mbhc->mbhc_started)\n\t\treturn;",""),
            "extcon-error-lost":("wcd_mbhc_init","\t\t\tret = PTR_ERR(extdev);","\t\t\tret = 0;"),
            "failed-init-notifier-leak":("wcd_mbhc_init","\twcd_mbhc_unregister_notifiers(mbhc);",""),
            "usbss-failure-not-unregistered":("wcd_mbhc_unregister_notifiers","\t\twcd_usbss_unreg_notifier(&mbhc->aatc_dev_nb,\n\t\t\t\t\t mbhc->wcd_usbss_aatc_dev_np);","\t\t(void)0;"),
            "calibration-not-restored":("wcd_mbhc_stop","\t\tmbhc->mbhc_cfg->calibration = mbhc->default_calibration;",""),
            "duplicate-wake-vote":("wcd_schedule_hs_detect_plug","\tif (!schedule_work(work))\n\t\tmbhc->mbhc_cb->lock_sleep(mbhc, false);","\tschedule_work(work);"),
            "mechanical-irq-before-start":("wcd_mbhc_mech_plug_detect_irq","\tif (!wcd_mbhc_is_started(mbhc))\n\t\treturn IRQ_HANDLED;",""),
        }
        controls={}
        for name,(fn,old,new) in mutations.items():
            assert bodies[fn].count(old)==1,(name,fn)
            changed=dict(bodies); changed[fn]=changed[fn].replace(old,new)
            src.write_text(SERVICES+"\n"+"\n".join(changed.values())+TESTS)
            build=subprocess.run([cc,"-std=gnu11","-g","-O1","-fsanitize=address,undefined","-fno-omit-frame-pointer",str(src),"-o",str(td/"test")],capture_output=True,text=True)
            if build.returncode: raise RuntimeError(f"Negative control did not compile: {name}\n{build.stderr}")
            run=subprocess.run([str(td/"test")],capture_output=True,text=True,timeout=20)
            assert run.returncode,("Negative control escaped",name)
            diagnostic=next((line for line in run.stderr.splitlines() if any(word in line for word in ['Assertion','runtime error:','ERROR: AddressSanitizer'])),None)
            assert diagnostic,(name,run.returncode,run.stderr)
            controls[name]={"result":"compiled and rejected","diagnostic":diagnostic}
        source_files=[base/"wcd-mbhc-v2.c",base/"wcd-mbhc-adc.c",args.source/"qcom/opensource/audio-kernel/include/asoc/wcd-mbhc-v2.h"]
        report={"result":"passed","scenarios":scenarios,"compiler":cc,"sanitizers":["address","undefined"],"functions":{n:hashlib.sha256(s.encode()).hexdigest() for n,s in bodies.items()},"negative_controls":controls,"source_sha256":{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files},"harness_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"limits":"Extracted production lifecycle functions with mocked IRQ/notifier/workqueue, ALSA/extcon, DT and firmware services. Mechanical IRQ entry and ADC scheduling/cancellation execute; other IRQ handlers, analog hardware helpers and detection algorithm are mocked. Lifecycle entrypoints require serialization by the codec owner. No complete kernel execution, physical audio, concurrent IRQ/DSP or provider-removal validation."}
        args.out.write_text(json.dumps(report,indent=2)+"\n")
        print(f"{len(controls)} compiled negative controls rejected")


if __name__=="__main__":main()
