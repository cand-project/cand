# C&1-C differential corpus

The corpus is a pure function of seed, generator version, profile version, and
case count. `generate.py` writes source files plus a manifest; `runner.py`
executes same-class batches, checks deterministic JSON, runs temporal ASan on
known violations, and writes `report.json`. `mutate.py` applies one named
semantic mutation and writes sidecar metadata.

Examples:

```sh
python3 tests/fuzz/generate.py --seed 12345 --cases 1000 --output build/cand1-fast
python3 tests/fuzz/runner.py --cand build/cand --mode fast --seed 12345 --output build/cand1-fast-run
```
