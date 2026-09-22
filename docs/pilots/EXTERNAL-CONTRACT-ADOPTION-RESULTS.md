# External contract adoption results (milestone #58)

Status: final measured results for the reviewed external-API boundary
library. Method, before/after numbers, trust cost, soundness gates, and the
measurement-integrity correction. Companion documents:
`docs/contracts/EXTERNAL-API-TRUST-MODEL.md` (what may be trusted and why),
`docs/pilots/EXTERNAL-API-BOUNDARY-PARETO.md` (why these symbols).

## Deliverable

- 6 reviewed bundles in `contracts/bundles/` (22 symbols) plus the audited
  allocator core `contracts/libc.yaml` (4 symbols) = **26 trusted symbols**.
- Deterministic merge tool `scripts/contracts/merge_contracts.py`
  (duplicate-symbol rejection, no last-wins; byte-identical output; SHA-256
  digest recording; merged file is a build product and is never committed).
- Static policy checker `scripts/contracts/check_bundles.py` (completeness,
  scalar-only `no_ownership_effect`, excluded-symbol blacklist, provenance
  section presence, authoritative signature table).
- Per-symbol provenance records `contracts/evidence/*.md`.
- Conformance + adversarial harness `tests/contracts/` (registered as
  CTest `cand-p0-5-contracts`).
- Measurement harness `scripts/pilots/external_boundary_experiment.py`
  (E0/E1 obligation-diff function-state measurement, AST-based mapping,
  skip-noncompiling-file handling).

No analyzer semantics changed: FlowAnalyzer, ownership/borrow state
machines, alias semantics, `canEmitCand1Pass`, cross-TU summaries,
out-owner semantics, callback retention, and realloc semantics are
untouched (verified by the fresh exact-head review and by the unchanged
verdicts on every non-contract test suite).

## Measurement design

For each project, the SAME pinned sources are checked twice; only the
contract file changes:

- **E0** (pre-milestone reviewed set): `contracts/libc.yaml` +
  `contracts/libc-borrow.yaml` merged.
- **E1** (milestone set): `contracts/libc.yaml` + `contracts/bundles/*.yaml`
  merged by `scripts/contracts/merge_contracts.py`
  (merged digest `86746ee8849172e71c79e856d61b4b9e93cbcdf83612576036294a8079ba841e`).

A function is CLEAR when no obligation and no finding maps inside its body
(AST range containment; the function universe matches cand's own
`coverage.functions_analyzed`). The harness prints gained/lost CLEAR sets
and the added/removed obligation sets; every number below was produced by
`scripts/pilots/external_boundary_experiment.py` with
`CAND_BIN=/tmp/opencode/extbuild/cand` (exact main `3fc8563`,
binary digest `e5ee4952b01cbd78ea1e00404e732eb78b54c898dda3a6d17b8ddbab0e89fd14`).

## Results

| Project (pinned) | Scope | Functions | E0 CLEAR | E1 CLEAR | Gained | Lost | Obligations E0 → E1 | Findings E0 → E1 |
|---|---|---|---|---|---|---|---|---|
| hiredis @33a12fb | 7 TUs, `-std=gnu11` | 181 | 18 (9.9%) | 23 (12.7%) | +5 | 0 | 915 → 886 (−29, +0) | 4 → 4 |
| zlib @d81c2d7 | 15 TUs | 159 | 49 | 49 | 0 | 0 | 928 → 921 (−7, +0) | 2 → 2 |
| libgit2 @0551dfd4a | first 40 `src/libgit2/*.c` | 1076 | 223 | 226 | +3 | 0 | 5659 → 5638 (−21, +0) | 0 → 0 |
| curl @a40991b97c | first 40 `lib/*.c` | 601 | 119 | 127 | +8 | 0 | 3349 → 3331 (−18, +0) | 2 → 2 |
| **total** | | **2017** | 409 | 425 | **+16** | **0** | −75, +0 | unchanged |

Gained functions:

