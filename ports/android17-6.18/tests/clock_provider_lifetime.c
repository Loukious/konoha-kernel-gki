/* Exact provider functions are generated from the selected staged package. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdalign.h>

#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define DEFINE_MUTEX(n) int n
#define DEFINE_SPINLOCK(n) int n
#define BIT(n) (1UL << (n))
#define QCOM_CLK_IS_CRITICAL BIT(0)
#define QCOM_CLK_BOOT_CRITICAL BIT(1)
#define __iomem
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x) = (v))
#define struct_size(p,m,n) (sizeof(*(p)) + sizeof((p)->m[0]) * (n))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_err(...) (++reported_errors)
#define WARN(c,...) (!!(c))
#define WARN_ON(c) (!!(c))
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define spin_lock(p) ((void)(p))
#define spin_unlock(p) ((void)(p))
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(n) struct list_head n = { &n, &n }
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define list_for_each_entry(p,h,m) for (p=container_of((h)->next,__typeof__(*p),m); &(p)->m!=(h); p=container_of((p)->m.next,__typeof__(*p),m))
static void list_add(struct list_head *n, struct list_head *h) { n->next=h->next; n->prev=h; h->next->prev=n; h->next=n; }
static void list_del(struct list_head *n) { n->prev->next=n->next; n->next->prev=n->prev; }
static void list_del_init(struct list_head *n) { list_del(n); n->next=n->prev=n; }
static bool list_empty(const struct list_head *h) { return h->next == h; }

struct device; struct clk_hw;
struct module { bool live; int refs; };
static struct module provider_module = {true,0};
#define THIS_MODULE (&provider_module)
static bool module_is_live(struct module *m) { return m->live; }
static void __module_get(struct module *m) { ++m->refs; }
struct device_driver { struct module *owner; };
struct resource { void (*release)(struct device *,void *); max_align_t alignment; unsigned char payload[]; };
struct regulator { int handles, enables, uv; };
struct icc_path { int handles, bw; };
struct clk { int handles, enabled; };
struct device {
 struct device_driver *driver; struct device *parent; void *drvdata; void *of_node;
 struct resource *resources[1024]; size_t nr;
 struct regulator regulators[4]; struct icc_path path; struct clk iface;
 int refs, pm_usage; bool pm_enabled, pm_active;
};
struct platform_device { struct device dev; };
struct clk_ops { int (*enable)(struct clk_hw *); void (*terminate)(struct clk_hw *); };
struct clk_init_data { const char *name; const struct clk_ops *ops; unsigned long flags; };
struct clk_hw { const struct clk_ops *registered_ops; const struct clk_init_data *init; void *core; struct clk *clk; bool registered, boot_hold; };
struct regmap { int unused; };
struct clk_vdd_class { const char *class_name; struct regulator **regulator; const char **regulator_names; int num_regulators; int *vdd_uv,*level_votes; int num_levels,cur_level; };
struct clk_vdd_class_data { struct clk_vdd_class *vdd_class, **vdd_classes; unsigned long *rate_max; int num_rate_max,vdd_level,num_vdd_classes; };
struct clk_regmap { struct clk_hw hw; struct regmap *regmap; struct device *dev; unsigned long flags; struct clk_vdd_class_data vdd_data; struct list_head list_node; };
struct qcom_cc_desc { struct clk_regmap **clks; size_t num_clks; struct clk_hw **clk_hws; size_t num_clk_hws; struct clk_vdd_class **clk_regulators; size_t num_clk_regulators; struct icc_path *path; size_t num_resets,num_gdscs; void *resets,*gdscs; };
struct qcom_reset_controller { struct device *dev; struct { void *of_node,*ops; struct module *owner; size_t nr_resets; } rcdev; struct regmap *regmap; void *reset_map; };
struct gdsc_desc { struct device *dev; void *scs; size_t num; };
struct qcom_cc { struct qcom_cc_lifetime *life; struct qcom_reset_controller reset; struct clk_regmap **rclks; size_t num_rclks; struct clk_hw **clk_hws; size_t num_clk_hws; };
static int reported_errors, alloc_calls, fail_alloc, op_calls, fail_op, fail_op_second;
static int register_calls, fail_register, provider_fail, debug_fail;
static struct device *pm_get_fail_device;
static bool pm_put_positive;
static int registered_clocks, debug_entries, sync_calls, writes_after_release;
static int qcom_reset_ops, protect_index=-1, critical_index=-1;
static struct regmap map;
static bool fail_now(void) { ++op_calls; return op_calls == fail_op || op_calls == fail_op_second; }
static void *kzalloc(size_t n,int flags) { (void)flags; if (++alloc_calls==fail_alloc) return NULL; return calloc(1,n?n:1); }
static void *kcalloc(size_t n,size_t size,int flags) { return kzalloc(n*size,flags); }
static void kfree(const void *p) { free((void *)p); }
static void *devres_alloc(void (*cb)(struct device *,void *),size_t n,int flags) { struct resource *r=kzalloc(sizeof(*r)+n,flags); if (!r) return NULL; r->release=cb; return r->payload; }
static struct resource *res_header(void *p) { return container_of(p,struct resource,payload); }
static void devres_free(void *p) { free(res_header(p)); }
static void devres_add(struct device *d,void *p) { assert(d->nr<1024); d->resources[d->nr++]=res_header(p); }
static void free_all(struct device *d) { struct resource *local[1024]; size_t n=d->nr; memcpy(local,d->resources,n*sizeof(*local)); d->nr=0; while(n) { struct resource *r=local[--n]; if(r->release)r->release(d,r->payload);free(r); } }
static void free_memory(struct device *d,void *p) { (void)d; free(*(void **)p); }
static void *devm_kzalloc(struct device *d,size_t n,int flags) { void *p=kzalloc(n,flags), **r; if(!p)return NULL;r=devres_alloc(free_memory,sizeof(*r),flags);if(!r){free(p);return NULL;}*r=p;devres_add(d,r);return p; }
static void *devm_kmemdup(struct device *d,const void *p,size_t n,int flags) { void *r=devm_kzalloc(d,n,flags);if(r&&n)memcpy(r,p,n);return r; }
struct action { void (*cb)(void *); void *data; };
static void release_action(struct device *d,void *p) { struct action *a=p;(void)d;a->cb(a->data); }
static int devm_add_action_or_reset(struct device *d,void (*cb)(void *),void *data) { struct action *a=devres_alloc(release_action,sizeof(*a),0);if(!a){cb(data);return -ENOMEM;}a->cb=cb;a->data=data;devres_add(d,a);return 0; }
static void *devres_open_group(struct device *d,void *id,int flags) { void *r=devres_alloc(NULL,1,flags);(void)id;if(r)devres_add(d,r);return r; }
static size_t group_index(struct device *d,void *g) {size_t i;for(i=0;i<d->nr;i++)if(d->resources[i]==res_header(g))return i;abort();}
static void devres_remove_group(struct device *d,void *g) {size_t i=group_index(d,g);free(d->resources[i]);memmove(d->resources+i,d->resources+i+1,(--d->nr-i)*sizeof(*d->resources));}
static void devres_release_group(struct device *d,void *g) {size_t i=group_index(d,g);while(d->nr>i){struct resource *r=d->resources[--d->nr];if(r->release)r->release(d,r->payload);free(r);}}
static struct device *get_device(struct device *d) {++d->refs;return d;}
static void *dev_get_drvdata(struct device *d) { return d->drvdata; }
static void dev_set_drvdata(struct device *d,void *p) { d->drvdata=p; }
static void platform_set_drvdata(struct platform_device *p,void *data) { p->dev.drvdata=data; }
static struct regulator *regulator_get(struct device *d,const char *name) { struct regulator *r=&d->regulators[name[0]-'0'];if(fail_now())return ERR_PTR(-EAGAIN);++r->handles;return r; }
static void regulator_put(struct regulator *r) { assert(r && r->handles>0);--r->handles; }
static int regulator_set_voltage(struct regulator *r,int uv,int max) {assert(r&&r->handles>0);(void)max;if(fail_now())return -EIO;r->uv=uv;return 0;}
static int regulator_enable(struct regulator *r) {assert(r&&r->handles>0);if(fail_now())return -EIO;++r->enables;return 0;}
static int regulator_disable(struct regulator *r) {assert(r&&r->handles>0&&r->enables>0);if(fail_now())return -EIO;--r->enables;return 0;}
static struct icc_path *of_icc_get(struct device *d,void *n) {(void)n;if(fail_now())return ERR_PTR(-EAGAIN);++d->path.handles;return &d->path;}
static int icc_set_bw(struct icc_path *p,int avg,int peak) {assert(p&&p->handles>0);(void)avg;if(fail_now())return -EIO;p->bw=peak;return 0;}
static void icc_put(struct icc_path *p) {if(p){assert(p->handles>0);--p->handles;}}
static struct clk *clk_get_optional(struct device *d,const char *n) {(void)n;if(fail_now())return ERR_PTR(-EAGAIN);++d->iface.handles;return &d->iface;}
static void clk_put(struct clk *c) {if(c){assert(c->handles>0);--c->handles;}}
static int clk_prepare_enable(struct clk *c) {if(fail_now())return -EIO;if(c)++c->enabled;return 0;}
static void clk_disable_unprepare(struct clk *c) {if(c){assert(c->enabled>0);--c->enabled;}}
static int qcom_cc_runtime_resume(struct device *d);
static int qcom_cc_runtime_suspend(struct device *d);
static void pm_runtime_enable(struct device *d) {assert(!d->pm_enabled);d->pm_enabled=true;}
static void pm_runtime_disable(struct device *d) {assert(d->pm_enabled);d->pm_enabled=false;}
static int pm_runtime_resume_and_get(struct device *d) {int ret=0;assert(d->pm_enabled);if(d==pm_get_fail_device)return -EIO;if(!d->pm_active)ret=qcom_cc_runtime_resume(d);if(!ret){d->pm_active=true;++d->pm_usage;}return ret;}
static void pm_runtime_put_noidle(struct device *d) {assert(d->pm_usage>0);--d->pm_usage;}
static int pm_runtime_put_sync(struct device *d) {int ret=0;assert(d->pm_usage>0);if(!--d->pm_usage){ret=qcom_cc_runtime_suspend(d);if(!ret)d->pm_active=false;}return !ret&&pm_put_positive?1:ret;}
static bool pm_runtime_enabled(struct device *d) {return d->pm_enabled;}
static struct regmap *dev_get_regmap(struct device *d,void *id) {(void)d;(void)id;return &map;}
static struct clk_hw *registered_hw[64];
static void ccf_release(struct device *d,void *p) {struct clk_hw *hw=*(struct clk_hw **)p;assert(hw->registered);assert(!hw->boot_hold);assert(qcom_cc_runtime_resume(d)==0 || !d->pm_enabled);if(d->pm_enabled)assert(qcom_cc_runtime_suspend(d)==0);if(hw->registered_ops&&hw->registered_ops->terminate)hw->registered_ops->terminate(hw);hw->registered=false;--registered_clocks;}
static int devm_clk_hw_register(struct device *d,struct clk_hw *hw) {struct clk_hw **r;assert(hw->init);++register_calls;r=devres_alloc(ccf_release,sizeof(*r),0);if(!r)return -ENOMEM;hw->registered_ops=hw->init->ops;hw->init=NULL;if(register_calls==fail_register){devres_free(r);return -EAGAIN;}hw->core=hw;hw->registered=true;hw->boot_hold=true;registered_hw[registered_clocks++]=hw;*r=hw;devres_add(d,r);return 0;}
static void clk_sync_state(struct device *d) {int i;(void)d;++sync_calls;for(i=0;i<registered_clocks;i++)registered_hw[i]->boot_hold=false;}
static void debug_release(void *p) {(void)p;assert(debug_entries>0);--debug_entries;}
static int clk_hw_debug_register(struct device *d,struct clk_hw *hw) {if(debug_fail)return -ENOMEM;++debug_entries;return devm_add_action_or_reset(d,debug_release,hw);}
static int devm_reset_controller_register(struct device *d,void *r) {(void)d;(void)r;return 0;}
static int gdsc_register(struct gdsc_desc *d,void *r,struct regmap *m) {(void)d;(void)r;(void)m;return 0;}
static void qcom_cc_gdsc_unregister(void *p) {(void)p;}
static void qcom_cc_drop_protected(struct device *d,struct qcom_cc *cc) {(void)d;if(protect_index>=0)cc->rclks[protect_index]=NULL;}
static void qcom_cc_set_critical(struct device *d,struct qcom_cc *cc) {(void)d;if(critical_index>=0&&cc->rclks[critical_index])cc->rclks[critical_index]->flags|=QCOM_CLK_IS_CRITICAL;}
static void clk_hw_populate_clock_opp_table(void *n,struct clk_hw *hw) {(void)n;(void)hw;}
static void *qcom_cc_clk_hw_get;
static int devm_of_clk_add_hw_provider(struct device *d,void *get,struct qcom_cc *cc) {(void)d;(void)get;(void)cc;return provider_fail?-EAGAIN:0;}
/* The real kernel's of_node field is not used by these dependency fakes. */
#include "vdd.inc"
#include "owner.inc"
#include "regmap.inc"
#include "registration.inc"
#include "runtime.inc"

