#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define PA_NAME_MAX 24
#define NAME_SIZE 32
#define SMARTPA_MAX_NUM 4
#define DEFINE_MUTEX(name) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define EXPORT_SYMBOL_GPL(name)
#define pr_err(...) ((void)0)
#define mutex_lock(lock) assert(pthread_mutex_lock(lock) == 0)
#define mutex_unlock(lock) assert(pthread_mutex_unlock(lock) == 0)
typedef uint32_t u32;
enum { SMARTPA_NONE, SMARTPA_FS19XX, SMARTPA_SIA91XX, SMARTPA_AW882XX, SMARTPA_MAX };

struct device_node { const char *compatible; bool available; struct device_node *parent; int refs; };
struct device_driver { const char *name; };
struct device {
	const char *name;
	struct device_driver *driver;
	struct device_node *of_node;
	atomic_int refs;
	void *managed[128];
	unsigned int managed_count;
};
struct snd_soc_dai_link_component { const char *name, *dai_name; struct device_node *of_node; };
struct snd_soc_dai_link { const char *name; struct snd_soc_dai_link_component *codecs; unsigned int num_codecs; };
struct snd_soc_card { struct device *dev; struct snd_soc_dai_link *dai_link; unsigned int num_links; };

static DEFINE_MUTEX(memory_lock);
static void *allocations[4096];
static unsigned int allocation_slots, live_allocations;
static int fail_allocation;
static void *allocate(size_t size)
{
	if (fail_allocation && !--fail_allocation)
		return NULL;
	void *ptr = calloc(1, size);
	assert(ptr);
	mutex_lock(&memory_lock);
	assert(allocation_slots < 4096);
	allocations[allocation_slots++] = ptr;
	live_allocations++;
	mutex_unlock(&memory_lock);
	return ptr;
}
static void kfree(const void *ptr)
{
	if (!ptr)
		return;
	mutex_lock(&memory_lock);
	unsigned int i;
	for (i = 0; i < allocation_slots; i++) {
		if (allocations[i] == ptr) {
			allocations[i] = NULL;
			live_allocations--;
			break;
		}
	}
	assert(i < allocation_slots);
	mutex_unlock(&memory_lock);
	free((void *)ptr);
}
static char *kstrdup(const char *src, int flags)
{
	(void)flags;
	char *dst = allocate(strlen(src) + 1);
	if (dst)
		strcpy(dst, src);
	return dst;
}
static char *kasprintf(int flags, const char *format, ...)
{
	(void)flags;
	va_list args;
	va_start(args, format);
	int length = vsnprintf(NULL, 0, format, args);
	va_end(args);
	assert(length >= 0);
	char *dst = allocate((size_t)length + 1);
	if (dst) {
		va_start(args, format);
		vsnprintf(dst, (size_t)length + 1, format, args);
		va_end(args);
	}
	return dst;
}
static int strscpy(char *dst, const char *src, size_t size)
{
	assert(size);
	size_t len = strlen(src);
	snprintf(dst, size, "%s", src);
	return len >= size ? -E2BIG : (int)len;
}
static void *managed(struct device *dev, void *ptr)
{
	if (ptr) {
		assert(dev->managed_count < 128);
		dev->managed[dev->managed_count++] = ptr;
	}
	return ptr;
}
static void *devm_kcalloc(struct device *dev, size_t count, size_t size, int flags)
{
	(void)flags;
	return managed(dev, allocate(count * size));
}
static char *devm_kstrdup(struct device *dev, const char *src, int flags)
{
	return managed(dev, kstrdup(src, flags));
}
static void release_managed(struct device *dev)
{
	while (dev->managed_count)
		kfree(dev->managed[--dev->managed_count]);
}
static const char *dev_name(struct device *dev) { return dev->name; }
static struct device *get_device(struct device *dev) { atomic_fetch_add(&dev->refs, 1); return dev; }
static void put_device(struct device *dev) { if (dev) assert(atomic_fetch_sub(&dev->refs, 1) > 1); }
static struct device_node dt_nodes[4];
static unsigned int dt_count;
static u32 tdm_enabled;
static struct device_node *next_compatible(struct device_node *prev, const char *compatible)
{
	unsigned int first = prev ? (unsigned int)(prev - dt_nodes) + 1 : 0;
	if (prev)
		prev->refs--;
	for (unsigned int i = first; i < dt_count; i++)
		if (!strcmp(dt_nodes[i].compatible, compatible)) {
			dt_nodes[i].refs++;
			return &dt_nodes[i];
		}
	return NULL;
}
#define for_each_compatible_node(node, type, compat) \
	for ((node) = next_compatible(NULL, compat); node; (node) = next_compatible(node, compat))
