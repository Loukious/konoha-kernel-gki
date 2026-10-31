#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CONFIG_AUDIO_PRM 1
#define __iomem
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define struct_size(p, m, n) (sizeof(*(p)) + sizeof((p)->m[0]) * (n))
#define GFP_KERNEL 0
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define EPROBE_DEFER 517
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define CLOCK_ID_QUI_MI2S_OSR 1
#define OSR_CLOCK_9_P600_MHZ 9600000
#define CLOCK_ATTRIBUTE_COUPLE_NO 0
#define HW_CORE_ID_LPASS 1
#define HW_CORE_ID_DCODEC 2
typedef uint8_t u8;
typedef uint32_t u32;
struct clk_hw;
struct clk_ops {
	int (*prepare)(struct clk_hw *);
	void (*unprepare)(struct clk_hw *);
	u8 (*get_parent)(struct clk_hw *);
};
struct clk_init_data {
	const char *name;
	const char * const *parent_names;
	u8 num_parents;
	const struct clk_ops *ops;
};
struct clk_hw {
	const struct clk_init_data *init;
	unsigned int num_parents;
	const struct clk_ops *ops;
};
struct clk_fixed_factor { unsigned int mult, div; struct clk_hw hw; };
struct clk_hw_onecell_data { unsigned int num; struct clk_hw *hws[]; };
struct device { void *of_node; };
struct platform_device { struct device dev; void *data; };
struct pinctrl { int unused; };
struct pinctrl_state { int active; };
struct clk_cfg { u32 clk_id, clk_freq_in_hz, clk_attri, clk_root; };

static struct pinctrl pins;
static struct pinctrl_state active = { 1 }, sleep_state;
static int source_id, with_pins, with_map, pin_live, map_live, clk_live, provider_live;
static int votes, hw_votes, pin_active, enabled_calls, disabled_calls;
static int request_error, release_error, active_error, sleep_error;
static int pin_get_error, lookup_error, map_error, register_error, provider_error;
static int allocation_site, allocation_failure, action_error;
static int last_core, release_sequence[16], release_count, cases;
static const char *parent_name;
static struct clk_hw *registered_hw;
static u32 *mapped_reg;
struct action { void (*fn)(void *); void *data; };
static struct action actions[32];
static unsigned int action_count;

