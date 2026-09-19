# C&1 conformance fixtures

`manifest.json` is the normative, reviewed fixture inventory. Each entry has a
semantic class (`SAFE`, `KNOWN_VIOLATION`, or `UNSUPPORTED`) and a mechanism
set. `run.py` renders each fixture deterministically and checks it through the
same `cand check --level=cand1 --format=json` interface used by the release
runner.

Generated fuzz cases are intentionally separate: conformance preserves known
behavior; fuzzing searches for behavior that is wrong.
