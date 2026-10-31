/* Host fault harness: actual staged helpers are injected by verify_charging.py. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef uint8_t u8;
typedef uint32_t u32;
#define __packed __attribute__((packed))
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define READ_ONCE(v) (v)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((intptr_t)(p))
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define MCA_EVENT_NOTIFY_SIZE 128
#define UEVENT_NUM_ENVP 64
#define KOBJ_CHANGE 0
#define QCOM_SMEM_HOST_ANY UINT_MAX
#define SMEM_ID_VENDOR_BATTERY_INFO 0x51
#define MCA_ADSP_PROP_DATA_LEN 256
#define MCA_ADSP_GLINK_OWNER 0x800a
#define MCA_ADSP_GLINK_QBG_OWNER 0x8009
#define MCA_ADSP_GLINK_MSG_REQ 1
#define MCA_ADSP_GLINK_MSG_NOTIFY 2
#define MCA_ADSP_GLINK_OPCODE_READ 1
#define MCA_ADSP_GLINK_OPCODE_WRITE 2
#define UCSI_BUF_SIZE 48
#define MSG_OWNER_UC 32779
#define UC_UCSI_READ_BUF_REQ 0x11
#define UC_UCSI_WRITE_BUF_REQ 0x12
#define UC_UCSI_USBC_NOTIFY_IND 0x13
#define UC_NOTIFY_RECEIVER_UCSI 0
#define PMIC_GLINK_STATE_DOWN 0
#define PMIC_GLINK_STATE_UP 1
struct list_head { struct list_head *next, *prev; };
struct mutex { int unused; };
struct completion { int done; };
struct work_struct { int unused; };
struct workqueue_struct { int unused; };
struct kobject { int unused; };
struct device { struct kobject kobj; void *data; };
struct rpmsg_device { struct device dev; };
struct ucsi { int unused; };
struct attribute { int id; };
struct attribute_group { int unused; };
struct device_attribute { struct attribute attr; };
struct mca_sysfs_attr_info { struct device_attribute attr; };
struct mca_event_notify_data { const char *event; int event_len; };
struct mca_event_dev { struct kobject *sysfs_ne; };
static struct mca_event_dev event_dev;
static struct mca_event_dev *g_mca_event_dev = &event_dev;
static void mutex_lock(struct mutex *lock) { (void)lock; }
static void mutex_unlock(struct mutex *lock) { (void)lock; }
#define spin_lock_irqsave(lock, flags) do { (void)(lock); (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)
static void complete(struct completion *c) { c->done++; }
static int atomic_read(int *v) { return *v; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static int allocation_fail, allocations, allocations_live;
static void *kcalloc(size_t n, size_t size, int flags)
{
	(void)flags;
	if (++allocations == allocation_fail)
		return NULL;
	void *p = calloc(n, size);
	if (p) allocations_live++;
	return p;
}
static void *kzalloc(size_t size, int flags) { return kcalloc(1, size, flags); }
static void kfree(void *p) { if (p) allocations_live--; free(p); }
static void reset_alloc(int fail) { assert(!allocations_live); allocation_fail = fail; allocations = 0; }
static int uevents, expected_envs;
static int kobject_uevent_env(struct kobject *k, int action, char **env)
{
	(void)k; (void)action;
	int i;
	for (i = 0; env[i]; i++) {
		assert(i < UEVENT_NUM_ENVP);
		assert(strlen(env[i]) < MCA_EVENT_NOTIFY_SIZE);
	}
	assert(i == expected_envs);
	uevents++;
	return 0;
}
/* STAGED_STRUCTS */
static struct mca_smem_battery_info smem_data;
static size_t smem_size = sizeof(smem_data);
static int smem_alloc_error, smem_get_error;
static int qcom_smem_alloc(unsigned int host, int item, size_t size)
{
	assert(host == QCOM_SMEM_HOST_ANY && item == SMEM_ID_VENDOR_BATTERY_INFO && size == sizeof(smem_data));
	return smem_alloc_error;
}
static void *qcom_smem_get(unsigned int host, int item, size_t *size)
{
	(void)host; (void)item;
	*size = smem_size;
	return smem_get_error ? ERR_PTR(smem_get_error) : &smem_data;
}
static struct device power_dev;
static int power_present = 1, refs, file_attempts, fail_file, removed, removed_order[8];
static int group_error, link_error, groups_removed;
static struct device *mca_sysfs_get_power_device(const char *name)
{
	assert(!strcmp(name, "charger"));
	if (!power_present) return NULL;
	refs++;
	return &power_dev;
}
static void put_device(struct device *dev) { assert(dev == &power_dev); refs--; }
static int sysfs_create_file(struct kobject *k, struct attribute *attr)
{
	(void)k;
	assert(attr->id == file_attempts);
	return file_attempts++ == fail_file ? -ENOMEM : 0;
}
static void sysfs_remove_file(struct kobject *k, struct attribute *attr)
{
	(void)k;
	removed_order[removed++] = attr->id;
}
static int sysfs_create_group(struct kobject *k, const struct attribute_group *g) { (void)k; (void)g; return group_error; }
static int sysfs_create_link(struct kobject *k, struct kobject *target, const char *name) { (void)k; (void)target; (void)name; return link_error; }
static void sysfs_remove_group(struct kobject *k, const struct attribute_group *g) { (void)k; (void)g; groups_removed++; }
struct mca_adsp_glink_dev {
	int response_lock;
	bool pending;
	u32 seq, owner, property_id, opcode;
	int retcode;
	u8 read_buf[MCA_ADSP_PROP_DATA_LEN];
	struct completion ack;
};
struct mca_adsp_event {
	struct list_head node;
	bool state_event;
	int state;
	struct mca_adsp_glink_notify_msg msg;
};
static int adsp_notifications;
static void mca_adsp_queue_event(struct mca_adsp_glink_dev *mca, struct mca_adsp_event *event)
{
	(void)mca;
	assert(event->msg.property_id == 77);
	adsp_notifications++;
	kfree(event);
}
struct pmic_glink_dev { int rx_lock; bool rx_enabled; struct list_head rx_list; struct workqueue_struct *rx_wq; struct work_struct rx_work; };
#define struct_size(p, member, count) (sizeof(*(p)) + sizeof((p)->member[0]) * (count))
static struct list_head *queued_node;
static int work_queued;
static void list_add_tail(struct list_head *node, struct list_head *head) { (void)head; assert(!queued_node); queued_node = node; }
static void queue_work(struct workqueue_struct *wq, struct work_struct *work) { (void)wq; (void)work; work_queued++; }
struct ucsi_dev {
	struct mutex state_lock;
	struct ucsi *ucsi;
	int state, response_lock;
	bool stopping, pending;
	u32 pending_opcode;
	int result;
	u8 read_buf[UCSI_BUF_SIZE];
	struct completion ack;
};
static int ucsi_notifications;
static void ucsi_notify_common(struct ucsi *ucsi, u32 cci) { assert(ucsi && cci == 42); ucsi_notifications++; }
/* STAGED_DEFINITIONS */

