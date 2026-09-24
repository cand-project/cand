# C&1/v1 real-project baseline

Status: initial zero-annotation pilot baseline. This report is evidence for
adoption planning, not a new qualification of C&1/v1 and not a claim about
the upstream projects.

The C&1 authority remains SPEC-0010 and the qualified verifier state remains:

- reviewed qualification HEAD:
  `7a6f4b65fb6e7506d073f9c93aa615c0e6bf8860`;
- qualified merge/release identity:
  `3a2672b6b6a6742c8ac19c2894a698cdd1970b7a`;
- current protected-main release commit:
  `0ee772ebe805f7f93ed3da15cd5a1a588902f669`.

No C& source, ownership rule, borrow rule, evidence rule, proof policy,
contract, or toolchain semantic source was changed for this pilot. Upstream
trees were not changed; build products and disposable analysis workspaces
were kept outside this repository.

## Method and limits

The environment was Ubuntu 24.04.4 LTS, x86_64, using Clang 18.1.3 and C11.
The strict generated-policy invocation was run once per selected translation
unit with zero new C& annotations and zero trusted contracts. A verdict is
counted by physical C source lines in the selected representative scope.
`coverage` and `decidable coverage` therefore describe the measured scope,
not the whole upstream project.

The baseline runner records wall time and maximum resident set size for each
translation unit. Its cause labels are conservative, non-exclusive evidence
labels: one translation unit can expose more than one unsupported condition.
An explicit cross-TU classifier was not emitted by this per-TU experiment;
cross-TU counts below are consequently reported as `0 explicit / not
measurable`, never inferred as support.

## Exact upstream pins and feasibility

| Stage | Project | Exact upstream commit | Source C files / C LOC | Build and test baseline |
|---|---|---|---:|---|
| PILOT-0 | madler/zlib | `d81c2d7eb705c62294ba03299255672078e89115` | 43 / 25,666 | CMake + Ninja with Clang; build passed; CTest 15/15 passed |
| PILOT-0 | redis/hiredis | `33a12fb23531f33e3455c7ed46008c20c7ad9c78` | 25 / 10,587 | CMake + Ninja with Clang; build passed; CTest 1/1 passed with the pinned Redis server on PATH |
| PILOT-1 | libgit2/libgit2 | `0551dfd4ad989b6a3d5683c0d4cf326c6efef929` | 326 / 216,728 | CMake + Ninja with Clang; build passed; CTest 12/12 passed |
| PILOT-2 | redis/redis | `5f08991bce470ab721f10d7f815baad49f3aae60` | 424 / 302,533 | upstream full Clang build, TLS disabled: passed; full upstream test suite passed |
| PILOT-3 | SQLite mirror | `30fbf300af101e53c034001877d9d5cbf7c6eb42` | 315 / 422,618 | `configure; make sqlite3` with Clang: passed; `make test` blocked by unavailable Tcl SQLite extension |
| PILOT-4 | curl/curl | `a40991b97c0ce721e85acbe16e16fe79b7b68c51` | 374 / 197,598 | CMake + Ninja with Clang: build passed; selected CTest configuration registered no tests |
| PILOT-4 | libevent/libevent | `d82464a277d0f42703702c4dfd9af6af38595a83` | 100 / 80,949 | CMake + Ninja with Clang; CTest suite was run |
| PILOT-5 | jemalloc/jemalloc | `84286c27f090e264edbbb38cc0ae615e25fbb78f` | 232 / 76,725 | upstream `autogen.sh` blocked because Ubuntu image has no `autoconf`; retained as a boundary/non-claim case |

The source-file and C-LOC inventory excludes `.git`, build directories, and
directories named `tests`; C LOC is physical lines in `.c` files. CMake
projects generated `compile_commands.json`. Redis and SQLite have deterministic
upstream Make/configure compilation, but no compile database was produced in
this initial baseline. The Redis representative run used the generated
jemalloc headers from its ordinary build workspace, not a source modification.

