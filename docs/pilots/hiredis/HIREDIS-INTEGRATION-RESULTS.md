# Hiredis C&1/v1 integration results

## Identity and controls

- C& starting main: `74adeaddbe42d3d34c1d0a26f7854871d890b7c8`
- evidence PR #38 reviewed HEAD: `d378f01270db88a535408af478e06edb424d656b`
- evidence PR #38 reviewer: `zoorpha`
- evidence PR #38 merge SHA: `678d64fa59c79670f13baf75409f6e37ca64e568`
- Hiredis: `redis/hiredis@33a12fb23531f33e3455c7ed46008c20c7ad9c78`
- Redis server: `redis/redis@5f08991bce470ab721f10d7f815baad49f3aae60`
- environment: qualified Ubuntu 24.04 / x86_64 profile
- compiler: Clang 18.1.3; C11
- common C& scope: `alloc.c async.c hiredis.c net.c read.c sds.c sockcompat.c`
- project C LOC: 10,587
- representative analyzed LOC: 5,509
- upstream checkout: 25 C translation units, 27 headers
- compile database: deterministic CMake generation with
  `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`; H0-H3 used equivalent explicit
  C11 flags over the fixed seven-file scope
- baseline artifacts: `libhiredis.so.1.5.0`, `hiredis-test`

H0 is the preserved zero-annotation baseline from Issue #36. All four modes
use the same source scope and manual C11 frontend flags. H1/H3 use the
pilot-scoped contract bundle; H2/H3 use the reproducible annotation patch.

## H0 through H3

| Mode | Analyzed LOC | PASS | FAIL | INCOMPLETE | Coverage | Decidable coverage | Annotations | Trusted contracts | Trusted symbols | Files patched | Runtime | Peak RSS | Upstream tests | Sanitizer | False PASS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---:|
| H0 zero metadata | 5,509 | 370 | 0 | 5,139 | 6.72% | 6.72% | 0 | 0 | 0 | 0 | 0.784s | 84,232 KB | 1/1 PASS | 1/1 PASS, clean | 0 |
| H1 contracts only | 5,509 | 370 | 0 | 5,139 | 6.72% | 6.72% | 0 | 1 | 17 | 0 | 0.777s | 84,132 KB | 1/1 PASS | 1/1 PASS, clean | 0 |
| H2 annotations only | 5,509 | 370 | 0 | 5,139 | 6.72% | 6.72% | 17 sites / 30 lines | 0 | 0 | 3 | 0.794s | 84,032 KB | 1/1 PASS | 1/1 PASS, clean | 0 |
| H3 contracts + annotations | 5,509 | 370 | 0 | 5,139 | 6.72% | 6.72% | 17 sites / 30 lines | 1 | 17 | 3 | 0.761s | 84,136 KB | 1/1 PASS | 1/1 PASS, clean | 0 |

Coverage is `PASS / analyzed LOC`; decidable coverage is `(PASS + FAIL) /
analyzed LOC`. The exact H0 numbers remain the authoritative control. The
contract bundle has one document containing 17 trusted symbols; “1” in the
contract column identifies that bundle, not one symbol.

## Trust-cost metrics

- H0 → H1 coverage gain: `0.00 percentage points`; incomplete reduction: `0`.
- H0 → H2 coverage gain: `0.00 percentage points`; incomplete reduction: `0`.
- H0 → H3 coverage gain: `0.00 percentage points`; incomplete reduction: `0`.
- H1 gain per trusted contract: `0.00 percentage points / contract`.
- H2 gain per annotation site: `0.00 percentage points / site`.
- metadata lines per 1,000 analyzed LOC: H1 `18.7`, H2 `5.4`, H3 `24.1`.
- H1 contract file: 103 lines, 17 symbols.
- H2 annotation shim and sites: 30 lines / 17 sites across 3 headers.

The zero gain is an adoption result, not a semantic failure or a justification
for turning INCOMPLETE into PASS. H1 also exposes current contract ergonomics:
visible function bodies can produce `contract-body-conflict` without improving
the per-translation-unit verdict.

## Build and sanitizer control

Unmodified Hiredis was configured and built with:

```sh
cmake -S hiredis -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DBUILD_TESTS=ON
cmake --build build -j2
PATH=/path/to/redis/src:$PATH ctest --test-dir build --output-on-failure
```