typedef uint32_t u32;
#define DFS_SUPPORT BIT(0)
#define CLK_GET_RATE_NOCACHE BIT(1)
#define SE_CMD_DFSR_OFFSET 0x14
#define SE_CMD_DFS_EN BIT(0)
#define MAX_PERF_LEVEL 8
struct freq_tbl { unsigned long freq; };
struct clk_rcg2 {struct clk_regmap clkr; unsigned long flags; const struct freq_tbl *freq_tbl; unsigned int cmd_rcgr;};
struct clk_rcg_dfs_data {struct clk_rcg2 *rcg; struct clk_init_data *init;};
static struct clk_rcg2 *to_clk_rcg2(struct clk_hw *hw) {return container_of(hw,struct clk_rcg2,clkr.hw);}
static int dfs_read_calls,dfs_read_fail,dfs_entry_fail=-1;
static int regmap_read(struct regmap *map,unsigned int offset,u32 *value) {(void)map;(void)offset;if(++dfs_read_calls==dfs_read_fail)return -EIO;*value=SE_CMD_DFS_EN;return 0;}
static int clk_rcg2_dfs_populate_freq(struct clk_hw *hw,unsigned int level,struct freq_tbl *entry) {(void)hw;if((int)level==dfs_entry_fail)return -EIO;entry->freq=1000+level;return 0;}
#include "dfs.inc"
static int setup_writes,setup_write_fail,setup_fixup_fail,setup_map_fail;
static int regmap_update_bits(struct regmap *m,unsigned int r,unsigned int mask,unsigned int val) {(void)m;(void)r;(void)mask;(void)val;return ++setup_writes==setup_write_fail?-EIO:0;}
static struct regmap *qcom_cc_map(struct platform_device *p,const struct qcom_cc_desc *d) {(void)p;(void)d;return setup_map_fail?ERR_PTR(-EIO):&map;}
static int disp_cc_tuna_fixup(struct platform_device *p,struct regmap *m) {(void)p;(void)m;return setup_fixup_fail?-EIO:0;}
#define dev_info(...) ((void)0)
#define dev_err_probe(d,r,...) (r)
#define clk_rivian_ole_pll_configure(a,b,c) ((void)(a),(void)(b),(void)(c))
#define clk_lucid_ole_pll_configure(a,b,c) ((void)(a),(void)(b),(void)(c))
#define clk_pongo_ole_pll_configure(a,b,c) ((void)(a),(void)(b),(void)(c))
#include "leaves.inc"