The qualified sanitizer toolchain is available: Clang accepts
`-fsanitize=address,undefined -fno-omit-frame-pointer`. zlib and hiredis
sanitizer build/test feasibility was exercised in disposable build
directories; projects whose ordinary build/test was blocked remain sanitizer
feasibility follow-ups rather than claimed sanitizer passes.

## Required project summary

The following table is the requested project-level handoff. The analyzed
scope is representative unless marked aggregate; whole-project C LOC and file
counts are in the feasibility table above. All initial runs used zero
annotations and zero trusted contracts, and no upstream build was changed.

| Project | Commit | C LOC | Files | Analyzed LOC | PASS | FAIL | INCOMPLETE | Top INCOMPLETE cause | Annotations | Contracts | Runtime | ASan comparison | False PASS | Build changed? | Pilot role |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---|---:|---|---|
| zlib | `d81c2d7` | 25,666 | 43 | 9,722 | 0 | 0 | 9,722 | unknown external call | 0 | 0 | 1.39 s | zlib sanitizer CTest 15/15 passed | 0 | No | semantic stress |
| hiredis | `33a12fb` | 10,587 | 25 | 5,509 | 370 | 0 | 5,139 | unknown external call | 0 | 0 | 0.79 s | sanitizer build; test blocked by Redis PATH in that run | 0 | No | adoption bridge |
| libgit2 | `0551dfd` | 216,728 | 326 | 27,911 | 0 | 0 | 27,911 | unknown external call | 0 | 0 | 10.06 s | not run; ordinary CTest 12/12 passed | 0 | No | semantic stress |
| Redis staged | `5f08991b` | 302,533 | 424 | 106,156 | 13 | 0 | 106,143 | allocator/external boundary | 0 | 0 | 11.07 s | not run; clean full Clang build and upstream tests passed | 0 | No | semantic stress |
| SQLite canonical | `30fbf300` | 422,618 | 315 | 97,127 | 0 | 0 | 97,127 | unknown external call | 0 | 0 | 4.47 s | not run; Tcl test prerequisite blocked | 0 | No | semantic stress |
| SQLite amalgamation | generated from `30fbf300` | 422,618 | 315 | 270,758 | 0 | 0 | 270,758 | aggregate/external boundary | 0 | 0 | 4.31 s | not run | 0 | No | semantic stress |
| curl | `a40991b9` | 197,598 | 374 | 23,118 | 2,621 | 0 | 20,497 | unknown external call | 0 | 0 | 5.82 s | not run; selected CTest registered none | 0 | No | adoption candidate |
| libevent | `d82464a2` | 80,949 | 100 | 37,035 | 279 | 0 | 36,542 | unknown external call | 0 | 0 | 4.46 s | not run; ordinary CTest suite completed | 0 | No | semantic stress |
| jemalloc | `84286c27` | 76,725 | 232 | 5,421 | 0 | 0 | 0 | generated-header/tool error | 0 | 0 | 0.85 s | blocked with upstream autoconf prerequisite | 0 | No | explicit non-claim |

## Zero-annotation measurements

