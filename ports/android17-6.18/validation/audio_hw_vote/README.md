# Optional-device LPASS vote contract

Run `python3 /tmp/onyx-audio-hw-vote-validation/verify.py` to regenerate the exact caller/provider extracts and execute the candidate and six rejecting controls. --source-root/--cc overrides are supported. Outputs remain in this temp directory. before.c/candidate.c freeze the manager change independently of later shared-source mutations; source/helper hashes and results are recorded in validation.json.

harness.c models CCF reference counting, list allocation, mutexes and the firmware API. Its calls execute exact production common/helper/clock callbacks. This is host verification, not hardware audio validation. Negative controls disable core dumping.

Proposed patch: /tmp/onyx-audio-hw-vote-proposed.patch.
Findings: /tmp/onyx-audio-hw-vote-findings.md.