static void add_action(void (*fn)(void *), void *data)
{
	assert(action_count < ARRAY_SIZE(actions));
	actions[action_count++] = (struct action){ fn, data };
}
static void release_provider(void *data)
{
	assert(clk_live && provider_live);
	provider_live = 0;
	release_sequence[release_count++] = 1;
}
static void release_clock(void *data)
{
	assert(!provider_live && clk_live);
	assert(!with_pins || pin_live);
	clk_live = 0;
	release_sequence[release_count++] = 2;
}
static void release_map(void *data)
{
	assert(!clk_live && !provider_live);
	map_live = 0;
	free(data);
	release_sequence[release_count++] = 3;
}
static void release_pins(void *data)
{
	assert(!clk_live && !provider_live);
	pin_live = 0;
	release_sequence[release_count++] = 4;
}
static void release_resources(void)
{
	while (action_count) {
		struct action a = actions[--action_count];
		a.fn(a.data);
	}
	assert(!provider_live && !clk_live && !pin_live && !map_live);
}
static void *devm_kzalloc(struct device *dev, size_t n, int flags)
{
	if (++allocation_site == allocation_failure)
		return NULL;
	void *p = calloc(1, n);
	assert(p);
	add_action(free, p);
	return p;
}
static void *platform_get_drvdata(struct platform_device *p) { return p->data; }
static void platform_set_drvdata(struct platform_device *p, void *d) { p->data = d; }
static struct pinctrl *devm_pinctrl_get(struct device *dev)
{
	if (pin_get_error)
		return ERR_PTR(pin_get_error);
	pin_live = 1;
	add_action(release_pins, &pins);
	return &pins;
}
static struct pinctrl_state *pinctrl_lookup_state(struct pinctrl *p, const char *name)
{
	assert(pin_live);
	if (lookup_error)
		return ERR_PTR(lookup_error);
	return strcmp(name, "active") ? &sleep_state : &active;
}
static int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *state)
{
	assert(pin_live && p == &pins);
	int err = state->active ? active_error : sleep_error;
	if (err)
		return err;
	pin_active = state->active;
	return 0;
}
static void *devm_ioremap(struct device *dev, u32 address, size_t n)
{
	if (map_error)
		return NULL;
	mapped_reg = calloc(1, n);
	assert(mapped_reg);
	map_live = 1;
	add_action(release_map, mapped_reg);
	return mapped_reg;
}
static void iowrite32(u32 value, void *address)
{
	assert(map_live && address == mapped_reg);
	*(u32 *)address = value;
}
static int of_property_read_u32(void *node, const char *name, u32 *value)
{
	if (!strcmp(name, "qcom,codec-ext-clk-src"))
		*value = source_id;
	else if (!strcmp(name, "qcom,use-pinctrl"))
		*value = with_pins;
	else if (!strcmp(name, "qcom,mclk-clk-reg") && with_map)
		*value = 0x1000;
	else
		return -EINVAL;
	return 0;
}
static int of_property_read_string(void *node, const char *name, const char **value)
{
	if (!parent_name)
		return -EINVAL;
	*value = parent_name;
	return 0;
}
static int audio_prm_set_lpass_clk_cfg(struct clk_cfg *cfg, int enable)
{
	if (enable) enabled_calls++; else disabled_calls++;
	int err = enable ? request_error : release_error;
	if (err)
		return err;
	votes += enable ? 1 : -1;
	assert(votes >= 0 && votes <= 1);
	return 0;
}
static int audio_prm_set_lpass_hw_core_req(struct clk_cfg *cfg, int core, int enable)
{
	last_core = core;
	int err = enable ? request_error : release_error;
	if (err)
		return err;
	hw_votes += enable ? 1 : -1;
	assert(hw_votes >= 0 && hw_votes <= 1);
	return 0;
}
static unsigned int clk_hw_get_num_parents(struct clk_hw *hw) { return hw->num_parents; }
static int devm_clk_hw_register(struct device *dev, struct clk_hw *hw)
{
	if (register_error)
		return register_error;
	assert(hw->init);
	hw->num_parents = hw->init->num_parents;
	hw->ops = hw->init->ops;
	hw->init = NULL;
	clk_live = 1;
	registered_hw = hw;
	add_action(release_clock, hw);
	return 0;
}
static int of_clk_hw_onecell_get;
static int devm_of_clk_add_hw_provider(struct device *dev, int getter,
				     struct clk_hw_onecell_data *data)
{
	assert(clk_live && data->num == 1 && data->hws[0] == registered_hw);
	if (provider_error)
		return provider_error;
	provider_live = 1;
	add_action(release_provider, data);
	return 0;
}
static int devm_add_action_or_reset(struct device *dev, void (*fn)(void *), void *data)
{
	if (action_error) {
		fn(data);
		return action_error;
	}
	add_action(fn, data);
	return 0;
}

/* PRODUCTION_SOURCE */