| Project / scope | Analyzed files | Analyzed LOC | PASS | FAIL | INCOMPLETE | Tool errors | Runtime | Peak RSS | Coverage | Decidable |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| zlib core representative | 15 | 9,722 | 0 | 0 | 9,722 | 0 | 1.39 s | 87.5 MiB | 0.00% | 0.00% |
| hiredis core representative | 7 | 5,509 | 370 | 0 | 5,139 | 0 | 0.79 s | 82.3 MiB | 6.72% | 6.72% |
| libgit2 `src/libgit2` sample | 40 | 27,911 | 0 | 0 | 27,911 | 0 | 10.06 s | 89.2 MiB | 0.00% | 0.00% |
| curl `lib/*.c` sample | 40 | 23,118 | 2,621 | 0 | 20,497 | 0 | 5.82 s | 84.3 MiB | 11.34% | 11.34% |
| libevent core sample | 30 | 37,035 | 279 | 0 | 36,542 | 214 | 4.46 s | 101.0 MiB | 0.75% | 0.75% |
| SQLite canonical sample | 20 | 97,127 | 0 | 0 | 97,127 | 0 | 4.47 s | 107.2 MiB | 0.00% | 0.00% |
| SQLite generated amalgamation | 1 | 270,758 | 0 | 0 | 270,758 | 0 | 4.31 s | 521.1 MiB | 0.00% | 0.00% |
| Redis SDS | 2 | 2,907 | 0 | 0 | 2,907 | 0 | 0.27 s | 79.7 MiB | 0.00% | 0.00% |
| Redis dict/hash | 6 | 15,902 | 0 | 0 | 15,902 | 0 | 1.00 s | 89.4 MiB | 0.00% | 0.00% |
| Redis object/value | 10 | 25,647 | 0 | 0 | 25,647 | 0 | 3.06 s | 128.6 MiB | 0.00% | 0.00% |
| Redis client/network | 7 | 13,964 | 0 | 0 | 13,964 | 0 | 1.74 s | 108.4 MiB | 0.00% | 0.00% |
| Redis broader core | 15 | 47,736 | 13 | 0 | 47,723 | 0 | 5.00 s | 136.6 MiB | 0.03% | 0.03% |
| Redis staged aggregate | 40 | 106,156 | 13 | 0 | 106,143 | 0 | 11.07 s | 136.6 MiB | 0.01% | 0.01% |
| jemalloc boundary sample | 3 | 5,421 | 0 | 0 | 0 | 5,421 | 0.85 s | 87.3 MiB | 0.00% | 0.00% |

Annotations added: 0. Trusted contracts added: 0. Confirmed false PASS:
0. No FAIL was observed in the selected zero-annotation scopes; this is not
evidence that the upstream projects contain no defects.

## INCOMPLETE taxonomy

Observed causes were kept separate rather than reported as one unsupported
bucket:

- unknown external call: the dominant cause in zlib, hiredis, libgit2, curl,
  libevent, SQLite, and the Redis scopes;
- unsupported alias/storage: repeatedly co-occurring with unknown external
  effects in the current strict reports;
- aggregate transport: observed in SQLite canonical and amalgamated runs;
- atomics: observed in SQLite canonical and amalgamated runs;
- other: frontend/tool/report conditions not safely assigned to a narrower
  semantic category;
- compiler/header/tool error: distinguished from INCOMPLETE, including
  libevent's one selected translation-unit tool error and jemalloc's generated
  header failures.

The following categories remain required taxonomy slots but were not
authoritatively observed in these selected files: callback/retention, realloc,
pointer/integer provenance, union, varargs, nonlocal control flow, and explicit
cross-TU ownership effect. Their absence from this sample is not support.

## SQLite experiment

The SQLite GitHub mirror commit is
`30fbf300af101e53c034001877d9d5cbf7c6eb42`. The authoritative Fossil
check-in identity at that mirror state is
`977e0faac735409fd06d61851a30686b471bcfc3f1dcfba2debe2b4a3d42b5e8`.
The generated `sqlite3.c` contains 270,758 physical lines and has SHA-256
`f5f945c63e5fce952275e2b4624ef7c9475756987c830da5598e366c4f02faf4`.

| Form | Analyzed LOC | PASS | FAIL | INCOMPLETE | Explicit cross-TU labels | External-effect proxy | Other causes | Runtime |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| canonical source sample | 97,127 | 0 | 0 | 97,127 | 0 / not measurable | 20 TUs | aggregate transport, atomics, other | 4.47 s |
| generated amalgamation | 270,758 | 0 | 0 | 270,758 | 0 / not measurable | 1 TU | aggregate transport, atomics, other | 4.31 s |

The amalgamation reduces the number of translation-unit boundaries in the
input, but it does not demonstrate general cross-TU support. The external
effect counts are only a diagnostic proxy for the current per-TU experiment.

## Redis and Hiredis