static void test_events(void)
{
	char bytes[128];
	memset(bytes, 'x', sizeof(bytes));
	struct kobject kobj = {};
	event_dev.sysfs_ne = &kobj;
	struct mca_event_notify_data events[64];
	for (int i = 0; i < 64; i++) events[i] = (struct mca_event_notify_data){ bytes, 127 };
	expected_envs = 1;
	mca_event_report_uevent(events);
	assert(uevents == 1);
	events[0].event_len = -1;
	mca_event_report_uevent(events);
	mca_event_report_multiple_uevent(events, 1);
	assert(uevents == 1);
	events[0].event_len = 128;
	mca_event_report_uevent(events);
	mca_event_report_multiple_uevent(events, 1);
	assert(uevents == 1);
	events[0].event_len = 127;
	mca_event_report_multiple_uevent(events, 0);
	mca_event_report_multiple_uevent(events, UINT_MAX);
	mca_event_report_multiple_uevent(events, 64);
	assert(uevents == 1);
	for (int fail = 1; fail <= 2; fail++) {
		reset_alloc(fail);
		mca_event_report_multiple_uevent(events, 2);
		assert(uevents == 1 && !allocations_live);
	}
	reset_alloc(0);
	expected_envs = 63;
	mca_event_report_multiple_uevent(events, 63);
	assert(uevents == 2 && !allocations_live);
}