static bool of_device_is_available(struct device_node *node) { return node->available; }
static struct device_node *of_node_get(struct device_node *node) { if (node) node->refs++; return node; }
static void of_node_put(struct device_node *node) { if (node) assert(node->refs-- > 0); }
static struct device_node *of_get_parent(struct device_node *node) { return of_node_get(node->parent); }
static int of_property_read_u32(struct device_node *node, const char *key, u32 *value)
{
	(void)node; assert(!strcmp(key, "qcom,tdm-audio-intf")); *value = tdm_enabled; return 0;
}

static DEFINE_MUTEX(smartpa_lock);
static char smartpa_num;
static char smartpa_type[PA_NAME_MAX] = "none";
static const char *smartpa_cust_name[] = { "none", "fs19xx", "sia91xx", "aw882xx" };
static unsigned int smartpa_count;
static struct snd_soc_dai_link_component smartpa_dails[SMARTPA_MAX_NUM];
static struct device *smartpa_devices[SMARTPA_MAX_NUM];
static const char *tdm_rx_dailink_name[] = {"RX0", "RX1", "RX2", "RX3", "RX4", "RX5", "RX6"};
static const char *tdm_tx_dailink_name[] = {"TX0", "TX1", "TX2", "TX3", "TX4", "TX5", "TX6"};
static int tdm_rx0_id = 5, tdm_rx1_id = 6, tdm_tx_id = 5;

/* PRODUCTION_SOURCE */

struct thread_case { struct device *dev; const char *type; int result; };
static void *register_thread(void *arg)
{
	struct thread_case *t = arg;
	t->result = register_smartpa_codec(t->dev, "playback", t->type);
	return NULL;
}