The staged Redis aggregate is 40 translation units and 106,156 analyzed
physical C lines. The dominant baseline blocker is the allocator boundary:
Redis source includes generated jemalloc headers and the ordinary build
produces those headers outside the pristine source tree. After supplying
those generated headers from the normal build workspace, the semantic runs
were still overwhelmingly INCOMPLETE because of external calls and alias or
storage effects. The selected core sample also exercises ownership transfer,
reference-counted object lifecycle, callbacks/modules, and network/external
library boundaries; no annotation or trusted contract was introduced to
turn any of those into PASS.

Hiredis is the smallest useful bridge baseline: its ordinary build passed and
its test passed when the pinned Redis server was available. It produced 370
PASS LOC and 5,139 INCOMPLETE LOC without annotations or contracts. It is the
recommended first integration/annotation pilot after this baseline, before
modifying Redis.

## Pilot value and effort estimate

| Project | Role | Annotation-free useful coverage | Estimated next effort |
|---|---|---:|---|
| hiredis | adoption candidate / bridge | 6.72% of selected LOC | small focused boundary contracts and callback/retention review; preserve zero-annotation result as the control |
| curl | adoption candidate, subject to test-fixture completion | 11.34% of selected LOC | medium; first establish a real CTest/ASan test scope, then classify external ownership boundaries |
| zlib | semantic stress candidate | 0.00% of selected LOC | medium; allocator and external-call boundary inventory before any annotation |
| libevent | semantic stress candidate | 0.75% of selected LOC | medium/high; callbacks, event retention, and test/tool errors require separate accounting |
| libgit2 | semantic stress candidate | 0.00% of selected LOC | high; large cross-library and cross-TU boundary inventory |
| Redis | semantic stress candidate | 0.01% of staged LOC | very high; allocator generation, ownership transfer, object reference counts, modules, and network callbacks must be separated |
| SQLite | semantic stress candidate | 0.00% of selected LOC | high; canonical build/test tooling and extensive external/global state need a dedicated scope |
| jemalloc | non-claim stress candidate | 0.00% of boundary sample | first make the upstream toolchain available; keep as an explicit boundary/non-claim control |

Coverage should not be ranked by PASS percentage alone. Hiredis has the best
initial adoption signal because its ordinary build/test path is small,
repeatable, and Redis-relevant; SQLite and Redis provide the strongest
semantic-stress evidence; jemalloc is useful specifically because it is not a
qualified C&1 claim candidate today.

## Sanitizer and defect control

ASan/UBSan flags are available in the qualified Clang toolchain. zlib's
disposable sanitizer build/test path was exercised. Hiredis sanitizer
configuration/build was attempted but its test invocation did not produce a
passing sanitizer baseline on this host; this is recorded as feasibility debt,
not as a C& result. The remaining projects retain their ordinary-build/test
results above and require project-specific sanitizer harness completion before
their temporal-defect evidence can be used for a later annotation phase.

No confirmed temporal defect was found in the executed baseline/test work, so
the false-PASS response procedure was not triggered. Confirmed false PASS
count: 0. Any future sanitizer-confirmed temporal violation receiving C&1
PASS stops the pilot and follows `docs/CAND1-FALSE-PASS-RESPONSE.md`.

## Post-adoption re-measurement (E2/E2+, milestone #36)

