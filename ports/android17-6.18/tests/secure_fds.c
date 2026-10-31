#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef errno

typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
#define __user
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) >= -4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define GFP_KERNEL 0
#define U64_MAX UINT64_MAX
#define UINT_MAX UINT32_MAX
#define ALIGN(v, a) (((v) + (a) - 1) & ~((typeof(v))(a) - 1))
#define u64_to_user_ptr(v) ((void *)(uintptr_t)(v))
#define _IOC_SIZE(c) sizeof(struct smcinvoke_cmd_req)
#define OBJECT_COUNTS_TOTAL(c) (((c) & 15) + (((c) >> 4) & 15) + (((c) >> 8) & 15) + (((c) >> 12) & 15))
#define OBJECT_COUNTS_PACK(a,b,c,d) ((a) | ((b) << 4) | ((c) << 8) | ((d) << 12))
#define SI_AT_END 0
#define SI_AT_IB 1
#define SI_AT_OB 2
#define SI_AT_IO 3
#define SI_AT_OO 4
#define SI_OT_NULL 0
#define SI_OT_CB_OBJECT 1
#define SI_OT_USER 2
#define SI_OT_ROOT 3
#define SMCINVOKE_USERSPACE_OBJ_NULL (-1)
#define U_HANDLE_IS_OBJECT(v) ((v) >= 0)
#define U_HANDLE_IS_NULL(v) ((v) == -1)
#define NULL_SI_OBJECT NULL
#define OBJECT_ERROR_INVALID (-1)
#define IClientEnv_OP_notifyDomainChange 1
#define IClientEnv_OP_adciAccept 2
#define IClientEnv_OP_adciShutdown 3
#define IClientEnv_OP_registerWithCredentials 4
#define ERESTARTSYS 512
#define XST_PROCESSED 3
struct file_operations { int unused; };
struct si_object { int type, refs, memory; struct file *dma_file; };
struct file { int refs; const struct file_operations *f_op; void *private_data; };
struct dma_buf { struct file *file; };
struct server_info { int unused; };
struct cb_object { struct si_object object; s64 u_handle; struct server_info *si; int notify_on_release; };
struct si_arg { int type; union { struct { void *addr; size_t size; } b; struct si_object *o; }; };
union smcinvoke_arg { struct { u64 addr, size; } b; struct { s64 fd, cb_server_fd; } o; };
struct smcinvoke_cmd_req { unsigned op, counts, argsize; u64 args; int result; };
struct smcinvoke_accept { u64 buf_addr, buf_len, txn_id; unsigned argsize, has_resp; int result; s64 cbobj_id; unsigned op, counts; };
struct si_object_invoke_ctx { int unused; };
struct cb_txn { struct si_arg *args; union smcinvoke_arg *u_args; size_t u_args_sz; unsigned op, context_id; s64 u_handle; int errno, completion; };
static const struct file_operations qtee_fops = {}, server_fops = {};
static struct file *table[128];
static bool reserved[128];
static unsigned fd_steps, anon_steps, copy_steps, fail_fd, fail_anon, fail_copy, objects;
static unsigned object_puts, installs, cases;
static struct si_object object_pool[15];
static struct dma_buf dma_pool[15];
static struct cb_txn callback;
static bool picked;
static unsigned delivered;
static int typeof_si_object(struct si_object *o) { return o ? o->type : SI_OT_NULL; }
static bool is_mem_object(struct si_object *o) { return o && o->memory == 1; }
static bool is_cb_object(struct si_object *o) { return o && o->type == SI_OT_CB_OBJECT && o->memory == 0; }
static struct cb_object *to_cb_object(struct si_object *o) { return (void *)o; }
static const char *si_object_name(struct si_object *o) { (void)o; return "object"; }
static void put_si_object(struct si_object *o) { if (o) { assert(o->refs > 0); o->refs--; object_puts++; } }
static struct dma_buf *mem_object_to_dma_buf(struct si_object *o) { return o->dma_file ? &dma_pool[o - object_pool] : NULL; }
static int get_unused_fd_flags(unsigned flags) {
 (void)flags;
 if (++fd_steps == fail_fd) return -EMFILE;
 for (int i = 0; i < 128; i++) if (!reserved[i] && !table[i]) { reserved[i] = true; return i; }
 return -EMFILE;
}
static void put_unused_fd(int fd) { assert(reserved[fd] && !table[fd]); reserved[fd] = false; }
static struct file *get_file(struct file *f) { assert(f->refs); f->refs++; return f; }
static void fput(struct file *f) {
 assert(f->refs > 0);
 if (!--f->refs) { if (f->f_op == &qtee_fops) put_si_object(f->private_data); free(f); }
}
static struct file *anon_inode_getfile(const char *name, const struct file_operations *ops, void *private, unsigned flags) {
 (void)name; (void)flags;
 if (++anon_steps == fail_anon) return ERR_PTR(-ENOMEM);
 struct file *f = calloc(1, sizeof(*f)); assert(f);
 f->refs = 1; f->f_op = ops; f->private_data = private; return f;
}
static void fd_install(int fd, struct file *f) {
 assert(reserved[fd] && !table[fd]); assert(f->refs);
 reserved[fd] = false; table[fd] = f; installs++;
}
static int copy_from_user(void *dst, const void *src, size_t len) { memcpy(dst, src, len); return 0; }
static int copy_to_user(void *dst, const void *src, size_t len) {
 assert(!installs); /* Every reply copy precedes publication. */
 if (++copy_steps == fail_copy) return 1;
 memcpy(dst, src, len); return 0;
}
static void *kzalloc(size_t len, int flags) { (void)flags; return calloc(1, len); }
static void *kcalloc(size_t n, size_t len, int flags) { (void)flags; return calloc(n, len); }
#define kfree free
static int marshal_in_req(struct si_arg *u, union smcinvoke_arg *args, u32 counts) {
 (void)args; assert(counts == (objects << 12));
 for (unsigned i = 0; i < objects; i++) u[i].type = SI_AT_OO;
 return 0;
}
static void marshal_in_req_cleanup(struct si_arg *u, int notify) { (void)u; (void)notify; }
static int si_object_do_invoke(struct si_object_invoke_ctx *ctx, struct si_object *obj, unsigned op, struct si_arg *u, int *result) {
 (void)ctx; (void)obj; (void)op;
 for (unsigned i = 0; i < objects; i++) u[i].o = &object_pool[i];
 *result = 0; return 0;
}
static int marshal_out_cb_req(struct si_arg *args, union smcinvoke_arg *u_args) { (void)args; (void)u_args; return 0; }
static struct cb_txn *get_txn_for_state_transition(struct server_info *si, unsigned id, unsigned state) { (void)si; (void)id; (void)state; return NULL; }
static int wait_for_pending_txn(struct server_info *si, struct cb_txn **txn) {
 (void)si; if (picked) return -ERESTARTSYS; picked = true; *txn = &callback; return 0;
}
static int set_txn_state(struct cb_txn *txn, unsigned state) { (void)txn; (void)state; return 0; }
static void complete(int *completion) { (*completion)++; }
static void put_txn(struct cb_txn *txn) { (void)txn; }
static void dequeue_and_put_txn(struct cb_txn *txn) { (void)txn; delivered++; }
/* BATCH STRUCTS */
/* FUNCTIONS */

