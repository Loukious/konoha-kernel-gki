# Camera initialization failure — 2026-11-01

High confidence cause: request-manager media entity function is overwritten during V4L2 registration.

Runtime ordinary-shell evidence (not REA Evidence):
- `fp-irq-20261101T221015Z/live-618/logcat-all.txt` lines10584-10587: `CSLHwEnumerateAndAddCSLHwDevice` fails enumeration at id `-2147483621` (`0x8000001b`, NEXT after entity27); `CSLInitializeHW` says `Failed to acquire requestManager invalid`; `HwEnvironment` raises SIGABRT.
- `/tmp/onyx-camera-dumpsys.txt`: service reports zero cameras. This proves initialization failed before any camera can be opened.
- Same abort sequence repeats for successive service PIDs.

Source chain:
- Current `onyx/qcom/opensource/camera-kernel/drivers/cam_req_mgr/cam_req_mgr_dev.c:1035` assigns `CAM_VNODE_DEVICE_TYPE` before `video_register_device`; registration at1036, no restoration afterwards.
- `common/drivers/media/v4l2-core/v4l2-dev.c:817` `video_register_media_controller`, called at1089 from registration, sets `entity.function = MEDIA_ENT_F_UNKNOWN` at830 and `MEDIA_ENT_F_IO_V4L` at835 for VFL_TYPE_VIDEO. It also populates device major/minor and registers the entity.
- `CAM_VNODE_DEVICE_TYPE == MEDIA_ENT_F_OLD_BASE == 0x10000`; generic `MEDIA_ENT_F_IO_V4L == OLD_BASE+1 == 0x10001`, colliding with vendor `CAM_SENSOR_DEVICE_TYPE`. CamX cannot identify request manager by expected type.
- Pinned original: project `baseline/kernel/xiaomi/sm8735-modules`, revision `8500748dae32f10507c02f1c94bc971553cb525b`, blob `a011b5f066de6ba18b90ff2743850ec0b7ac64ed`, same source path. Original registration line927 precedes native type assignment939. `/tmp/onyx-camera-native-req-mgr.c` retains exact original.
- Patch0119 moved native type assignment ahead of registration (patch lines2543 and2556). Both original6.6 and current6.18 generic V4L2 registration reset the type; the ordering introduced by port patch is the regression.
- Generic `media_device_enum_entities` in common mc-device.c:113 copies `ent->function` to user-visible `entd->type`; values0x10000/0x10001 fall in legacy range and are retained, so this change matters directly to the ioctl ABI.

Correction: `/tmp/onyx-camera-proposed.patch` removes preregistration type assignment and restores it immediately after successful registration, reproducing native ordering. Existing pre-registration pad initialization and failure cleanup remain intact. Only camera module changes; no common/GKI change needed.

Suggested validation: extract exact candidate/current `cam_video_device_setup`, use the pinned generic registration assignment block as behavior of fake video registration, assert successful setup returns vendor entity function0x10000, name and retained major/minor. Current setup must fail this success assertion (it returns generic0x10001). Check allocation failure, pads-init failure and video-registration failure preserve cleanup and do not perform post-registration writes. Reuse existing camera teardown/lifetime validations.

Limits: error16/charging/Bluetooth unrelated to this camera cause; physical preview and sensor probe still require live candidate validation. Runtime exact media ioctl enumeration was not directly acquired because rootless shell cannot open camera-group device nodes and phone modification was excluded from this subtask. Main cause combines observed CamX failure with exact original/current/kernel source ordering, not decompilation.