struct fixture {struct platform_device p;struct module mod;struct device_driver driver;struct clk_vdd_class classes[2];struct clk_vdd_class *classptr[2];struct regulator *regs[2][1];const char *names[2][1];int uv[2][3],votes[2][3];struct clk_regmap clocks[3];struct clk_regmap *clkptr[3];struct clk_init_data init[3];struct qcom_cc_desc desc;};
static void init_fixture(struct fixture *f) {int i;memset(f,0,sizeof(*f));f->mod.live=true;f->driver.owner=&f->mod;f->p.dev.driver=&f->driver;for(i=0;i<2;i++){f->classptr[i]=&f->classes[i];f->names[i][0]=i?"1":"0";f->uv[i][1]=100;f->uv[i][2]=200;f->classes[i]=(struct clk_vdd_class){.class_name="test",.regulator=f->regs[i],.regulator_names=f->names[i],.num_regulators=1,.vdd_uv=f->uv[i],.level_votes=f->votes[i],.num_levels=3,.cur_level=3};}for(i=0;i<3;i++){f->init[i].name="clock";f->clocks[i].hw.init=&f->init[i];f->clkptr[i]=&f->clocks[i];}f->desc=(struct qcom_cc_desc){.clks=f->clkptr,.num_clks=3,.clk_regulators=f->classptr,.num_clk_regulators=2};alloc_calls=op_calls=register_calls=sync_calls=reported_errors=0;fail_alloc=fail_op=fail_op_second=fail_register=provider_fail=debug_fail=0;protect_index=critical_index=-1;pm_get_fail_device=NULL;pm_put_positive=false;setup_writes=setup_write_fail=setup_fixup_fail=setup_map_fail=0;}
static void assert_clean(struct fixture *f) {int i,j;assert(!f->p.dev.nr&&!f->p.dev.pm_enabled&&!f->p.dev.pm_usage&&!f->p.dev.drvdata&&!f->desc.path);assert(!registered_clocks&&!debug_entries&&list_empty(&clk_regmap_list));assert(!qcom_cc_lifetime_find(&f->p.dev));for(i=0;i<2;i++){assert(!f->p.dev.regulators[i].handles&&!f->p.dev.regulators[i].enables&&!f->regs[i][0]);for(j=0;j<3;j++)assert(!f->votes[i][j]);assert(f->classes[i].cur_level==3);}assert(!f->p.dev.path.handles&&!f->p.dev.path.bw&&!f->p.dev.iface.handles&&!f->p.dev.iface.enabled);for(i=0;i<3;i++){assert(f->clkptr[i]==&f->clocks[i]);assert(f->clocks[i].hw.init==&f->init[i]);assert(!f->clocks[i].hw.registered&&!f->clocks[i].flags);}}
static int probe(struct fixture *f) {int ret=qcom_cc_runtime_acquire(&f->p,&f->desc);if(ret)return ret;return qcom_cc_really_probe(&f->p,&f->desc,&map);}
static void cleanup(struct fixture *f) {fail_op=fail_op_second=fail_alloc=0;free_all(&f->p.dev);assert_clean(f);f->p.dev.pm_active=false;}
static void successful_cycle(struct fixture *f) {assert(probe(f)==0);assert(f->p.dev.pm_usage==1);qcom_cc_sync_state(&f->p.dev,&f->desc);qcom_cc_sync_state(&f->p.dev,&f->desc);assert(qcom_cc_runtime_release(&f->p)==0);assert(qcom_cc_runtime_release(&f->p)==0);cleanup(f);}
static void test_release_quarantine(struct device *dev)
{
 struct qcom_cc_quarantine *q, *found=NULL;
 unsigned int i;
 list_for_each_entry(q,&qcom_cc_quarantines,node)if(q->dev==dev){found=q;break;}
 assert(found&&found->error<0&&found->dev->refs==1);
 for(i=0;i<found->num_regulators;i++) {found->regulators[i]->enables=0;regulator_put(found->regulators[i]);}
 if(found->path){found->path->bw=0;icc_put(found->path);}
 --found->dev->refs;list_del(&found->node);free(found);--provider_module.refs;
}
static void test_quarantine(void)
{
 struct fixture f,other;struct regulator *r[2]={0};const char *names[2]={"0","1"};int uv[6]={0,0,100,100,200,200};
 init_fixture(&f);f.desc.num_clk_regulators=1;f.classes[0].num_regulators=2;f.classes[0].regulator=r;f.classes[0].regulator_names=names;f.classes[0].vdd_uv=uv;
 fail_op=7;fail_op_second=8;assert(qcom_cc_runtime_acquire(&f.p,&f.desc)==-EIO);assert(f.classes[0].cur_level==-1);fail_op=fail_op_second=0;free_all(&f.p.dev);
 assert(reported_errors==1&&provider_module.refs==1);assert(!qcom_cc_lifetime_find(&f.p.dev));assert(!f.p.dev.drvdata&&!f.desc.path);assert(qcom_cc_runtime_acquire(&f.p,&f.desc)==-EIO);
 init_fixture(&other);successful_cycle(&other);test_release_quarantine(&f.p.dev);
 init_fixture(&f);assert(probe(&f)==0);qcom_cc_lifetime_find(&f.p.dev)->exposed=true;free_all(&f.p.dev);assert(!f.clocks[0].hw.init);assert(!registered_clocks&&!debug_entries&&list_empty(&clk_regmap_list));assert(qcom_cc_runtime_acquire(&f.p,&f.desc)==-EBUSY);init_fixture(&other);assert(qcom_cc_runtime_acquire(&other.p,&f.desc)==-EBUSY);assert(!qcom_cc_lifetime_find(&other.p.dev));test_release_quarantine(&f.p.dev);
 init_fixture(&f);assert(probe(&f)==0);qcom_cc_lifetime_find(&f.p.dev)->exposed=true;f.mod.live=false;cleanup(&f);
 assert(!provider_module.refs&&list_empty(&qcom_cc_quarantines));
}
static void test_dfs(void)
{
 struct fixture f;struct clk_rcg2 rcg[2];struct clk_rcg_dfs_data data[2];struct clk_init_data init[2];const struct freq_tbl original[2]={{123},{456}};int i,n,ret;
 for(n=0;n<20;n++) {
  init_fixture(&f);memset(rcg,0,sizeof(rcg));memset(init,0,sizeof(init));f.desc.num_clk_regulators=0;f.desc.num_clks=2;
  for(i=0;i<2;i++){init[i].name="dfs";init[i].flags=8;rcg[i].clkr.hw.init=&init[i];rcg[i].freq_tbl=original;rcg[i].flags=4;f.clkptr[i]=&rcg[i].clkr;data[i]=(struct clk_rcg_dfs_data){&rcg[i],&init[i]};}
  dfs_read_calls=0;dfs_read_fail=n<3?n:0;fail_alloc=n>=3?n-2:0;
  ret=qcom_cc_register_rcg_dfs_managed(&f.p.dev,&f.desc,&map,data,2);
  fail_alloc=0;dfs_read_fail=0;
  if(!ret){assert(qcom_cc_really_probe(&f.p,&f.desc,&map)==0);assert(rcg[0].freq_tbl==NULL);for(i=0;i<MAX_PERF_LEVEL;i++){dfs_entry_fail=i;assert(clk_rcg2_dfs_populate_freq_table(&rcg[0])==-EIO);assert(!rcg[0].freq_tbl);}dfs_entry_fail=-1;assert(clk_rcg2_dfs_populate_freq_table(&rcg[0])==0);assert(rcg[0].freq_tbl[7].freq==1007);}
  free_all(&f.p.dev);for(i=0;i<2;i++){assert(rcg[i].freq_tbl==original&&rcg[i].flags==4&&init[i].flags==8&&init[i].ops==NULL&&rcg[i].clkr.hw.init==&init[i]);}
  assert(!registered_clocks&&!debug_entries&&list_empty(&clk_regmap_list));assert(!qcom_cc_lifetime_find(&f.p.dev));
 }
 puts("PASS DFS: managed setup allocation/read failures, table-population failure/retry, CCF termination and template restoration");
}
static void test_nested_pm(void)
{
 struct fixture parent,child;struct clk_regmap rclk={0};struct qcom_cc_desc shared;int ret;
 init_fixture(&parent);init_fixture(&child);child.p.dev.parent=&parent.p.dev;rclk.dev=&child.p.dev;
 assert(qcom_cc_runtime_init(&parent.p,&parent.desc)==0);shared=parent.desc;shared.clks=child.clkptr;assert(qcom_cc_runtime_init(&child.p,&shared)==-EBUSY);assert(qcom_cc_runtime_init(&child.p,&child.desc)==0);
 pm_get_fail_device=&child.p.dev;ret=clk_runtime_get_regmap(&rclk);assert(ret==-EIO&&!parent.p.dev.pm_usage&&!child.p.dev.pm_usage);
 pm_get_fail_device=&parent.p.dev;assert(clk_runtime_get_regmap(&rclk)==-EIO&&!parent.p.dev.pm_usage&&!child.p.dev.pm_usage);
 pm_get_fail_device=NULL;assert(clk_runtime_get_regmap(&rclk)==0&&parent.p.dev.pm_usage==1&&child.p.dev.pm_usage==1);clk_runtime_put_regmap(&rclk);cleanup(&child);cleanup(&parent);
}
static void test_leaf_probes(void)
{
 struct fixture f;int i,n,ret;
 int (*probes[])(struct platform_device *)={cam_bist_mclk_cc_tuna_probe,cam_cc_tuna_probe,disp_cc_tuna_probe,eva_cc_tuna_probe,video_cc_tuna_probe};
 struct qcom_cc_desc *descs[]={&cam_bist_mclk_cc_tuna_desc,&cam_cc_tuna_desc,&disp_cc_tuna_desc,&eva_cc_tuna_desc,&video_cc_tuna_desc};
 const int writes[]={1,4,5,6,3};
 for(i=0;i<5;i++)for(n=0;n<=writes[i];n++) {
  init_fixture(&f);*descs[i]=f.desc;setup_write_fail=n;ret=probes[i](&f.p);assert(n?ret==-EIO:ret==0);assert(setup_writes==(n?n:writes[i]));cleanup(&f);assert(!descs[i]->path);
 }
 for(i=0;i<5;i++){init_fixture(&f);*descs[i]=f.desc;pm_put_positive=true;assert(probes[i](&f.p)==0);pm_put_positive=false;cleanup(&f);}
 init_fixture(&f);disp_cc_tuna_desc=f.desc;setup_fixup_fail=1;assert(disp_cc_tuna_probe(&f.p)==-EIO);cleanup(&f);
 puts("PASS leaves: every native setup write failure, successful positive PM put, display fixup failure and cleanup");
}
int main(void) {struct fixture f;int n,ret;
 init_fixture(&f);successful_cycle(&f);successful_cycle(&f);
 init_fixture(&f);fail_op=7;assert(qcom_cc_runtime_acquire(&f.p,&f.desc)==-EIO);assert(!f.p.dev.regulators[0].enables&&!f.p.dev.regulators[1].enables);cleanup(&f);
 for(n=1;n<=65;n++){init_fixture(&f);fail_alloc=n;ret=probe(&f);(void)ret;cleanup(&f);successful_cycle(&f);}
 for(n=1;n<=20;n++){init_fixture(&f);fail_op=n;ret=probe(&f);if(n<=12)assert(ret<0);cleanup(&f);successful_cycle(&f);}
 for(n=1;n<=3;n++){init_fixture(&f);fail_register=n;assert(probe(&f)==-EAGAIN);cleanup(&f);fail_register=0;successful_cycle(&f);}
 init_fixture(&f);provider_fail=1;assert(probe(&f)==-EAGAIN);cleanup(&f);provider_fail=0;successful_cycle(&f);
 init_fixture(&f);debug_fail=1;assert(probe(&f)==-ENOMEM);cleanup(&f);debug_fail=0;successful_cycle(&f);
 init_fixture(&f);protect_index=0;critical_index=1;assert(probe(&f)==0);assert(f.clkptr[0]&&f.clkptr[1]);cleanup(&f);
 init_fixture(&f);assert(qcom_cc_runtime_acquire(&f.p,&f.desc)==0);op_calls=0;fail_op=1;assert(qcom_cc_runtime_release(&f.p)==-EIO);assert(f.p.dev.pm_usage==0);assert(qcom_cc_lifetime_find(&f.p.dev)->bw_voted);cleanup(&f);
 init_fixture(&f);assert(qcom_cc_runtime_acquire(&f.p,&f.desc)==0);assert(qcom_cc_lifetime_get(&f.p.dev,&f.desc)==qcom_cc_lifetime_find(&f.p.dev));cleanup(&f);
 test_nested_pm();test_leaf_probes();test_dfs();test_quarantine();assert(list_empty(&qcom_cc_lifetimes));puts("PASS provider: acquisition, allocation/registration faults, detached devres callbacks, retry, votes, descriptors and probe references");return 0;}