static void test_sysfs(void)
{
	struct mca_sysfs_attr_info attrs[4] = {};
	struct device target = {};
	struct attribute_group group = {};
	for (int i = 0; i < 4; i++) attrs[i].attr.attr.id = i;
	assert(mca_sysfs_create_files(NULL, attrs, 4) == -EINVAL);
	assert(mca_sysfs_create_files("charger", attrs, -1) == -EINVAL);
	power_present = 0;
	assert(mca_sysfs_create_files("charger", attrs, 4) == -ENODEV);
	power_present = 1;
	for (int fail = 0; fail < 4; fail++) {
		file_attempts = removed = 0;
		fail_file = fail;
		assert(mca_sysfs_create_files("charger", attrs, 4) == -ENOMEM);
		assert(removed == fail && !refs);
		for (int i = 0; i < fail; i++) assert(removed_order[i] == fail - i - 1);
	}
	file_attempts = removed = 0;
	fail_file = -1;
	assert(!mca_sysfs_create_files("charger", attrs, 4));
	assert(!removed && !refs);
	mca_sysfs_remove_files("charger", attrs, 4);
	assert(removed == 4 && !refs);
	link_error = -EEXIST;
	assert(mca_sysfs_create_link_group("charger", "a", &target, &group) == -EEXIST);
	assert(groups_removed == 1 && !refs);
	group_error = -ENOMEM;
	assert(mca_sysfs_create_link_group("charger", "a", &target, &group) == -ENOMEM);
	assert(groups_removed == 1 && !refs);
}

static void test_smem(void)
{
	int mode = 99;
	u8 verified = 99, chip = 99;
	u32 cell = 99;
	assert(get_smem_battery_info(NULL) == -EINVAL);
	assert(get_smem_battery_verify_result(NULL, &chip) == -EINVAL);
	assert(get_smem_battery_cell_sn(NULL) == -EINVAL);
	smem_size = sizeof(smem_data) - 1;
	assert(get_smem_battery_info(&mode) == -EMSGSIZE && mode == 99);
	assert(get_smem_battery_verify_result(&verified, &chip) == -EMSGSIZE && verified == 99);
	assert(get_smem_battery_cell_sn(&cell) == -EMSGSIZE && cell == 99);
	smem_size++;
	smem_alloc_error = -EEXIST;
	smem_data.zero_speed_start_mode = 42;
	smem_data.verify_result = 0x30;
	smem_data.cell_sn = 0x81;
	assert(!get_smem_battery_info(&mode) && mode == 42);
	assert(!get_smem_battery_verify_result(&verified, &chip) && verified == 1 && chip == 1);
	assert(!get_smem_battery_cell_sn(&cell) && cell == 0x81);
	smem_get_error = -ENOENT;
	assert(get_smem_battery_info(&mode) == -ENOENT);
	smem_alloc_error = -ENOMEM;
	assert(get_smem_battery_info(&mode) == -ENOMEM);
}

static void test_adsp(void)
{
	struct mca_adsp_glink_dev mca = { .pending = true, .seq = 3, .owner = MCA_ADSP_GLINK_OWNER, .property_id = 7, .opcode = MCA_ADSP_GLINK_OPCODE_READ };
	struct mca_adsp_glink_resp_msg msg = { .hdr = { MCA_ADSP_GLINK_OWNER, MCA_ADSP_GLINK_MSG_REQ, MCA_ADSP_GLINK_OPCODE_READ }, .property_id = 7, .seq = 3 };
	msg.data[0] = 0xa5;
	for (size_t len = 0; len < sizeof(msg); len++) {
		void *short_msg = malloc(len ? len : 1);
		memcpy(short_msg, &msg, len);
		assert(mca_adsp_glink_callback(&mca, short_msg, len) < 0);
		free(short_msg);
	}
	assert(!mca.ack.done);
	msg.seq = 4;
	assert(!mca_adsp_glink_callback(&mca, &msg, sizeof(msg)) && !mca.ack.done);
	msg.seq = 3;
	msg.property_id = 8;
	assert(!mca_adsp_glink_callback(&mca, &msg, sizeof(msg)) && !mca.ack.done);
	msg.property_id = 7;
	msg.hdr.owner = MCA_ADSP_GLINK_QBG_OWNER;
	assert(!mca_adsp_glink_callback(&mca, &msg, sizeof(msg)) && !mca.ack.done);
	msg.hdr.owner = MCA_ADSP_GLINK_OWNER;
	assert(!mca_adsp_glink_callback(&mca, &msg, sizeof(msg)));
	assert(mca.ack.done == 1 && !mca.pending && !mca.retcode && mca.read_buf[0] == 0xa5);
	mca.pending = true;
	msg.retcode = 5;
	assert(!mca_adsp_glink_callback(&mca, &msg, sizeof(msg)) && mca.retcode == -EREMOTEIO);
	struct mca_adsp_glink_notify_msg note = { .hdr = { MCA_ADSP_GLINK_QBG_OWNER, MCA_ADSP_GLINK_MSG_NOTIFY, 1 }, .property_id = 77 };
	reset_alloc(0);
	assert(mca_adsp_glink_callback(&mca, &note, sizeof(note) - 1) == -EMSGSIZE);
	assert(!mca_adsp_glink_callback(&mca, &note, sizeof(note)) && adsp_notifications == 1 && !allocations_live);
}

