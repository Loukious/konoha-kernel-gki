from pathlib import Path
import sys,json,tempfile,subprocess,shutil
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'audio_machine'))
from verify_smartpa import function
COMMON=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/common/drivers/media/v4l2-core/v4l2-dev.c')
api=COMMON.read_text()
register=function(api,'video_register_media_controller')
assert 'vdev->entity.function = MEDIA_ENT_F_UNKNOWN;' in register
assert 'case VFL_TYPE_VIDEO:\n\t\tintf_type = MEDIA_INTF_T_V4L_VIDEO;\n\t\tvdev->entity.function = MEDIA_ENT_F_IO_V4L;' in register
fixture=r"""
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define CAM_VNODE_DEVICE_TYPE 0x10000
#define MEDIA_ENT_F_IO_V4L 0x10001
#define VFL_TYPE_VIDEO 0
#define V4L2_CAP_VIDEO_CAPTURE 1
struct entity { unsigned int function; const char *name; };
struct video { void *v4l2_dev; char name[32];void (*release)(struct video *);void *fops,*ioctl_ops;int minor,vfl_type,device_caps;struct entity entity; };
static struct { struct video *video;void *v4l2_dev; } g_dev;
static int g_cam_fops,g_cam_ioctl_ops;
static int fail_alloc,fail_pads,fail_register,cleanup,releases,registered;
static void video_device_release_empty(struct video *v) { (void)v; }
static struct video *video_device_alloc(void) { return fail_alloc?NULL:calloc(1,sizeof(struct video)); }
static void video_device_release(struct video *v) { assert(v);releases++;free(v); }
static void strscpy(char *dst,const char *src,size_t n) { assert(strlen(src)<n);strcpy(dst,src); }
static int media_entity_pads_init(struct entity *e,int count,void *pads) { assert(e && !count && !pads);return fail_pads; }
static void media_entity_cleanup(struct entity *e) { assert(e);cleanup++; }
/* Pinned V4L2 API: registration unconditionally assigns generic function. */
static int video_register_device(struct video *v,int type,int n) { assert(type==VFL_TYPE_VIDEO && n==-1);v->entity.function=MEDIA_ENT_F_IO_V4L;if(fail_register)return fail_register;registered++;return 0; }
static const char *video_device_node_name(struct video *v) { assert(v);return "video0"; }
/* SOURCE */
static void reset(void) { assert(!g_dev.video);fail_alloc=fail_pads=fail_register=cleanup=releases=registered=0; }
int main(void) {
 reset();fail_alloc=1;assert(cam_video_device_setup()==-ENOMEM && !g_dev.video && !cleanup && !releases);
 reset();fail_pads=-EINVAL;assert(cam_video_device_setup()==-EINVAL && !g_dev.video && !cleanup && releases==1);
 reset();fail_register=-EBUSY;assert(cam_video_device_setup()==-EBUSY && !g_dev.video && cleanup==1 && releases==1 && !registered);
 reset();assert(cam_video_device_setup()==0 && g_dev.video && registered==1 && !cleanup && !releases);
 assert(g_dev.video->entity.function==CAM_VNODE_DEVICE_TYPE);
 assert(!strcmp(g_dev.video->entity.name,"video0"));
 video_device_release(g_dev.video);g_dev.video=NULL;
}
"""
rel='qcom/opensource/camera-kernel/drivers/cam_req_mgr/cam_req_mgr_dev.c'
results=[]
for version in ['candidate','before']:
 s=function((HERE/version/rel).read_text(),'cam_video_device_setup')
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'camera.c';p.write_text(fixture.replace('/* SOURCE */',s));exe=Path(tmp)/'camera';r=subprocess.run([shutil.which('cc'),'-Wall','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True)
  if r.returncode:raise RuntimeError(r.stderr.decode())
  r=subprocess.run([str(exe)],capture_output=True)
 assert (r.returncode==0)==(version=='candidate'),(version,r.stderr)
 results.append({'version':version,'exit':r.returncode})
report={'status':'PASS','results':results,'api':'Pinned V4L2 registration generic function overwrite verified; exact setup success/failure paths extracted','limits':'Host validates legacy entity identity and cleanup, camera HAL preview requires hardware validation'}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
