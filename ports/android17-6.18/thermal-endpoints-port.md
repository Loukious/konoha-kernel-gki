# Native Qualcomm thermal endpoints

Root integration record reconstructed from the frozen source, build5 log,
configuration/export receipts and sanitizer report. The worker completed its
freeze receipts before its service stopped; it did not write summary.md.
Frozen inputs and source are unchanged.

Twelve pinned core inputs build seven native compilation units into five
modules: qti_cpufreq_cdev, qti_devfreq_cdev, qti_userspace_cdev, qti_qmi_cdev
and qti_qmi_sensor_v2. The two QMI modules use the existing qmi_helpers provider;
the other three have no module dependencies. All five retain their native empty
export/alias sets and module exit routines. Existing thermal/provider headers
are reused. Native protocol schemas, sensor IDs and cooling client names remain
byte-identical. The isolated build verifies 157 imported symbol CRCs.

CPU and GPU cooling retain native frequency/QoS behavior while checking sparse
CPU tables, OPP counts, frequency conversion, policy references and registration
failures. Deferred registration and callbacks are withdrawn before storage is
released. Userspace cooling validates level bounds and balances device-node
ownership when setup fails.

QMI receive callbacks update connection state and queue work without waiting
for transactions on their own receive worker. Transactions serialize separately,
use explicit QRTR server addresses, and reject stale connection generations.
Cooling requests retain desired state for replay. Sensor handling retains the
native cached-temperature contract, validates lists and paging, and retries
threshold registration after failures or reconnect. Removal withdraws entry
points and drains work before releasing QMI handles and thermal objects.

Three ASan/UBSan suites exercise frequency/QoS setup, cooling transport and
sensor transport, including 200 removal races. All 38 deliberate regressions
compile and are rejected. Strict checkpatch and exact zero-fuzz/zero-offset
source replay pass. The harness uses native protocol structs and name tables
with mocked kernel thermal, QoS, QMI, DT, device and workqueue services. It does
not execute the actual QMI codec, kernel frameworks, modem/DSP firmware or
physical temperature/thermal-control behavior. No boot or device validation
is claimed.