The pinned Redis server is `redis/redis@5f08991bce470ab721f10d7f815baad49f3aae60`.
The unmodified control completed CTest `1/1 PASS` in 15.64 seconds with no
ASan/UBSan finding. The annotation build also completed ordinary CTest `1/1`
and sanitizer CTest `1/1` (15.53 seconds). No Hiredis source behavior was
changed by the compile-away metadata.

## ABI and layout control

The unmodified and annotation builds both report:

```text
sizeof(redisReply)=64 sizeof(redisContext)=272 sizeof(redisReader)=224
```

The exported-symbol-list SHA-256 is identical for both builds:
`76047a716fa5cf9a8936dc315695d176f19d94b83b66804de7791dfc9e728aa1`.
The patch changes no struct layout, function ABI, exported symbol, control
flow, or runtime cleanup.

## Remaining INCOMPLETE taxonomy

Diagnostics are nonexclusive and are not treated as one “unsupported” bucket.
The H3 leading diagnostic frequencies were:

| Cause | H3 diagnostic count | Interpretation |
|---|---:|---|
| unmodelled pointer parameter | 685 | pointer ownership/effect is not established |
| ambiguous alias target | 123 | storage/alias target is uncertain |
| unknown call with pointer output | 96 | external call effect is unknown |
| unknown pointer return ownership | 80 | return family/effect is unknown |
| contract-body conflict | 29 | current contract/body interaction blocks the result |
| pointer arithmetic/reassignment | 34 | pointer provenance/storage path is not decidable |
| callback retention | present, chiefly async | deliberately uncontracted |
| realloc/aggregate/varargs/union/custom allocator | present in affected paths | no broad rule was invented |

The mechanism totals are also nonexclusive: pointer storage 812, unknown
return 172, unknown call 126, unknown/other 82, pointer arithmetic 34,
`memcpy` 26, global/static storage 7, and `memmove` 3. The dominant practical
blockers are unknown external ownership effects, cross-TU effects, alias and
storage uncertainty, reallocating operations, and async callback retention.

The current report does not expose a reliable function-level PASS/FAIL/
INCOMPLETE partition. Function counts exist, but assigning verdicts from the
available output would be invented evidence; no verifier change was made.

The follow-up pilot now provides a separate observational mapping in
`HIREDIS-OBLIGATION-DELTA.md`. It maps all 1,217 H0 obligations to 181 Clang
function definition ranges, with no VIOLATION or TOOL-UNKNOWN mapping. CLEAR
and BLOCKED remain observational labels and are not C&1 verdicts.

## Minimal metadata fixtures

The isolated fixtures under `fixtures/` were run through the same C& runner in
H0/H1/H2/H3 modes. The result is deliberately small and exact:

| Fixture | H0 | H1 contracts | H2 annotations | H3 combined |
|---|---|---|---|---|
| owned return + destructor | INCOMPLETE (1) | PASS | INCOMPLETE (1) | PASS |
| borrowed parameter | PASS | PASS | PASS | PASS |
| consumed parameter | INCOMPLETE (1) | PASS | INCOMPLETE (1) | PASS |
| pointer-to-pointer output | INCOMPLETE (1) | INCOMPLETE (1) | INCOMPLETE (1) | INCOMPLETE (1) |
| cross-TU owned return boundary | INCOMPLETE (1) | PASS | INCOMPLETE (1) | PASS |
| matching visible body + contract | PASS | PASS | PASS | PASS |
| conflicting visible body + contract | PASS | INCOMPLETE (1) | INCOMPLETE (1) | INCOMPLETE (1) |
| realloc-like boundary | INCOMPLETE (1) | INCOMPLETE (1) | INCOMPLETE (1) | INCOMPLETE (1) |
| callback retention | INCOMPLETE (3) | INCOMPLETE (3) | INCOMPLETE (3) | INCOMPLETE (3) |

The fixture results show that reviewed contracts can resolve a narrow external
owned-return or consumed-parameter boundary. They also show that current
declaration annotations alone did not resolve the external-only owned-return
fixtures, while a visible annotation/body conflict remains fail-closed. The
fixture contract intentionally does not describe the realloc-like boundary.

## Root-cause decomposition

The leading H3 mechanisms map to these current implementation paths:

- `unmodelled-pointer-parameter`: `checkAccess` emits it when a pointer access
  contains parameter storage but the current summary does not classify the
  parameter as borrow or ownership transfer; `handleReturn` emits it when a
  pointer parameter escapes without a modeled borrowed-return summary.
- `ambiguous-alias-target`: storage binding and assignment paths emit it when
  an alias target is unknown or a pointer is assigned into uncertain storage.
