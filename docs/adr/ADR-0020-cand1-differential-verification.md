# ADR-0020 — C&1 differential verification infrastructure

Status: Accepted for C&1-C

## Decision

Keep normative conformance fixtures separate from generated fuzz cases. The
conformance manifest preserves reviewed behavior; the deterministic generator
and semantic mutation engine search for false PASS results. The existing JSON
check interface is the only analyzer integration point, and the runner emits
release-qualification reports rather than program-verification evidence.

Every generated source is analyzed individually through the authoritative
strict cand1 agent path. Mechanism coverage is source-driven: the generator
records a transformation plan, renders it, and validates that declared
mechanisms occur in the analyzed source. Metadata-only labels do not count.
Supported SAFE fixtures must exercise the positive PASS path; SAFE plus
INCOMPLETE is an explicit coverage gap.

Temporal ASan is an independent bug-finding oracle only. The shared-binary
optimization is compile-once only: each case ID runs in a separate process, so
each temporal count is attributable to exactly one case. A temporal sanitizer
finding paired with cand1 PASS fails immediately; sanitizer cleanliness never
upgrades INCOMPLETE to PASS.

Protocol and frontend inputs are fuzzed with a local fail-closed validator.
Malformed, path-traversal, duplicate-key, digest, trust, and argument cases
must never become authoritative proof. The runner separately checks repeated
JSON output for byte-stable analyzer results.

## Consequences

* Safe, invalid, and unsupported sources are all represented; the corpus is not
  rejection-biased.
* Every mutation operator is an executable differential case with structural
  source assertions and an expected semantic class.
* Independent review counts are individual analyzer verdicts, not batch
  classifications.
* The report exposes semantic mechanism coverage, result taxonomy, sanitizer
  categories, protocol authority bypasses, timing, and reproducibility.
* A known false PASS is a blocker and must become a minimized permanent test;
  false FAIL/INCOMPLETE results are tracked separately.
* C&1's public claim stays disabled. C&1-D and issue #26 remain open.

## Rejected alternatives

* Using ASan as an ownership oracle: path-dependent and incomplete.
* Random token mutation: mostly non-compiling noise and no semantic oracle.
* Binding fuzz reports into per-source evidence: qualification evidence is not
  proof for an individual program.