int main(void)
{
	unsigned int cases = 0;
	struct device_driver driver = { .name = "amp" };
	struct device devices[5] = {0};
	const char *names[] = {"1-0034", "1-0035", "1-0036", "1-0037", "1-0038"};
	for (unsigned int i = 0; i < 5; i++) {
		devices[i].name = names[i]; devices[i].driver = &driver; devices[i].refs = 1;
	}
	assert(register_smartpa_codec(NULL, "dai", "aw882xx") == -EINVAL);
	assert(register_smartpa_codec(&devices[0], NULL, "aw882xx") == -EINVAL);
	assert(register_smartpa_codec(&devices[0], "dai", "unknown") == -EINVAL);
	assert(set_smartpa_type("aw", 2) == -1);
	assert(set_smartpa_type("aw882xx", 0) == -1);
	assert(set_smartpa_type("this-name-is-far-too-long", 25) == -1);
	cases += 6;
	for (int fault = 1; fault <= 3; fault++) {
		fail_allocation = fault;
		assert(register_smartpa_codec(&devices[0], "dai", "aw882xx") == -ENOMEM);
		assert(!smartpa_count && !live_allocations && get_smartpa_type() == SMARTPA_NONE);
		assert(devices[0].refs == 1);
		fail_allocation = 0; cases++;
	}
	char borrowed_dai[] = "speaker-playback";
	assert(!register_smartpa_codec(&devices[0], borrowed_dai, "aw882xx"));
	borrowed_dai[0] = 'X';
	assert(!strcmp(smartpa_dails[0].dai_name, "speaker-playback"));
	assert(!strcmp(smartpa_dails[0].name, "amp.1-0034"));
	assert(devices[0].refs == 2 && check_smartpa_type("aw882xx"));
	assert(!check_smartpa_type("sia91xx") && !check_smartpa_type(NULL));
	assert(!register_smartpa_codec(&devices[0], "speaker-playback", "aw882xx"));
	assert(smartpa_count == 1 && devices[0].refs == 2);
	assert(register_smartpa_codec(&devices[0], "changed", "aw882xx") == -EEXIST);
	assert(register_smartpa_codec(&devices[1], "dai", "sia91xx") == -ENODEV);
	assert(set_smartpa_type("sia91xx", sizeof("sia91xx")) == -1);
	cases += 6;
	dt_nodes[0] = (struct device_node){"awinic,aw882xx_smartpa", true, NULL, 0};
	dt_nodes[1] = dt_nodes[0]; dt_count = 2; tdm_enabled = 1;
	struct device_node parent = {.available = false};
	dt_nodes[0].parent = dt_nodes[1].parent = &parent;
	assert(smartpa_dt_count("awinic,aw882xx_smartpa") == 0 && !parent.refs);
	parent.available = true;
	assert(smartpa_dt_count("awinic,aw882xx_smartpa") == 2 && !parent.refs);
	cases += 2;
	struct device card_dev = {.refs = 1};
	struct snd_soc_dai_link_component original = {.name = "dummy", .dai_name = "dummy-dai"};
	struct snd_soc_dai_link links[] = {{"RX5", &original, 1}, {"TX5", &original, 1}, {"OTHER", &original, 1}};
	struct snd_soc_card card = {&card_dev, links, 3};
	assert(smartpa_dai_link_select(&card) == -EPROBE_DEFER);
	assert(links[0].codecs == &original && !card_dev.managed_count);
	assert(!register_smartpa_codec(&devices[1], "speaker-playback", "aw882xx"));
	for (int fault = 1; fault <= 5; fault++) {
		fail_allocation = fault;
		assert(smartpa_dai_link_select(&card) == -ENOMEM);
		release_managed(&card_dev); fail_allocation = 0;
		assert(smartpa_count == 2); cases++;
	}
	assert(!smartpa_dai_link_select(&card));
	assert(links[0].num_codecs == 2 && links[1].num_codecs == 2);
	assert(links[0].codecs != smartpa_dails && links[0].codecs != links[1].codecs);
	assert(links[2].codecs == &original);
	unregister_smartpa_codec(&devices[0]);
	unregister_smartpa_codec(&devices[1]);
	assert(!strcmp(links[0].codecs[0].name, "amp.1-0034"));
	assert(!strcmp(links[0].codecs[0].dai_name, "speaker-playback"));
	assert(!strcmp(links[1].codecs[1].name, "amp.1-0035"));
	assert(get_smartpa_type() == SMARTPA_NONE);
	release_managed(&card_dev);
	assert(!live_allocations); cases += 3;
	unregister_smartpa_codec(&devices[0]);
	for (unsigned int i = 0; i < 4; i++)
		assert(!register_smartpa_codec(&devices[i], "dai", "aw882xx"));
	assert(register_smartpa_codec(&devices[4], "dai", "aw882xx") == -ENOSPC);
	assert(smartpa_count == 4 && devices[4].refs == 1);
	unregister_smartpa_codec(&devices[1]);
	assert(smartpa_devices[1] == &devices[2]);
	assert(!register_smartpa_codec(&devices[4], "dai", "aw882xx"));
	for (unsigned int i = 0; i < 5; i++) unregister_smartpa_codec(&devices[i]);
	assert(!live_allocations && !smartpa_count); cases += 3;
	for (unsigned int i = 0; i < 32; i++) {
		struct thread_case t[] = {{&devices[0], "aw882xx", 99}, {&devices[1], "sia91xx", 99}};
		pthread_t threads[2];
		assert(!pthread_create(&threads[0], NULL, register_thread, &t[0]));
		assert(!pthread_create(&threads[1], NULL, register_thread, &t[1]));
		assert(!pthread_join(threads[0], NULL)); assert(!pthread_join(threads[1], NULL));
		assert((t[0].result == 0 && t[1].result == -ENODEV) ||
		       (t[1].result == 0 && t[0].result == -ENODEV));
		assert(smartpa_count == 1);
		unregister_smartpa_codec(&devices[0]); unregister_smartpa_codec(&devices[1]);
		assert(!live_allocations); cases++;
	}
	for (unsigned int i = 0; i < 5; i++) assert(devices[i].refs == 1);
	for (unsigned int i = 0; i < dt_count; i++) assert(!dt_nodes[i].refs);
	printf("%u registry, snapshot, allocation, readiness and concurrent-family cases passed\n", cases);
	return 0;
}
