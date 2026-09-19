# C&1 conformance fixtures

`manifest.json` is the normative, reviewed fixture inventory. Each entry has a
semantic class (`SAFE`, `KNOWN_VIOLATION`, or `UNSUPPORTED`) and a mechanism
set derived from its renderer. `run.py` renders and analyzes every fixture
individually through the strict authoritative cand1 agent path. Supported SAFE
fixtures must produce PASS; unsupported fixtures must remain INCOMPLETE.

Generated fuzz cases are intentionally separate: conformance preserves known
behavior; fuzzing searches for behavior that is wrong.