static void test_pmic(void)
{
	struct pmic_glink_dev dev = { .rx_enabled = true };
	struct rpmsg_device rpdev = { .dev.data = &dev };
	struct pmic_glink_hdr hdr = {};
	reset_alloc(0);
	assert(pmic_glink_rpmsg_callback(&rpdev, &hdr, -1, NULL, 0) == -EINVAL && !allocations);
	assert(pmic_glink_rpmsg_callback(&rpdev, &hdr, INT_MIN, NULL, 0) == -EINVAL && !allocations);
	assert(pmic_glink_rpmsg_callback(&rpdev, &hdr, sizeof(hdr) - 1, NULL, 0) == -EINVAL);
	reset_alloc(1);
	assert(pmic_glink_rpmsg_callback(&rpdev, &hdr, sizeof(hdr), NULL, 0) == -ENOMEM);
	reset_alloc(0);
	dev.rx_enabled = false;
	assert(pmic_glink_rpmsg_callback(&rpdev, &hdr, sizeof(hdr), NULL, 0) == -ENOTCONN && !allocations_live);
	dev.rx_enabled = true;
	assert(!pmic_glink_rpmsg_callback(&rpdev, &hdr, sizeof(hdr), NULL, 0));
	assert(queued_node && work_queued == 1);
	struct pmic_glink_buf *buf = (void *)queued_node;
	assert(buf->len == sizeof(hdr) && !memcmp(buf->buf, &hdr, sizeof(hdr)));
	kfree(buf);
	queued_node = NULL;
}

static void test_ucsi(void)
{
	struct ucsi core = {};
	struct ucsi_dev dev = { .ucsi = &core, .state = PMIC_GLINK_STATE_UP, .pending = true, .pending_opcode = UC_UCSI_READ_BUF_REQ };
	struct ucsi_read_buf_resp_msg read = { .hdr = { MSG_OWNER_UC, 1, UC_UCSI_READ_BUF_REQ } };
	read.buf[0] = 0x12;
	for (size_t len = 0; len < sizeof(read); len++) {
		void *short_msg = malloc(len ? len : 1);
		memcpy(short_msg, &read, len);
		dev.pending = true;
		assert(ucsi_callback(&dev, short_msg, len) < 0);
		free(short_msg);
	}
	dev.pending = true;
	assert(!ucsi_callback(&dev, &read, sizeof(read)) && dev.read_buf[0] == 0x12 && !dev.pending);
	dev.pending = true;
	read.ret_code = 1;
	assert(ucsi_callback(&dev, &read, sizeof(read)) == -EREMOTEIO && dev.result == -EREMOTEIO);
	struct ucsi_write_buf_resp_msg write = { .hdr = { MSG_OWNER_UC, 1, UC_UCSI_WRITE_BUF_REQ } };
	dev.pending = true;
	dev.pending_opcode = UC_UCSI_WRITE_BUF_REQ;
	assert(!ucsi_callback(&dev, &write, sizeof(write)) && !dev.pending);
	struct ucsi_notify_ind_msg note = { .hdr = { MSG_OWNER_UC, 1, UC_UCSI_USBC_NOTIFY_IND }, .notification = 42 };
	assert(!ucsi_callback(&dev, &note, sizeof(note)) && ucsi_notifications == 1);
	dev.stopping = true;
	assert(!ucsi_callback(&dev, &note, sizeof(note)) && ucsi_notifications == 1);
	dev.stopping = false;
	dev.state = PMIC_GLINK_STATE_DOWN;
	assert(!ucsi_callback(&dev, &note, sizeof(note)) && ucsi_notifications == 1);
	note.receiver = 1;
	assert(ucsi_callback(&dev, &note, sizeof(note)) == -EINVAL);
}

int main(void)
{
	test_events(); test_sysfs(); test_smem(); test_adsp(); test_pmic(); test_ucsi();
	assert(!allocations_live && !refs);
	puts("{\"passed\":true,\"event_boundaries_and_allocation_failures\":true,\"sysfs_file_and_link_rollback\":true,\"smem_size_and_error_handling\":true,\"adsp_truncated_lengths\":280,\"pmic_signed_lengths_and_rx_shutdown\":true,\"ucsi_truncated_lengths\":64,\"glink_response_matching\":true}");
	return 0;
}
