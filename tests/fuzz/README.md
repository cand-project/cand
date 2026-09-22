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

## Cumulative campaign accounting

`runner.py` embeds full provenance in every report (`cand_sha256`,
`generator_sha256`, `source_commit`) so runs are attributable and auditable.
`accumulate.py` aggregates report files into cumulative campaign evidence:

```sh
python3 tests/fuzz/accumulate.py build/cand1-c-extended-*/report.json --output build/cumulative.json
```

A **unique campaign case** is the triple `(generator_sha256, seed, case index
within the seed corpus)`. Because the corpus is a deterministic function of
the generator and seed, re-running an identical pair cannot inflate the
cumulative total: the accumulator counts each `(generator_sha256, seed)` pair
exactly once and records duplicate runs separately. Reports lacking
provenance, or carrying any false PASS, false positive, coverage gap, wrong
failure class, harness error, non-deterministic output, or ASan-confirmed
false PASS, are rejected loudly and contribute nothing.

## Mutation operators

`mutate.py` defines the single-operation semantic mutation suite. Each
operator declares the verdict class the mutated program requires:

- `KNOWN_VIOLATION` — the mutation introduces a defect C& must detect
  (`fail`);
- `UNSUPPORTED` — the mutation introduces semantics outside the decidable
  scope that must fail closed (`incomplete`), never `pass`;
- `SAFE` — the mutation preserves safety and must still `pass`.

The suite includes the historical soundness-incident mechanisms as generator
mechanisms (for example `VARIADIC_ESCAPE_LIVE`/`VARIADIC_ESCAPE_DEAD` pin
incident #53's variadic-argument escape class).
