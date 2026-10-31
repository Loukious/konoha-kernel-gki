# Native media and DSP build inventory

The ROM build receipts select six modules below. Every discovered vendor source
and header matches the pinned modules revision
`8500748dae32f10507c02f1c94bc971553cb525b`; no native working-tree overlay was
found in this selection. The JSON records bind native modules, object lists,
compiler receipts, flags, includes, dependencies, source pins and aliases.

| Module | Native units | Fresh vendor inputs | Existing vendor inputs |
| --- | ---: | ---: | ---: |
| camera | 148 | 485 | 11 |
| cameralog | 1 | 2 | 0 |
| cameramsger | 5 | 10 | 0 |
| msm-eva | 24 | 54 | 4 |
| msm_video | 38 | 84 | 0 |
| frpc-adsprpc | 2 | 6 | 0 |

Counts describe each module independently, so shared inputs must be deduplicated
at integration. Kernel headers are recorded separately and require comparison
with current GKI and already imported providers. The video port adds three
native clock-binding headers and two native Kbuild/configuration receipts,
for 89 total new inputs. All 38 video units, including the native Sun/Tuna/Kera
and Iris33/Iris35 selection, are retained. This does not require adding all
alternative hardware clock drivers.

Camera still requires the flash-LED provider. EVA requires FastRPC. The native
module dependencies for video and FastRPC already have central providers;
API compatibility and lifetime behavior still need porting and validation.
This inventory is source/build evidence, not a hardware or flashability test.

`discover.py` reads the preserved local ROM build and pinned workspace. It
writes reports in its own directory and does not modify native sources or
stage modules. Original native build receipts must be available to repeat it.