static void reset(void)
{
	assert(!action_count && !votes && !hw_votes);
	source_id = AUDIO_EXT_CLK_LPASS;
	with_pins = with_map = 1;
	request_error = release_error = active_error = sleep_error = 0;
	pin_get_error = lookup_error = map_error = register_error = provider_error = 0;
	allocation_site = allocation_failure = action_error = release_count = 0;
	enabled_calls = disabled_calls = pin_active = last_core = 0;
	parent_name = NULL;
	cases++;
}
static struct audio_ext_clk_priv *probe(struct platform_device *pdev)
{
	assert(!audio_ref_clk_probe(pdev));
	assert(provider_live && clk_live);
	return platform_get_drvdata(pdev);
}
int main(void)
{
	struct platform_device pdev = {0};
	struct audio_ext_clk_priv *p;
	struct clk_hw *hw;

	/* Callback remains valid after CCF has discarded its init pointer. */
	for (size_t i = 0; i < ARRAY_SIZE(audio_ext_pmi_div_clk); i++) {
		reset(); source_id = AUDIO_EXT_CLK_PMI;
		parent_name = audio_ext_pmi_div_clk[i];
		p = probe(&pdev); hw = &p->audio_clk.fact.hw;
		assert(!hw->init && audio_ext_clk_get_parent(hw) == i);
		release_resources();
	}
	reset(); p = probe(&pdev); hw = &p->audio_clk.fact.hw;
	assert(!audio_ext_clk_prepare(hw) && votes == 1 && pin_active && *mapped_reg == 1);
	audio_ext_clk_unprepare(hw);
	assert(!votes && !pin_active && !*mapped_reg);
	audio_ext_clk_unprepare(hw);
	assert(enabled_calls == 1 && disabled_calls == 1);
	release_resources();
	assert(release_sequence[0] == 1 && release_sequence[1] == 2);

	reset(); p = probe(&pdev); hw = &p->audio_clk.fact.hw;
	active_error = -EIO;
	assert(audio_ext_clk_prepare(hw) == -EIO && !votes && !p->enable);
	assert(!p->pins_active && !*mapped_reg);
	active_error = 0; release_resources();

	reset(); p = probe(&pdev); hw = &p->audio_clk.fact.hw;
	active_error = -EINVAL; release_error = -EIO;
	assert(audio_ext_clk_prepare(hw) == -EINVAL && votes == 1 && p->enable);
	active_error = release_error = 0;
	assert(!audio_ext_clk_prepare(hw) && enabled_calls == 1);
	release_resources(); assert(!votes && !pin_active);

	reset(); p = probe(&pdev); hw = &p->audio_clk.fact.hw;
	assert(!audio_ext_clk_prepare(hw));
	release_error = -EIO; audio_ext_clk_unprepare(hw);
	assert(votes == 1 && p->enable && !pin_active);
	release_error = 0; audio_ext_clk_unprepare(hw);
	assert(!votes && !p->enable);
	release_resources();

	reset(); p = probe(&pdev); hw = &p->audio_clk.fact.hw;
	assert(!audio_ext_clk_prepare(hw)); sleep_error = -EBUSY;
	audio_ext_clk_unprepare(hw);
	assert(votes == 1 && p->pins_active && p->gate_enabled);
	sleep_error = 0; release_resources(); assert(!votes && !pin_active);

	reset(); p = probe(&pdev); hw = &p->audio_clk.fact.hw;
	request_error = -EPROBE_DEFER;
	assert(audio_ext_clk_prepare(hw) == -EPROBE_DEFER && !votes && !pin_active);
	request_error = 0; release_resources();

	for (int mode = 0; mode < 2; mode++) {
		reset(); source_id = mode ? AUDIO_EXT_CLK_LPASS_AUDIO_HW_VOTE : AUDIO_EXT_CLK_LPASS_CORE_HW_VOTE;
		p = probe(&pdev); hw = &p->audio_clk.fact.hw;
		request_error = -EAGAIN;
		assert(lpass_hw_vote_prepare(hw) == -EAGAIN && !p->enable && !hw_votes);
		request_error = 0;
		assert(!lpass_hw_vote_prepare(hw) && hw_votes == 1);
		assert(last_core == (mode ? HW_CORE_ID_DCODEC : HW_CORE_ID_LPASS));
		release_error = -EIO; lpass_hw_vote_unprepare(hw);
		assert(hw_votes == 1 && p->enable);
		assert(!lpass_hw_vote_prepare(hw) && hw_votes == 1);
		release_error = 0; release_resources(); assert(!hw_votes);
	}

	/* Every setup failure releases acquired resources and preserves its error. */
	for (int site = 0; site < 9; site++) {
		reset();
		int expected = -ENOMEM;
		switch (site) {
		case 0: allocation_failure = 1; break;
		case 1: allocation_failure = 2; break;
		case 2: pin_get_error = expected = -EPROBE_DEFER; break;
		case 3: lookup_error = expected = -ENODEV; break;
		case 4: sleep_error = expected = -EBUSY; break;
		case 5: map_error = 1; break;
		case 6: register_error = expected = -EINVAL; break;
		case 7: provider_error = expected = -EIO; break;
		case 8: action_error = -ENOMEM; break;
		}
		assert(audio_ref_clk_probe(&pdev) == expected);
		release_resources(); assert(!votes && !hw_votes);
	}
	printf("PASS: %d clock lifecycle scenarios\n", cases);
	return 0;
}
