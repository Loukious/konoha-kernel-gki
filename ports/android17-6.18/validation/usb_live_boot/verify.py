#!/usr/bin/env python3
"""Exercise production queue setup and sparse SoundWire mapping under sanitizers."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'audio_machine'))
from verify_smartpa import function

VIDEO = 'qcom/opensource/video-driver/driver/vidc/src/msm_vidc_driver.c'
SWR = 'qcom/opensource/audio-kernel/soc/swr-mstr-ctrl.c'

video_fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
enum v4l2_buf_type { INPUT=1, OUTPUT, INPUT_META, OUTPUT_META };
#define VB2_MMAP 1
#define VB2_DMABUF 2
#define V4L2_BUF_FLAG_TIMESTAMP_COPY 1
#define i_vpr_e(...) ((void)0)
struct device { int unused; };
struct platform_device { struct device dev; };
struct msm_vidc_core { struct platform_device *pdev; void *vb2_ops, *vb2_mem_ops; };
struct msm_vidc_inst { struct msm_vidc_core *core; int ctx_q_lock; };
struct vb2_queue { enum v4l2_buf_type type; int io_modes, timestamp_flags, copy_timestamp; void *ops, *mem_ops, *drv_priv; int *lock; struct device *dev; };
static struct msm_vidc_inst *expected;
static int injected_error;
static int vb2_queue_init(struct vb2_queue *q)
{
    /* 6.18 rejects an unlocked queue without custom wait callbacks. */
    if (!q->lock) return -EINVAL;
    assert(q->lock == &expected->ctx_q_lock);
    assert(q->dev == &expected->core->pdev->dev);
    assert(q->drv_priv == expected);
    return injected_error;
}
/* SOURCE */
int main(void)
{
    struct platform_device pdev = {0};
    struct msm_vidc_core core = {.pdev=&pdev};
    struct msm_vidc_inst inst = {.core=&core};
    expected = &inst;
    for (int type=INPUT; type<=OUTPUT_META; type++) {
        struct vb2_queue q = {0};
        assert(vb2q_init(&inst, &q, type) == 0);
        assert(q.type == type && q.io_modes == (VB2_MMAP|VB2_DMABUF));
        injected_error = -ENOMEM;
        assert(vb2q_init(&inst, &q, type) == -ENOMEM);
        injected_error = 0;
    }
    puts("all four queue types meet the 6.18 lock contract; errors propagate");
}
'''

swr_fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define SWR_MSTR_PORT_LEN 13
#define SWR_MAX_CH_PER_PORT 8
#define MASTER_ID_BT 5
#define max(a,b) ((a)>(b)?(a):(b))
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
struct swr_mstr_ctrl {
    u32 num_ports, master_id;
    void *dev;
    struct { u8 port_type, ch_mask; } port_mapping[SWR_MSTR_PORT_LEN+1][SWR_MAX_CH_PER_PORT];
};
static int parse(struct swr_mstr_ctrl *swrm, u32 *temp, u32 map_length, u32 num_ports)
{
    u32 i, port_num, port_type, ch_mask, ch_iter;
    u8 mapped_channels[SWR_MSTR_PORT_LEN+1] = {0};
    int ret = 0;
    swrm->num_ports = num_ports;
    /* LOOP */
    return 0;
err_pdata_fail:
    return ret;
}
/* SOURCE */
int main(void)
{
    struct swr_mstr_ctrl s = {.master_id=MASTER_ID_BT};
    u32 sparse[] = {1,101,3,2,103,3,3,105,3,5,102,3,6,104,3,7,106,3,8,107,3};
    u8 id=0, mask=0;
    assert(parse(&s, sparse, 7, 7) == 0);
    assert(swrm_get_master_port(&s, &id, &mask, 107, 1) == 0);
    assert(id == 7 && mask == 1);
    assert(swrm_get_master_port(&s, &id, &mask, 255, 1) == -EINVAL);
    for (u32 port=1; port<=13; port++) {
        memset(&s, 0, sizeof(s));
        u32 row[] = {port,42,3};
        assert(parse(&s, row, 1, 1) == 0);
        assert(swrm_get_master_port(&s, &id, &mask, 42, 1) == 0);
        assert(id == port-1 && mask == 3);
    }
    u32 invalid[][3] = {{0,1,1},{14,1,1},{1,256,1},{1,1,0},{1,1,256}};
    for (u32 i=0; i<5; i++) {
        memset(&s, 0, sizeof(s));
        assert(parse(&s, invalid[i], 1, 7) == -EINVAL);
    }
    u32 rows[9*3];
    for (u32 i=0; i<9; i++) { rows[3*i]=1; rows[3*i+1]=i; rows[3*i+2]=1; }
    memset(&s, 0, sizeof(s));
    assert(parse(&s, rows, 9, 7) == -EINVAL);
    puts("sparse BT port 8, boundary IDs, masks and channel overflow verified");
}
'''


def swr_unit(source):
    probe = function(source, 'swrm_probe')
    start = probe.index('\tfor (i = 0; i < map_length; i++) {')
    depth = 0
    for i in range(start, len(probe)):
        if probe[i] == '{':
            depth += 1
        elif probe[i] == '}':
            depth -= 1
            if not depth:
                loop = probe[start:i+1]
                break
    return swr_fixture.replace('/* LOOP */', loop).replace(
        '/* SOURCE */', function(source, 'swrm_get_master_port'))


def main():
    report = {'status': 'PASS', 'checks': [], 'source_sha256': {},
              'limits': 'Extracted production C with mocked kernel structures; hardware boot remains a separate check.'}
    with tempfile.TemporaryDirectory() as tmp:
        for kind, path, fixture in [('video', VIDEO, video_fixture), ('swr', SWR, swr_fixture)]:
            for variant in ['candidate', 'before']:
                source = (HERE/variant/path).read_text()
                code = (fixture.replace('/* SOURCE */', function(source, 'vb2q_init'))
                        if kind == 'video' else swr_unit(source))
                unit = Path(tmp)/(kind+'-'+variant+'.c')
                binary = unit.with_suffix('')
                unit.write_text(code)
                compiler = shutil.which('clang') or shutil.which('cc')
                subprocess.run([compiler, '-std=gnu11', '-Wall', '-Werror',
                                '-Wno-unused-parameter', '-g', '-O1',
                                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                str(unit), '-o', str(binary)], check=True)
                run = subprocess.run([str(binary)], capture_output=True, text=True)
                assert (run.returncode == 0) == (variant == 'candidate'), run.stderr
                report['checks'].append({'kind':kind, 'variant':variant,
                                         'expected_result':True, 'output':run.stdout.strip()})
                report['source_sha256'][variant+'/'+path] = hashlib.sha256(source.encode()).hexdigest()
    (HERE/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
