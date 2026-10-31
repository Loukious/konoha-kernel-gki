#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define QDF_STATUS_SUCCESS 0
#define QDF_STATUS_E_FAILURE 1
#define QDF_IS_STATUS_ERROR(x) ((x) != QDF_STATUS_SUCCESS)
#define QDF_DMA_FROM_DEVICE 1
#define WLAN_TARGET_IF_COMP_DIRECT_BUF_RX 7
#define WMI_HOST_DBR_DATA_ADDR_HI_GET(x) ((x) & 0xf)
#define WMI_HOST_DBR_DATA_ADDR_HI_HOST_DATA_GET(x) (((x) >> 12) & 0x7ffff)
#define direct_buf_rx_err(...) ((void)0)
#define direct_buf_rx_debug(...) ((void)0)
typedef int QDF_STATUS;
typedef uint64_t qdf_dma_addr_t;
struct wlan_objmgr_psoc { int id; };
struct wlan_objmgr_pdev { struct wlan_objmgr_psoc *psoc; };
struct direct_buf_rx_psoc_obj { void *osdev; };
struct direct_buf_rx_ring_cap { unsigned min_buf_size; };
struct direct_buf_rx_module_param { struct direct_buf_rx_ring_cap *dbr_ring_cap; };
struct direct_buf_rx_rsp { struct { uint32_t paddr_hi, paddr_lo, len; } dbr_entries[1]; };
struct direct_buf_rx_data { size_t dbr_len, buffer_capacity; void *vaddr; uint32_t cookie; qdf_dma_addr_t paddr; };
static struct direct_buf_rx_psoc_obj psoc_obj;
static bool missing_obj, bad_cookie, fail_repost;
static int unmapped, reposted, consumed;
static void *buffer;
static unsigned capacity;
static struct wlan_objmgr_psoc *wlan_pdev_get_psoc(struct wlan_objmgr_pdev *pdev) { return pdev->psoc; }
static struct direct_buf_rx_psoc_obj *wlan_objmgr_psoc_get_comp_private_obj(struct wlan_objmgr_psoc *psoc, int id) { assert(psoc && id==7); return missing_obj ? NULL : &psoc_obj; }
static void *target_if_dbr_vaddr_lookup(struct direct_buf_rx_module_param *mod, qdf_dma_addr_t addr, uint32_t cookie) { assert(addr==UINT64_C(0x112345678) && cookie==13); return bad_cookie ? NULL : buffer; }
static void qdf_mem_unmap_nbytes_single(void *dev, qdf_dma_addr_t addr, int direction, unsigned size) { assert(direction==QDF_DMA_FROM_DEVICE && size==capacity && !unmapped); unmapped++; }
static QDF_STATUS target_if_dbr_replenish_ring(struct wlan_objmgr_pdev *pdev, struct direct_buf_rx_module_param *mod, void *data, uint32_t cookie) { assert(unmapped && !consumed && data==buffer && cookie==13); reposted++; return fail_repost ? QDF_STATUS_E_FAILURE : QDF_STATUS_SUCCESS; }

/* GET DATA */

static QDF_STATUS receive_one(struct wlan_objmgr_pdev *pdev,
                             struct direct_buf_rx_module_param *mod_param,
                             struct direct_buf_rx_rsp *rsp)
{
    struct direct_buf_rx_data dbr_data={0};
    uint32_t cookie;
    QDF_STATUS status=target_if_get_dbr_data(pdev,mod_param,rsp,&dbr_data,0,&cookie);
    if (QDF_IS_STATUS_ERROR(status)) return status;
    assert(dbr_data.buffer_capacity==capacity && dbr_data.dbr_len==rsp->dbr_entries[0].len);
    for (int i=0; i<1; i++) {
        /* OVERSIZE GUARD */
        assert(dbr_data.dbr_len<=capacity && unmapped && !reposted);
        consumed++;
    }
out:
    return status;
}

int main(void)
{
    struct wlan_objmgr_psoc psoc={0};
    struct wlan_objmgr_pdev pdev={.psoc=&psoc};
    struct direct_buf_rx_ring_cap cap={0};
    struct direct_buf_rx_module_param mod={.dbr_ring_cap=&cap};
    int cases=0;
    for (capacity=1; capacity<=4096; capacity*=2) {
        cap.min_buf_size=capacity; buffer=malloc(capacity);
        uint32_t lengths[]={0,capacity-1,capacity,capacity+1,UINT32_MAX};
        for (unsigned i=0; i<5; i++) for (int failed=0; failed<2; failed++) {
            struct direct_buf_rx_rsp rsp={.dbr_entries={{.paddr_hi=(13<<12)|1,.paddr_lo=0x12345678,.len=lengths[i]}}};
            unmapped=reposted=consumed=0; fail_repost=failed;
            int ret=receive_one(&pdev,&mod,&rsp);
            bool oversize=lengths[i]>capacity;
            assert(unmapped==1 && reposted==oversize && consumed==!oversize);
            assert(ret==(oversize && failed ? QDF_STATUS_E_FAILURE : QDF_STATUS_SUCCESS));
            cases++;
        }
        free(buffer);
    }
    puts("130 DBR allocation-bound/repost/failure cases passed");
    assert(cases==130);
    return 0;
}
