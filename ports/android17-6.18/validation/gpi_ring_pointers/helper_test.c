#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t phys_addr_t;
#define lower_32_bits(x) ((u32)(x))
#define BIT(x) (1U << (x))
#define unlikely(x) (x)
#define mb() ((void)0)
#define MAX_CHANNELS_PER_GPII 2
#define GPI_DBG_COMMON 2
#define GPII_VERB(...) ((void)0)
#define GPII_ERR(...) ((void)0)
#define GPII_INFO(...) ((void)0)
#define dev_err_ratelimited(...) (invalid_logs++)
#define dev_info(...) (trace_logs++)
#define XFER_COMPLETE_EV_TYPE 1
#define STALE_EV_TYPE 2
#define IMMEDIATE_DATA_EV_TYPE 3
#define QUP_NOTIF_EV_TYPE 4
#define QUP_TCE_TYPE_Q2SPI_STATUS 5
#define QUP_TCE_TYPE_Q2SPI_CR_HEADER 6
#define MSM_GPI_QUP_FW_ERROR 3
#define MSM_GPI_QUP_SW_ERROR 6

struct gpi_ring {
    void *base, *rp, *wp;
    phys_addr_t phys_addr;
    u32 len, el_size;
    bool rp_traced;
};
struct event { u32 chid, type; };
union gpi_event {
    struct event xfer_compl_event, immediate_data_event, qup_notif_event;
    struct event q2spi_status, q2spi_cr_header_event;
    struct { u32 dword[4]; } gpi_ere;
};
struct gpii;
struct gpii_chan { struct gpii *gpii; u32 chid; };
struct gpi_dev { void *dev; };
struct gpii {
    struct gpi_ring *ev_ring;
    struct gpii_chan gpii_chan[2];
    struct gpi_dev *gpi_dev;
    void *ev_ring_rp_lsb_reg, *ieob_clr_reg;
    u32 gpii_id;
};

static bool boot_trace;
static unsigned invalid_logs, trace_logs, callbacks, recycled, doorbells, acks;
static unsigned reads, change_at_read;
static u32 producer, next_producer;
static int error_callbacks, last_error;
static u32 gpi_read_reg(struct gpii *g, void *reg)
{
    assert(reg == g->ev_ring_rp_lsb_reg);
    if (++reads == change_at_read) producer = next_producer;
    return producer;
}
static void gpi_write_reg(struct gpii *g, void *reg, u32 value)
{
    assert(reg == g->ieob_clr_reg && value == BIT(0));
    acks++;
}
static void gpi_write_ev_db(struct gpii *g, struct gpi_ring *ring, void *wp)
{
    assert(g->ev_ring == ring && ring->wp == wp);
    doorbells++;
}
static void gpi_ring_recycle_ev_element(struct gpi_ring *ring)
{
    assert(++recycled <= ring->len / ring->el_size * 2);
    ring->rp = (char *)ring->rp + ring->el_size;
    if (ring->rp == (char *)ring->base + ring->len) ring->rp = ring->base;
    ring->wp = ring->rp;
}
#define processor(name) static void name(struct gpii_chan *ch, struct event *ev) \
    { assert(ch->chid == ev->chid); assert(++callbacks <= 128); }
processor(gpi_process_xfer_compl_event)
processor(gpi_process_imed_data_event)
processor(gpi_process_qup_notif_event)
processor(gpi_process_qup_q2spi_status)
processor(gpi_process_xfer_q2spi_cr_header)
static void gpi_generate_cb_event(struct gpii_chan *ch, int event, int status)
{
    (void)ch; (void)status;
    error_callbacks++;
    last_error = event;
}

#include "helper.inc"

static unsigned checks;
static void pointer_cases(void)
{
    static const uint64_t bases[] = {
        0, 0x12000000, 0xffff8000, 0x100000000,
        0x112000000, 0x12ffff8000, 0xffff00000000ULL
    };
    static const unsigned lengths[] = {32, 256, 32768};
    char *storage = malloc(32768);
    assert(storage);
    for (unsigned b = 0; b < sizeof(bases) / sizeof(*bases); b++) {
        for (unsigned n = 0; n < sizeof(lengths) / sizeof(*lengths); n++) {
            struct gpi_ring ring = {.base=storage, .phys_addr=bases[b],
                                    .len=lengths[n], .el_size=16};
            for (unsigned off = 0; off < ring.len; off += 16) {
                void *target = gpi_ring_ptr_lsb(&ring, (u32)(bases[b] + off));
                assert(target == storage + off);
                assert(to_virtual(&ring, bases[b] + off) == target);
                assert(to_physical(&ring, target) == bases[b] + off);
                unsigned walk = (off + ring.len - 16) % ring.len;
                unsigned steps = 0;
                while (storage + walk != target) {
                    assert(++steps < ring.len / ring.el_size);
                    walk = (walk + 16) % ring.len;
                }
                checks++;
            }
            assert(!gpi_ring_ptr_lsb(&ring, (u32)(bases[b] - 16)));
            assert(!gpi_ring_ptr_lsb(&ring, (u32)(bases[b] + ring.len)));
            assert(!gpi_ring_ptr_lsb(&ring, (u32)(bases[b] + 1)));
            assert(!to_virtual(&ring, bases[b] + 0x100000000ULL));
            assert(!to_virtual(&ring, bases[b] - 16));
            ring.el_size = 0;
            assert(!gpi_ring_ptr_lsb(&ring, (u32)bases[b]));
            ring.el_size = 16; ring.len = 0;
            assert(!gpi_ring_ptr_lsb(&ring, (u32)bases[b]));
            checks += 7;
        }
    }
    /* Previous conversion subtracts a full DMA base from a zero-extended RP.
     * Model the integer offset to avoid forming an invalid C pointer. */
    uint64_t high = 0x112000000ULL;
    uint64_t old_offset = (uint64_t)(u32)(high + 32) - high;
    assert(old_offset >= 256 && old_offset != 32);
    for (unsigned off = 0; off < 256; off += 16) assert(off != old_offset);
    free(storage);
}

