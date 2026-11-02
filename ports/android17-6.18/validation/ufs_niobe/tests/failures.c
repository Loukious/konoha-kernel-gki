#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define EIO 5
#define EOPNOTSUPP 95
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define dev_err(...) ((void)0)
#define UFS_PHY_DEFAULT_LANES_PER_DIRECTION 2
#define UFS_QCOM_PHY_SUBMODE_G4 1
#define UFS_QCOM_PHY_SUBMODE_G5 2
#define PHY_MODE_UFS_HS_B 4
#define UFS_PHY_SW_RESET 0
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
struct device { void *of_node; };
struct platform_device { struct device dev; };
struct phy { struct device dev; };
struct phy_ops { int unused; };
struct phy_provider { int unused; };
struct ufs_qcom_phy_specific_ops { int unused; };
struct ufs_qcom_phy {
    struct device *dev;
    struct ufs_qcom_phy_specific_ops *phy_spec_ops;
    int lanes_per_direction, regs_list_head, submode, mode;
    void *ufs_reset;
    unsigned char *mmio;
};
static struct phy physical;
static struct phy_provider provider;
static struct ufs_qcom_phy state;
static unsigned char mmio[4];
static int failure, reset_calls, deassert_calls, writes, ready_calls;
static int phy_cal_table_rate_A_g5[1], phy_cal_table_rate_A_g4[1];
static int phy_cal_table_2nd_lane[1], phy_cal_table_rate_B[1];
static int ufs_qcom_phy_base_init(struct platform_device *p, struct ufs_qcom_phy *c) { return failure == 1 ? -517 : 0; }
static struct phy *devm_phy_create(struct device *d, void *n, const struct phy_ops *o) { return failure == 2 ? ERR_PTR(-12) : &physical; }
static struct phy_provider *devm_of_phy_provider_register(struct device *d, int x) { return failure == 3 ? ERR_PTR(-517) : &provider; }
static int of_phy_simple_xlate;
static int of_property_read_u32(void *n, const char *s, int *v) { return -1; }
static void pm_runtime_no_callbacks(struct device *d) { }
#define INIT_LIST_HEAD(p) (*(p)=1)
static struct ufs_qcom_phy *get_ufs_qcom_phy(struct phy *p) { return &state; }
static int reset_control_assert(void *p) { reset_calls++; return failure == 4 ? -EIO : 0; }
static int reset_control_deassert(void *p) { deassert_calls++; return failure == 5 ? -EIO : 0; }
static void writel_relaxed(int v, void *p) { writes++; }
static void wmb(void) { }
static void ufs_qcom_phy_write_tbl(struct ufs_qcom_phy *p, int *t, int n) { writes++; }
static void ufs_qcom_phy_qmp_v4_start_serdes(struct ufs_qcom_phy *p) { }
static int ufs_qcom_phy_qmp_v4_is_pcs_ready(struct ufs_qcom_phy *p) { ready_calls++; return failure == 6 ? -110 : 0; }
/* PRODUCTION_FUNCTIONS */
int main(void)
{
    struct platform_device p = {0};
    struct ufs_qcom_phy common = {0};
    struct phy_ops ops = {0};
    struct ufs_qcom_phy_specific_ops specific = {0};
    for (failure = 1; failure <= 3; failure++) {
        struct phy *result = ufs_qcom_phy_generic_probe(&p, &common, &ops, &specific);
        assert(IS_ERR(result));
        assert(PTR_ERR(result) == (failure == 2 ? -12 : -517));
    }
    failure = 0;
    assert(ufs_qcom_phy_generic_probe(&p, &common, &ops, &specific) == &physical);
    assert(common.dev == &p.dev && common.phy_spec_ops == &specific);
    assert(common.lanes_per_direction == 2 && common.regs_list_head == 1);
    state.mmio = mmio;
    for (int mode = -2; mode <= 4; mode++) {
        if (mode == 1 || mode == 2) continue;
        state.submode = mode;
        assert(ufs_qcom_phy_qmp_v4_phy_calibrate(&physical) == -EOPNOTSUPP);
        assert(reset_calls == 0 && writes == 0);
    }
    state.submode = 2;
    state.lanes_per_direction = 2;
    state.mode = PHY_MODE_UFS_HS_B;
    for (failure = 4; failure <= 6; failure++) {
        assert(ufs_qcom_phy_qmp_v4_phy_calibrate(&physical) == (failure == 6 ? -110 : -EIO));
    }
    failure = 0;
    assert(ufs_qcom_phy_qmp_v4_phy_calibrate(&physical) == 0);
    assert(reset_calls == 4 && deassert_calls == 3 && ready_calls == 2);
    puts("PASS: probe error propagation and calibration failure ordering");
}
