#include "cnss2_mock.h"
#define THERMAL_NAME_LENGTH 20
struct device_node { int refs; };
struct cnss_plat_data { struct mutex tcdev_lock,tcdev_op_lock;bool tcdev_stopping,stopping;struct list_head cnss_tcdev_list; };
struct device { struct cnss_plat_data *priv; };
struct thermal_cooling_device { void *devdata; };
struct thermal_cooling_device_ops { int (*get_max_state)(struct thermal_cooling_device *,unsigned long *);int (*get_cur_state)(struct thermal_cooling_device *,unsigned long *);int (*set_cur_state)(struct thermal_cooling_device *,unsigned long); };
/* THERMAL TYPE */
static struct device_node node;static bool no_node,no_cells,null_device;static int core_fail,bus_fail,removed,created,last_id;static struct cnss_plat_data *last_priv;
static struct cnss_plat_data *cnss_bus_dev_to_plat_priv(struct device *d) { return d?d->priv:NULL; }
static struct device_node *of_find_node_by_name(void *from,const char *name) { if(no_node)return NULL;node.refs++;return &node; }
static void *of_find_property(struct device_node *n,const char *name,void *p) { return no_cells?NULL:n; }
static void of_node_put(struct device_node *n) { assert(n && n->refs>0);n->refs--; }
static int cnss_bus_set_therm_cdev_state(struct cnss_plat_data *p,unsigned long state,int id) { last_priv=p;last_id=id;return bus_fail; }
static struct thermal_cooling_device *thermal_of_cooling_device_register(struct device_node*,const char*,void*,struct thermal_cooling_device_ops*);
static void thermal_cooling_device_unregister(struct thermal_cooling_device*);
static int cnss_tcdev_get_max_state(struct thermal_cooling_device *,unsigned long *);
static int cnss_tcdev_get_cur_state(struct thermal_cooling_device *,unsigned long *);
static int cnss_tcdev_set_cur_state(struct thermal_cooling_device *,unsigned long);
static struct thermal_cooling_device_ops cnss_cooling_ops={cnss_tcdev_get_max_state,cnss_tcdev_get_cur_state,cnss_tcdev_set_cur_state};
/* FUNCTIONS */
static struct thermal_cooling_device *thermal_of_cooling_device_register(struct device_node *n,const char *name,void *data,struct thermal_cooling_device_ops *ops)
{
 assert(n && n->refs);if(core_fail)return ERR_PTR(core_fail);if(null_device)return NULL;
 struct thermal_cooling_device *t=kzalloc(sizeof(*t),0);assert(t);t->devdata=data;unsigned long max,cur;
 assert(!ops->get_max_state(t,&max) && !ops->get_cur_state(t,&cur) && cur==0);
 assert(!ops->set_cur_state(t,0));created++;return t;
}
static void thermal_cooling_device_unregister(struct thermal_cooling_device *t)
{
 struct cnss_thermal_cdev *c=t->devdata;assert(!c->plat_priv->tcdev_lock.held);
 assert(!cnss_tcdev_set_cur_state(t,0));removed++;kfree(t);
}
int main(void)
{
 struct cnss_plat_data p={0},other={0};INIT_LIST_HEAD(&p.cnss_tcdev_list);INIT_LIST_HEAD(&other.cnss_tcdev_list);struct device dev={&p},dev2={&other};unsigned long state;
 assert(cnss_thermal_cdev_register(NULL,5,0)==-ENODEV);assert(cnss_thermal_cdev_register(&dev,5,-1)==-EINVAL);assert(cnss_thermal_cdev_register(&dev,5,2147483647)==-EINVAL);
 alloc_fault=alloc_step+1;assert(cnss_thermal_cdev_register(&dev,5,1)==-ENOMEM);alloc_fault=0;
 no_node=true;assert(cnss_thermal_cdev_register(&dev,5,1)==-EINVAL);no_node=false;
 no_cells=true;assert(cnss_thermal_cdev_register(&dev,5,1)==-EOPNOTSUPP);no_cells=false;
 core_fail=-EIO;assert(cnss_thermal_cdev_register(&dev,5,1)==-EIO);core_fail=0;
 null_device=true;assert(cnss_thermal_cdev_register(&dev,5,1)==-ENOMEM);null_device=false;
 assert(!node.refs && !live);
 assert(!cnss_thermal_cdev_register(&dev,5,1));assert(!cnss_thermal_cdev_register(&dev,9,2));assert(!cnss_thermal_cdev_register(&dev2,9,1));
 assert(cnss_thermal_cdev_register(&dev,5,1)==-EEXIST);
 struct cnss_thermal_cdev *c=list_first_entry(&p.cnss_tcdev_list,struct cnss_thermal_cdev,tcdev_list);
 assert(cnss_tcdev_set_cur_state(c->tcdev,6)==-EINVAL);assert(!cnss_tcdev_set_cur_state(c->tcdev,4));assert(last_priv==&p && last_id==1);
 bus_fail=-EIO;assert(cnss_tcdev_set_cur_state(c->tcdev,3)==-EIO);bus_fail=0;assert(!cnss_get_curr_therm_cdev_state(&dev,&state,1) && state==4);
 assert(cnss_get_curr_therm_cdev_state(&dev,NULL,1)==-EINVAL);
 cnss_thermal_cdev_unregister(&dev,99);assert(!removed);cnss_thermal_cdev_unregister(&dev,1);assert(removed==1);
 assert(cnss_get_curr_therm_cdev_state(&dev,&state,1)==-EINVAL);assert(!cnss_get_curr_therm_cdev_state(&dev,&state,2));
 assert(!cnss_get_curr_therm_cdev_state(&dev2,&state,1));
 cnss_thermal_cdev_unregister_all(&p);cnss_thermal_cdev_unregister_all(&other);assert(created==removed && !live && !node.refs);
 assert(cnss_thermal_cdev_register(&dev,5,7)==-ESHUTDOWN);
 puts("{\"thermal_failure_cases\":8,\"per_device_and_id_ownership\":true,\"core_callbacks_outside_list_lock\":true,\"failed_bus_update_retains_state\":true,\"node_refs_balanced\":true}");
}