static union gpi_event events[8];
static struct gpi_ring ring;
static struct gpi_dev device;
static struct gpii gpi;
static void reset(uint64_t dma, unsigned start, unsigned end)
{
    invalid_logs = trace_logs = callbacks = recycled = doorbells = acks = 0;
    error_callbacks = last_error = reads = change_at_read = 0;
    boot_trace = true;
    ring = (struct gpi_ring){.base=events, .rp=&events[start], .wp=&events[start],
        .phys_addr=dma, .len=sizeof(events), .el_size=sizeof(events[0])};
    gpi = (struct gpii){.ev_ring=&ring, .gpi_dev=&device,
        .ev_ring_rp_lsb_reg=(void *)1, .ieob_clr_reg=(void *)2};
    for (unsigned ch = 0; ch < 2; ch++)
        gpi.gpii_chan[ch] = (struct gpii_chan){.gpii=&gpi, .chid=ch};
    for (unsigned i = 0; i < 8; i++)
        events[i].xfer_compl_event = (struct event){.chid=i % 2, .type=1};
    producer = (u32)(dma + end * sizeof(events[0]));
}
static void drain_cases(void)
{
    uint64_t bases[] = {0x12000000, 0x112000000ULL, 0x100000000ULL};
    for (unsigned b = 0; b < 3; b++) {
        for (unsigned start = 0; start < 8; start++) {
            for (unsigned end = 0; end < 8; end++) {
                reset(bases[b], start, end);
                gpi_process_events(&gpi);
                assert(recycled == (end + 8 - start) % 8);
                assert(callbacks == recycled && !error_callbacks && !invalid_logs);
                assert(acks == 1 && doorbells == 1 && trace_logs == 1);
                assert(ring.rp == &events[end]);
                checks++;
            }
        }
    }
    reset(0x112000000ULL, 6, 1);
    change_at_read = 2;
    next_producer = (u32)(ring.phys_addr + 3 * ring.el_size);
    gpi_process_events(&gpi);
    assert(callbacks == 5 && recycled == 5 && acks == 2 && doorbells == 2);
    assert(ring.rp == &events[3] && trace_logs == 1);

    reset(0x112000000ULL, 0, 1);
    producer++;
    gpi_process_events(&gpi);
    assert(!recycled && !callbacks && invalid_logs == 1 && error_callbacks == 2);
    assert(last_error == MSM_GPI_QUP_SW_ERROR && acks == 1 && !doorbells);

    reset(0x112000000ULL, 0, 1);
    change_at_read = 2; next_producer = producer + 1;
    gpi_process_events(&gpi);
    assert(recycled == 1 && callbacks == 1 && invalid_logs == 1 && error_callbacks == 2);
    assert(acks == 2 && doorbells == 1);

    reset(0x112000000ULL, 0, 2);
    events[0].xfer_compl_event.chid = 2;
    gpi_process_events(&gpi);
    assert(!recycled && !callbacks && error_callbacks == 2);
    assert(last_error == MSM_GPI_QUP_FW_ERROR && acks == 1);

    reset(0x112000000ULL, 6, 3);
    gpi_mark_stale_events(&gpi.gpii_chan[0]);
    assert(events[6].xfer_compl_event.type == STALE_EV_TYPE);
    assert(events[0].xfer_compl_event.type == STALE_EV_TYPE);
    assert(events[2].xfer_compl_event.type == STALE_EV_TYPE);
    assert(events[7].xfer_compl_event.type == XFER_COMPLETE_EV_TYPE);
    assert(ring.rp == &events[6]);

    reset(0x112000000ULL, 0, 2); producer++;
    gpi_mark_stale_events(&gpi.gpii_chan[0]);
    assert(events[0].xfer_compl_event.type == XFER_COMPLETE_EV_TYPE);
    assert(invalid_logs == 1 && !error_callbacks);
    checks += 6;
}
int main(void)
{
    pointer_cases();
    drain_cases();
    printf("PASS: %u pointer/drain cases; old unreachable-target regression reproduced\n", checks);
}
