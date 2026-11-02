# Delayed-task migration accounting

The CVP-cells hardware run panicked at 8.770198 seconds while removing
HeapTaskDaemon (PID 1518) from the default related-thread group. CPU2's new-task
current total was 108980, below the task's 196710 contribution. The main group
current and previous totals had already wrapped below zero.

In the pinned 6.18 scheduler, a delayed CFS task can remain queued while
`task_is_runnable()` is false. An affinity migration physically moves that
entity and preserves `sched_delayed`. WALT's port incorrectly used runnable
membership to gate its source runtime subtraction, and skipped destination
runtime addition at the runnable-only enqueue gate. The retained task window
totals then belong to a different CPU from the task. Group removal and further
migration can subtract those totals from a CPU that never received them.

Patch 0142 restores queue membership for the source runtime transfer and
finishes the destination runtime transfer before filtering runnable statistics.
It keeps delayed tasks excluded from runnable counts and enqueue notifications.
The hardware trace proves corrupted totals, but does not identify the first
bad migration; the new candidate still needs a hardware test.

`python3 run.py` extracts the complete migration functions and the enqueue hook
prefix from the candidate source, plus the actual runnable enqueue predicate.
It freezes time and stubs scheduler locks, window updates and unrelated
statistics. The cases assert the four retained-runtime totals on every CPU,
completed migration state, and delayed/runnable enqueue counting. They cover
intra- and inter-cluster moves, zero and large contributions, old and new tasks,
two successive delayed migrations, delayed wakeups, migrating runnable tasks,
TASK_WAKING, and unqueued sleeping tasks. AddressSanitizer and UBSan are enabled.

The original source and independent regressions of either migration gate must
fail. This verifies transfer bookkeeping; it does not emulate real scheduler
concurrency, cycle scaling, aging, or window rollover. Full build/module CRC
checks and device testing provide separate evidence.