- `unknown-call-with-pointer-output`: `handleCall` emits it when an unknown
  call may write pointer storage. The documented `produces_out_owner` schema
  term is not parsed by the current loader.
- `unknown-pointer-return-ownership`: `bindSummaryReturn` emits it for a
  missing, conflicting, or unknown summary.
- `contract-body-conflict`: `loadContracts` compares a trusted contract with
  a visible body summary and marks disagreement as conflict; `handleCall`
  then emits the fail-closed diagnostic.
- `pointer-arithmetic-reassignment`: pointer arithmetic assignments are
  explicitly unsupported; H3 had 34 such observations.

For the 685 H3 pointer-parameter observations, Clang source-token mapping
directly attributed 77 observations: 30 mutable-borrow-shaped, 9
read-only-borrow-shaped, 3 consumed/destroyed-shaped, 11 pointer-to-pointer,
8 array/buffer-plus-length, and 16 unknown. The remaining 608 observations
were indirect field/derived-pointer uses for which the diagnostic does not
identify a parameter. These are reported as unknown rather than guessed into a
parameter class. Eighteen directly attributed observations had explicit
nullable checks; nullability is an orthogonal property, not a lifetime proof.

H3 unknown pointer-return diagnostics total 199 when symbol-qualified variants
are included: 97 libc/runtime, 12 same-project cross-TU, 10 indirect, and 80
without a usable direct symbol. H3 unknown-call diagnostics total 128; AST
matching identified 17 same-project cross-TU calls to `__redisSetError`, 6
same-TU calls, 6 libc/runtime calls, 12 indirect calls, and 87 calls whose call
target could not be reliably recovered from the diagnostic location. These are
estimates of candidate boundaries, not enabled cross-TU support.

The 29 H3 contract-body conflicts occur at 24 functions. Twenty-one locations
also occur in H2 and are annotation/body reconciliation cases; eight additional
locations are contract/body cases. The diagnostic does not contain enough
semantic detail to label a conflict as “wrong contract” versus “conditional
body not expressible,” so the evidence classification is provisional B/C/D,
with no evidence for blindly overriding the body. The matching and conflicting
fixtures pin the required behavior: matching remains decidable; disagreement
remains INCOMPLETE.

## Follow-up issues and Redis decision

Focused follow-up issues are open:

- #39 declaration-site ownership annotation propagation;
- #40 pointer-parameter and alias/storage decomposition;
- #41 bounded pointer-output ownership contracts;
- #42 same-project cross-TU summary qualification;
- #43 contract/body reconciliation diagnostics and expressiveness.

Redis is **NO-GO for now**. The Hiredis evidence shows zero TU-level coverage
gain, no BLOCKED-to-CLEAR function transitions, and dominant unresolved
pointer/storage, cross-TU, and callback-boundary effects. Hiredis is therefore
the next regression corpus for these targeted improvements; full Redis
annotation or integration work is deferred.

## Mutation corpus and false-PASS control

The disposable corpus contained 9 controlled temporal mutations: reply use
after free, double reply free, reader use after free, double reader free, SDS
use after free, double SDS free, context use after free, double context free,
and command use after free. Each executable mutation produced an independent
Clang 18 ASan/UBSan diagnostic and a nonzero exit. C&1/v1 returned INCOMPLETE
for all 9, with no authoritative PASS. Confirmed in-scope temporal defect plus
C&1 PASS: **0**.

Broken Hiredis sources and mutation files are disposable and are not committed.
Any future confirmed false PASS must follow
`docs/CAND1-FALSE-PASS-RESPONSE.md` and stop adoption work.

## Decision

The Hiredis integration is limited: zero metadata gain was observed from the
small, reviewed boundary bundle and compile-away annotations. The useful next
step is adoption-boundary/contract ergonomics work and a narrowly selected
annotation follow-up, not full Redis and not C&1/v2 implementation. Async
callback retention remains fail-closed. #25 remains separate precision debt.

The bounded contract/body reconciliation follow-up is recorded in
`HIREDIS-CONTRACT-RECONCILIATION.md`. It removes the old omitted-field versus
explicit-no-effect representation ambiguity and adds deterministic conflict
diagnostics, but the exact rerun still has 370 PASS, 0 FAIL, and 5,139
INCOMPLETE at the line-weighted TU metric, with zero observational
`BLOCKED -> CLEAR` transitions. Redis remains NO-GO.