After the adoption program (#58–#71) landed, the pilot matrix above was
re-measured on the protected main HEAD `c4429f5` (PR #71; verifier binary
SHA-256 `dd9b74ad910d57e5…`, tree-identical to the reviewed `226d3eb`).
This section is the post-adoption record; the tables above are unchanged
history and are never silently replaced — every delta is attributed below.
Committed harness: `scripts/pilots/realworld_e2.py` (method, invariants,
and the regression-guard semantics are documented in its docstring).

Three configurations per pilot, identical scopes and flags:

- **B0** — zero annotations, zero trusted contracts (replicates this
  baseline's condition on the current tool);
- **E2** — the reviewed #58 contract library (libc/posix bundles, 26
  symbols; merged bundle SHA-256 `86746ee8849172e7…`);
- **E2+** — E2 plus a measurement-only pointer-output contract bundle
  (8 symbols; SHA-256 `597d1e085e1b9411…`; `/tmp` only, never committed)
  enabled with `--pointer-output-contracts`.

### Method deltas vs the original baseline runs (all recorded, none hidden)

- Per-TU verdicts use an agent-equivalent acceptance predicate on
  non-agent `--level=cand1 --format=json` runs (fail = findings;
  pass = no findings and no unsupported; incomplete = otherwise).
  Equivalence with the original generated-policy machinery was verified
  empirically on four hiredis TUs (exact match on all four).
- Function-level CLEAR is keyed per (file, function) and computed from
  Clang AST main-file ranges; macro-expansion-location definitions
  (start line 0) are excluded and obligations that land at macro
  invocation lines outside real ranges are counted as unmapped, never
  dropped (full accounting in the runner docstring).
- Scopes are the original ones where recoverable exactly (zlib 15 files
  9,722 LOC; hiredis 7 files 5,509; libgit2 first-40-sorted 27,911) and
  published reconstructions otherwise: curl first-40 23,522 vs 23,118
  (+404, +1.7%); libevent 30-file 37,162 vs 37,035 (+127, excluding the
  seven platform files the Linux build does not compile); SQLite
  canonical core-20 102,589 vs 97,127; Redis staged 40-file 89,864 vs
  106,156 (original lists lost with the disposable runners; the SDS
  stage matches exactly at 2,907).
- The regression guard fails the run on lost CLEAR or on obligations or
  findings added inside baseline-CLEAR functions. Guard events are
  adjudicated, never exempted.

### E2 matrix (function-level, guard baseline = B0)

| Pilot | LOC | Functions (AST / cand) | B0 obligations / CLEAR / findings | E2 | E2+ | Guard |
|---|---:|---:|---|---|---|---|
| zlib | 9,722 | 159 / 159 | 1,023 / 49 / 2 | 911 / 51 / 2 | 911 / 51 / 2 | PASS |
| hiredis | 5,509 | 181 / 181 | 1,031 / 13 / 0 | 886 / 20 / 1 | 889 / 20 / 1 | PASS |
| libgit2 | 27,911 | 942 / 1,055 | 6,071 / 71 / 0 | 5,711 / 87 / 0 | 5,737 / 87 / 0 | PASS |
| curl | 23,522 | 549 / 549 | 3,450 / 56 / 0 | 3,315 / 69 / 0 | 3,315 / 69 / 0 | PASS |
| libevent | 37,162 | 1,176 / 1,204 | 5,318 / 150 / 0 | 4,846 / 164 / 3 | 4,848 / 164 / 3 | VIOLATION (adjudicated) |
| Redis staged | 89,864 | (Gate C) | (Gate C) | (Gate C) | (Gate C) | (Gate C) |
| SQLite canonical | 102,589 | (Gate C) | (Gate C) | (Gate C) | (Gate C) | (Gate C) |
| SQLite amalgamation | 270,758 | 1 TU | (Gate C) | (Gate C) | (Gate C) | (Gate C) |
| jemalloc sample | 5,421 | — | blocked (no `autoconf`, no shipped `configure`): retained as the explicit non-claim, unchanged from this baseline ||||

Unmapped obligations (macro-invocation-line rows, libgit2 and libevent
only) are recorded per configuration in the runner JSON and never enter
CLEAR. Excluded macro-location functions can never be counted CLEAR, so
the exclusion direction is conservative.

### Program-level deltas vs this baseline, fully attributed

- hiredis B0 PASS LOC 370 → 280: `alloc.c:48`
  global-or-static-pointer-storage, introduced by #58 (ad25748).
- zlib B0 0 → 1,390 FAIL LOC: two CAND-O006 at `gzread.c:666` /
  `gzwrite.c:720` (free of a borrowed-param-derived pointer in
  `gzclose_r`/`gzclose_w`), introduced by #58's allocator-core modeling.
  Fail-closed at the annotation boundary on correct code; not a
  regression and not a false PASS.
- hiredis E2 CLEAR 23 (#58-era) → 20: three functions
  (`redisContextUpdateConnectTimeout`, `…CommandTimeout`, `sds_malloc`)
  previously CLEAR only via unsound invented `borrow_from_arg` facts for
  function-pointer allocator dispatch (`hi_malloc`/`s_malloc` through
  `hiredisAllocFns`); removed by the #64 compound-origin fix (928d9bc).
- hiredis findings 4 → 1: #64 removed three B003s; `read.c:168`
  remains (cursor-advance borrow idiom, see guard events).
- curl and libevent per-TU verdict buckets shifted (curl PASS LOC
  2,621 → 3,648; libevent 279 → 1,310): attributable to the scope
  reconstruction deltas above plus ten milestones of diagnostic
  evolution; the original per-TU records are unrecoverable, so these
  are aggregate-level attributions only.

### Guard events and adjudications

- **libevent, adjudicated violation**: `evutil_set_tcp_keepalive` lost
  CLEAR under E2/E2+ via a new `unmodelled-pointer-parameter` at
  `evutil.c:3211` — `setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on,
  sizeof(on))`. In B0 the unknown call left `&on` untracked; the
  reviewed setsockopt contract forces tracking of the address-of-local,
  which the model cannot classify. Same idiom as hiredis `net.c:248`.
  Fail-closed precision cost of adoption on correct code; no finding,
  no false PASS; the guard was not weakened.
- **Cursor-advance borrow idiom (correct C, fail-closed findings)**:
  hiredis `read.c:168` and libevent `buffer.c:1541/1542/1544`
  (`find_eol_char`): memchr-derived interior pointers returned while
  the cursor parameter is compound-reassigned, tripping the #62/#64
  fail-closed borrow-origin discipline. Two independent real-world
  instances of this idiom warrant a dedicated follow-up issue.

### Cause slots and next-milestone inputs

Aggregate obligations (five Gate A+B pilots) by cause slot, B0 → E2:
unknown external call 12,945 → 11,680 (−1,265: the #58 library working
as designed); unsupported alias/storage 3,379 → 3,417 (+38:
contract-forced tracking and kind upgrades); pointer/integer provenance
415 flat; aggregate transport 51 flat; all other slots ≤ 38.

Top remaining unmodeled callees at E2: `indirect` (function-pointer
dispatch, 836 rows — including the allocator fn-ptr wrappers
`event_mm_*`, the same family that cost hiredis three CLEAR functions
in #64), `__errno_location` (112), project-internal cross-TU callees
(`git__calloc` 72, `git_error_set` 67, `Curl_trc_cf_infof` 106, …), and
a concrete bundle gap: `strcmp` is missing from
`contracts/bundles/libc-string.yaml` (50 rows across libgit2/curl/
libevent; `strncmp` is contracted). Recommended next-milestone ranking:
allocator function-pointer dispatch first, then project-internal
cross-TU summaries, then the `strcmp` bundle gap and the cursor-advance
borrow idiom.

Confirmed false PASS count across every E2/E2+ run: **0**.

## Evidence-backed C&1/v2 candidates

The highest-value follow-up areas are:

1. reviewed external-call ownership contracts with explicit effects;
2. cross-TU boundary accounting and diagnostics;
3. allocator/reallocator boundary modeling;
4. callback retention and reference-count lifecycle modeling;
5. aggregate transport and alias/storage diagnostics.

These are evidence-backed coverage candidates, not implementations in this
pilot and not a C&1/v2 claim.

## Conclusion

This baseline establishes that C&1/v1 is useful on a narrow subset, but that
current real-project coverage is dominated by conservative INCOMPLETE results
at external, allocator, callback, aggregate, and storage boundaries. The next
action is a controlled Hiredis integration/annotation pilot with the
zero-annotation result preserved as its control. C&2 has not started.