- hiredis: `redisNetClose`, `redisContextUpdateConnectTimeout`,
  `redisContextUpdateCommandTimeout`, `sdstolower`, `sdstoupper`
  (exactly the five clearable sole-EXT functions predicted by the Pareto
  analysis; the other two sole-EXT functions are blocked by excluded
  symbols).
- libgit2: `checkout_stream_close`, `delta_make_rename`,
  `git_attr_session__init`.
- curl: `Curl_bufcp_init`, `Curl_bufq_init`, `Curl_bufq_init2`,
  `Curl_bufq_initp`, `Curl_socket_addr_from_ai`, `linux_quic_ecn`,
  `linux_quic_mtu`, `parse_expires`.
- zlib: none (its blockers are alias/pointer-arithmetic/z_stream issues,
  not external boundaries; 7 obligations still removed inside
  already-blocked functions).

In every project: zero lost CLEAR, zero added obligations, findings
unchanged, verdicts never improved from fail/incomplete to pass on any
defect-bearing input. False PASS = 0.

## Measurement-integrity correction

The intermediate measurement recorded during the experiment ("+17 CLEAR,
17→22") was an artifact of an ad-hoc mapping script that attributed
obligations to functions which contain no obligation sites (e.g. `bulklen`,
which is CLEAR in E0 already). It was replaced by the obligation-diff
harness above, and the corrected numbers were posted to issue #58 before
implementation landed. Lessons encoded in the final method: the function
universe must match `coverage.functions_analyzed`; gained/lost sets must be
derived from obligation-set diffs mapped by AST range containment; and the
harness is committed (`scripts/pilots/external_boundary_experiment.py`)
rather than ad-hoc.

## Trust cost

- 22 newly trusted symbols (plus 4 audited allocator symbols) → 16 CLEAR
  functions across 4 projects: **0.73 CLEAR per new trusted symbol**
  (0.62 per trusted symbol including the allocator core).
- Per 1,000 analyzed functions: 16 / 2.017 ≈ **7.9 CLEAR per 1,000 analyzed functions**.
- Each trusted symbol carries: an authoritative-document citation, a
  signature table entry in the checker, a provenance section, and at least
  one adversarial fixture class in `tests/contracts/`.
- The trusted surface grew by 22 symbols; the fail-closed surface
  (excluded classes) is unchanged and machine-checked (excluded-symbol
  blacklist in `scripts/contracts/check_bundles.py`).

## Soundness gates

1. **CVE replay** (`tests/cve-replay/run.sh`, new bundle flow, merged
   digest recorded in the run output): CVE-2026-87933 (cJSON) and
   CVE-2026-50219 (expat) both remain **BOUNDED-INCOMPLETE**, ASan ground
   truth reconfirmed on vulnerable and fix revisions. No MISSED.
2. **Conformance harness** (`tests/contracts/run.sh`): policy checker,
   merge determinism, duplicate/malformed-bundle rejection, digest
   mutation detection, no-committed-merge-product guard, positive fixtures
   PASS only with bundles and INCOMPLETE without, adversarial fixtures
   (`ctype_scalar_uaf.c`, `close_member_uaf.c`,
   `memchr_borrowed_return_uaf.c`, `free_then_use_malloc.c`,
   `realloc_use_after.c`, `snprintf_variadic_escape.c`,
   `excluded_symbols_uncontracted.c`) pin the sound verdicts that a wrong
   contract would break.
3. **Regression suites**: full `scripts/check.sh`, CTest (including the
   renamed `cand-p0-5-contracts` / `cand-p0-6-agent-verification` tests),
   and the fuzz regression batch all green; no contract-driven behavior
   change outside the intended obligation removals.

## Conclusion

The reviewed external-boundary library delivers a real but bounded CLEAR
gain (+16 functions across four projects, 9.9%→12.7% on Hiredis) at a
measured and auditable trust cost, with zero regressions and zero false
PASS anywhere. The remaining Hiredis Pareto is dominated by cross-TU
summaries (19 sole), same-TU undecided bodies (24 sole), and alias
precision (6 sole) — the external-API boundary is no longer the binding
constraint on this codebase.