static void prepare(unsigned count, bool memory) {
 assert(count <= 15); objects = count;
 fd_steps = anon_steps = copy_steps = installs = object_puts = 0;
 fail_fd = fail_anon = fail_copy = 0; picked = false; delivered = 0;
 memset(object_pool, 0, sizeof(object_pool));
 for (unsigned i = 0; i < count; i++) {
  object_pool[i] = (struct si_object){.refs = 1, .type = memory ? SI_OT_CB_OBJECT : SI_OT_USER, .memory = memory};
  if (memory) {
   struct file *f = calloc(1, sizeof(*f)); assert(f); f->refs = 1;
   object_pool[i].dma_file = f; dma_pool[i].file = f;
  }
 }
}
static void cleanup(bool memory) {
 for (unsigned i = 0; i < 128; i++) {
  assert(!reserved[i]);
  if (table[i]) { fput(table[i]); table[i] = NULL; }
 }
 for (unsigned i = 0; i < objects; i++) {
  assert(!object_pool[i].refs);
  if (memory) { assert(object_pool[i].dma_file->refs == 1); fput(object_pool[i].dma_file); }
 }
 cases++;
}
static void invocation(unsigned n, bool memory, unsigned fd_fault, unsigned anon_fault, unsigned copy_fault) {
 prepare(n, memory); fail_fd = fd_fault; fail_anon = anon_fault; fail_copy = copy_fault;
 union smcinvoke_arg args[15] = {};
 struct smcinvoke_cmd_req req = {.op = 5, .counts = n << 12, .argsize = sizeof(args[0]), .args = (uintptr_t)args};
 struct si_object root = {.type = SI_OT_NULL}; struct file filp = {.private_data = &root};
 int ret = process_invoke_req(&filp, 0, (uintptr_t)&req);
 bool failure = fd_fault || anon_fault || copy_fault;
 assert(failure ? ret < 0 && !installs : !ret && installs == n);
 cleanup(memory);
}
static void acceptance(unsigned n, bool memory, unsigned fd_fault, unsigned anon_fault, unsigned copy_fault) {
 prepare(n, memory); fail_fd = fd_fault; fail_anon = anon_fault; fail_copy = copy_fault;
 struct si_arg u[16] = {}; union smcinvoke_arg args[15] = {}; unsigned char buffer[sizeof(args) + 8] = {};
 for (unsigned i = 0; i < n; i++) u[i] = (struct si_arg){.type = SI_AT_IO, .o = &object_pool[i]};
 callback = (struct cb_txn){.args = u, .u_args = args, .u_args_sz = n * sizeof(args[0]), .context_id = 7};
 struct smcinvoke_accept accept = {.argsize = sizeof(args[0]), .buf_addr = (uintptr_t)buffer, .buf_len = sizeof(buffer)};
 struct smcinvoke_accept user = {}; struct server_info server = {};
 int ret = process_accept_req(&server, &accept, &user);
 bool failure = fd_fault || anon_fault || copy_fault;
 assert(failure ? ret < 0 && !installs && callback.completion : !ret && installs == n && !user.has_resp);
 cleanup(memory);
}
int main(void) {
 for (unsigned n = 1; n <= 15; n++) for (unsigned memory = 0; memory <= 1; memory++) {
  invocation(n, memory, 0, 0, 0); acceptance(n, memory, 0, 0, 0);
  for (unsigned pos = 1; pos <= n; pos++) {
   invocation(n, memory, pos, 0, 0); acceptance(n, memory, pos, 0, 0);
   if (!memory) { invocation(n, false, 0, pos, 0); acceptance(n, false, 0, pos, 0); }
  }
  for (unsigned pos = 1; pos <= 2; pos++) {
   invocation(n, memory, 0, 0, pos); acceptance(n, memory, 0, 0, pos);
  }
 }
 prepare(15, false); struct smci_fd_batch batch = {};
 for (unsigned i = 0; i < 15; i++) { s64 fd; assert(!get_u_handle_from_si_object(&object_pool[i], &fd, NULL, &batch)); }
 assert(smci_fd_reserve(&batch, 0) == -E2BIG && !installs);
 smci_fd_batch_finish(&batch, false); smci_fd_batch_finish(&batch, false); cleanup(false);
 struct si_object generic = {.type = SI_OT_CB_OBJECT, .refs = 1, .memory = 2};
 s64 invalid_handle = 7;
 assert(get_u_handle_from_si_object(&generic, &invalid_handle, NULL, &batch) == -EINVAL);
 assert(invalid_handle == -1 && generic.refs == 1 && !batch.count);
 struct si_object physical = {.type = SI_OT_CB_OBJECT, .refs = 1, .memory = 1};
 assert(get_u_handle_from_si_object(&physical, &invalid_handle, NULL, &batch) == -EINVAL);
 assert(invalid_handle == -1 && physical.refs == 1 && !batch.count);
 printf("{\"invoke_and_accept_fault_cases\":%u,\"all_reply_copies_precede_fd_install\":true,\"dma_and_object_refs_balanced\":true,\"partial_export_aborted\":true,\"batch_limit_checked\":true}\n", cases);
}
