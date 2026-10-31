#include "rmnet_mock.h"
#define __RMNET_MODULE_NUM_HOOKS 4
struct rmnet_module_hook_info { void *func; };
struct rmnet_module_hook_register_info { int hooknum;void *func; };
static struct rmnet_module_hook_info rmnet_module_hooks[__RMNET_MODULE_NUM_HOOKS];
static DEFINE_SPINLOCK(rmnet_hook_lock);
/* FUNCTIONS */
int main(void)
{
 int a,b;struct rmnet_module_hook_register_info entries[]={{-1,&a},{0,&a},{3,&a},{4,&a},{INT_MAX,&a},{INT_MIN,&a}};
 rmnet_module_hook_register(NULL,3);rmnet_module_hook_unregister_no_sync(NULL,3);
 rmnet_module_hook_register(entries,6);assert(rmnet_module_hook_is_set(0) && rmnet_module_hook_is_set(3));
 assert(!rmnet_module_hook_is_set(-1) && !rmnet_module_hook_is_set(INT_MIN) && !rmnet_module_hook_is_set(4));
 struct rmnet_module_hook_register_info replace={0,&b};rmnet_module_hook_register(&replace,1);
 rmnet_module_hook_unregister_no_sync(entries,6);assert(rmnet_module_hooks[0].func==&b && !rmnet_module_hook_is_set(3));
 rmnet_module_hook_unregister_no_sync(&replace,1);assert(!rmnet_module_hook_is_set(0) && !rmnet_hook_lock);
 puts("{\"negative_and_excess_hook_ids_rejected\":true,\"stale_unregister_retains_new_owner\":true,\"null_lists_safe\":true}");
}
