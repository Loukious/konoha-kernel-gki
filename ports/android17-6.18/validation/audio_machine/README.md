The SUN machine module keeps the two native compilation units and the Onyx
DAI links, audio routing, controls, MBHC calibration format and existing
amplifier-facing exports. Cards and DAI component arrays now own their copies.
The amplifier registry copies both names, retains device references, rejects
mixed families and provides checked register/unregister APIs. The machine
defers registration until the enabled Onyx amplifier family is available.

Common state exists before ASoC can invoke callbacks. Failure and removal drain
SSR registration and unregister the card before releasing common state, clock
votes and GPIO resources. The DAI exit callback stops headset detection while
its component is still alive, then releases the card-owned calibration. Sysfs
objects own their storage, input parsing is bounded, and QoS storage covers
sparse CPU indices with balanced requests.

The separate provider.patch adds snd_event_clients_free for lists whose master
registration failed. Successful master registration retains its existing list
ownership. Integrate that patch into the existing SND event target before the
machine patch, and use integration-BUILD.fragment for the one new module.

Three suites compile selected production functions with mocked kernel services
and ASan/UBSan. They cover 118 allocation, registry, sysfs, QoS, DAI, headset,
SSR and teardown cases and reject 20 compiled deliberate regressions. These
checks do not execute the full kernel probe or physical audio/DSP behavior.

Strict source checkpatch retains two reviewed advisories: card_state preserves
the native text format without a newline, and the DAI append macro accepts
fixed arrays whose ARRAY_SIZE use does not evaluate its argument. Compiler and
module symbol/version checks remain enabled.

Amplifier consumers must unregister registry entries and stop their users before
device removal. Hardware playback, capture, real PM/SSR and phone boot still
require device testing.
