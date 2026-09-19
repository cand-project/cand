# C&1-C differential corpus

The corpus is a pure function of seed, generator version, profile version, and
case count. `generate.py` writes source files plus a manifest; `runner.py`
executes every case independently through the strict cand1 agent path, checks
source-driven mechanisms and deterministic JSON, runs each temporal ASan case
in its own process, and writes `report.json`. `mutate.py` applies one named
semantic mutation and executes it through the same differential gate.

Examples:

```sh
python3 tests/fuzz/generate.py --seed 12345 --cases 1000 --output build/cand1-fast
python3 tests/fuzz/runner.py --cand build/cand --mode fast --seed 12345 --output build/cand1-fast-run
```
