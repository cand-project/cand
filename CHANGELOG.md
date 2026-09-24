# Changelog

All notable project changes are recorded here.

## Unreleased

### Milestone #73: borrow-idiom modeling — address-of-parameter borrows, integer-delta cursor advances, verified-origin borrow returns (ADR-0031)

- Closes the three fail-closed borrow-idiom families measured in #36
  (E2) that fired on correct C. A TU-level re-verification first
  corrected the issue's framing: all five `CAND-B003` findings
  (hiredis `read.c:168`, libevent `buffer.c:1541/1542/1544`, redis
  `zmalloc.c:541/568`) are one mechanism — a local holding a shared
  borrow from a `BorrowFromArg` summary returned while the enclosing
  function's own summary return effect is `Unknown`; none is
  `borrow-unknown-parent`.
- Area A (ADR-0031 §1): `&pointer-free-param` at a reviewed
  borrow-effect argument is ownership-neutral (a scalar read or
  read-or-write cannot fabricate, duplicate, or clobber a tracked
  pointer) and no longer emits `unmodelled-pointer-parameter`. Fixes
  libevent `evutil.c:3211`, hiredis `net.c:248`, sqlite amalgam
  `sqlite3.c:85990` plus 23 same-kind collateral rows in the amalgam.
- Area C (ADR-0031 §2): `PointerRelation::Interior` — pure-integer-delta
  cursor advances (`p += e`, `p++`, `q = p ± e`) keep the parent object
  instead of poisoning the storage; pointer-mentioning deltas still
  poison. Mandatory companion: the exact-base-required destruction
  predicate emits the new `destroy-of-non-base` obligation in all four
  destruction paths (free/destroy/transfer/move), so
  `p += 1; free(p)` stays INCOMPLETE
  (`tests/storage/{compound,unary}_pointer_advance_incomplete.c`).
- Area R (ADR-0031 §3): summary-side, monotone-join origin dataflow
  resolves a function's return to `BorrowFromArg(j)` when every return
  site's value resolves through machine-verified provenance (verified
  summary chains, parameter-derived shapes, integer-delta advances) to
  a single parameter. Resolution-with-detection, not suppression: the
  caller keeps the lifetime link and use-after-free through the
  returned borrow still FAILs. Explicitly annotated borrows still
  require `CAND_RETURNS_BORROW_FROM`
  (`tests/p2/undeclared_borrow_return.c` unchanged FAIL).
- Measured effect (S6 re-measurement, all 8 pilots × {b0, e2, e2plus},
  old vs new binary under identical bundles; full accounting in
  `docs/CAND1-73-FINAL-REPORT.md`): the four correct-C findings removed
  (TU fail→incomplete; `seekNewline`/`find_eol_char` CLEAR), both
  baseline CLEAR-loss guard violations restored (libevent
  `evutil.c:3211`, sqlite amalgam `sqlite3.c:85990`), 4,538 obligations
  removed vs 462 added (all fail-closed, all inside already-blocked
  functions; net down in every pilot×config cell), 201 CLEARs gained /
  0 lost, 0 false PASS (the one TU→pass, zlib `adler32.c`, is a
  pure-read cursor loop with no destruction). One new fail-closed
  finding on correct code — redis `listpack.c:916`, a genuine
  two-parameter origin join that no summary vocabulary can decide — is
  recorded as residual debt with issue #76, alongside the unchanged
  zmalloc 541/568 (realloc-family chain) and the declaration-form
  advance. The new `destroy-of-non-base` kind's first real-world
  firing is sqlite `sqlite3MemFree`'s real interior free
  (`p--; SQLITE_FREE(p)`) — correct fail-closed detection.
- Permanent paired regressions in `tests/interprocedural/` (18
  fixtures, ADR-0027 convention): unlocks
  `addr_param_scalar_borrow_safe.c`, `cursor_integer_advance_use_safe.c`,
  `borrowed_local_return_safe.c`, `seeknewline_shape_safe.c`,
  `find_eol_char_shape_safe.c`, `borrowed_local_chain_b0_safe.c`;
  fail-closed controls `addr_param_pointer_pass.c`,
  `addr_param_tracked_member_pass.c`,
  `cursor_variable_delta_rebind_incomplete.c`,
  `cursor_two_step_delta_rebind_incomplete.c`,
  `cursor_advance_roundtrip_incomplete.c`,
  `destroy_of_interior_incomplete.c`, `move_of_interior_incomplete.c`,
  `borrowed_local_two_params_incomplete.c`,
  `borrowed_local_loop_carried_incomplete.c`,
  `borrowed_local_two_param_null_incomplete.c`; detection controls
  `cursor_integer_advance_uaf_detect.c` (FAIL, `CAND-B002`),
  `borrowed_local_return_uaf_detect.c` (FAIL, `CAND-B001`+`B002`).
  All 18 verified fail-first against pre-change main.
- The #62/#64/#61 incident corpora re-ran byte-identical after every
  stage (subscript, comma, callargs, conditional/condptr, controls,
  reassigned_*, conditional_join_*, undeclared_borrow_return,
  direct_global_escape, contracts trio); two in-development
  regressions — including an exact #62 false-PASS shape — were caught
  by these gates and fixed before commit.
- Qualification: `scripts/check.sh`, CTest 20/20 (incl. fast fuzz and
  policy attacks), CVE replay 2/2 within recorded classifications,
  and the full eight-pilot × {b0, e2, e2plus} old-vs-new re-measurement
  (see `docs/CAND1-73-FINAL-REPORT.md`): zero false PASS, zero lost
  CLEAR, every diff adjudicated. Residual fail-closed debt is filed as
  issue #76 (multi-param origin joins, realloc-family chains).
- Docs: ADR-0031 (+ index entries for ADR-0030 and ADR-0031, closing
  the ADR-0030 index drift), SPEC-0005 §4/§6/§7 and SPEC-0010 §4
  rule 10 amendments, PASS-path-audit amendment (§1), KIND_TO_SLOT
  entry for `destroy-of-non-base`.

### Contracts: string/memory bundle gap closure (#74, measured in #36)

- `strcmp` was missing from `contracts/bundles/libc-string.yaml`
  (it is in neither the symbol list nor the deliberate-exclusion list
  of `contracts/evidence/libc-string.md`) — a genuine omission, unlike
  the reviewed exclusions. Added with the strncmp claim class
  (ISO C11 7.24.4.2; params 0,1 borrow; scalar return).
- Same-pass family audit of the measured E2 long tail added the other
  symbols that fit an existing reviewed claim class exactly:
  `strrchr`/`strstr`/`strpbrk` (interior borrowed return from param 0,
  the strchr class), `strspn`/`strcspn` (scalar-return borrows),
  `strlcpy` (POSIX.1-2024, bounded write within the call, the strncpy
  class, scalar return) in libc-string 1.1.0, and `memrchr` (the
  memchr class) in libc-memory 1.1.0.
- `strsep` and `strtok_r` joined the `check_bundles.py` exclusion
  table (pointer-to-pointer state output), and `strtoll` was added to
  the existing strtol/strtoul entry; the excluded-symbols fixture now
  pins `strtok_r` as uncontracted.
- New conformance fixtures: `strsearch_borrowed_return_safe.c`
  (positive: interior returns used alive, plus strlcpy) and
  `strsearch_borrowed_return_uaf.c` (adversarial: use-after-free
  through every new interior-return symbol must still FAIL);
  `libc_conformance_safe.c` covers the scalar-return additions;
  every positive fixture stays INCOMPLETE without contracts.
- Measured effect (libgit2 `checkout.c`, old vs new merged bundle):
  14 obligations removed (8 `:strcmp` rows plus 6 adjacent
  summary-branch rows from the same call sites), 0 added, findings
  unchanged — consistent with the 104 `strcmp` rows measured across
  the eight #36 pilots at E2. `strerror` stays excluded (151 measured
  rows; needs a reviewed static-storage return class, recorded as
  demand in the evidence file).

### Milestone #36: E2 post-adoption re-measurement of the real-world pilot matrix

- `scripts/pilots/realworld_e2.py`: committed, reproducible
  B0/E2/E2+ runner for the nine real-world pilots (extends the
  `external_boundary_experiment.py` method). Per-TU verdicts use an
  agent-equivalent acceptance predicate on non-agent cand1 JSON runs;
  the function universe comes from Clang AST main-file ranges keyed
  per (file, function), with macro-expansion-location definitions
  excluded and macro-invocation-line obligations counted as unmapped;
  obligations map through the 12-slot #36 cause taxonomy; a
  function-level regression guard fails the run on lost CLEAR or on
  obligations/findings added inside baseline-CLEAR functions.
- `docs/pilots/CAND1-REAL-WORLD-BASELINE.md` gains the post-adoption
  re-measurement section: the B0/E2/E2+ matrix for the eight measured
  pilots (jemalloc remains the blocked non-claim), fully attributed
  deltas against the original baseline, adjudicated guard events, the
  SQLite canonical-vs-amalgamation gap measurement, and the
  next-milestone Pareto ranking (allocator function-pointer dispatch,
  project-internal cross-TU summaries, the `strcmp` bundle gap,
  `__builtin_expect`/`__errno_location`, cursor-advance and
  pointer-output borrow idioms). False PASS count across all 24 corpus
  runs: 0.

### Milestone #41: bounded pointer-output `produces_out_owner` contracts (ADR-0030)

- `produces_out_owner` contracts gain a mandatory `output:` block
  (`write: always|on_success`, `success: zero|nonzero` required iff
  `write: on_success`, `nullable`), validated by
  `contracts/schema/cand-api-contract.schema.json`. At most one produces
  parameter per symbol; `T ***`, function-pointer, and array out-slot
  shapes, legacy-indent `nullable` keys, and `success` with
  `write: always` fail closed at load; symbols without a visible
  translation-unit declaration or with a visible same-unit body are
  dropped fail-closed.
- The rule set ships as the draft measurement profile
  `cand1/v1.1-draft` (transport
  `cand1-pointer-transport-v2`): enabled by the non-authoritative
  `--pointer-output-contracts` modifier (cand1 only; exit 2 usage error
  elsewhere, emitted before any policy load) or by the
  `features.pointer_output_contracts` policy feature (the sole agent-mode
  authority; changes against the reviewed base are `REVIEW_REQUIRED`;
  modifier/policy disagreement is `fail-policy`, never a silent rule-set
  change). No feature-enabled run can emit a C&1 PASS.
- Accepted produces are bounded by the call-site acceptance predicate
  (direct non-variadic callee; `&local` destination whose address is not
  otherwise taken; syntactic loop refusal; `dest = f(&dest)` refusal;
  destination pre-state `Null`/`Moved`/`MaybeMoved` for `write: always`
  and exactly `Null` for `write: on_success`). Refused calls fall through
  to the ordinary unknown-call path, so their rows are byte-identical to
  v1. Accepted produces bind through the allocation-site machinery:
  `write: always` + non-nullable is immediately usable; every other form
  marks the binding, and uses before a recognized single-form guard
  (C1 call guard, C2 embedded assignment, C3 stored-then-tested, C4
  destination guard) emit the fail-closed `unrefined-out-owner-use`
  obligation instead of a verdict.
- Short-circuit chains (`if (A || B)`): the defensive terminator pass no
  longer pre-applies a later block's element call (spurious live-
  destination refusal observed in the libgit2 pilot); element calls are
  pre-marked processed for the defensive pass
  (`tests/interprocedural/pointer_output_short_circuit.c`). With the
  feature off the analyzer is unchanged.
- Fuzzing: eight `POINTER_OUTPUT_*` mutation operators run in a
  `PointerOutputWorkspace(StrictWorkspace)` with the feature on, a pinned
  reviewed contract bundle, and the CLI modifier never passed; PO verdicts
  are SAFE → incomplete (never PASS), KNOWN_VIOLATION → fail
  (ASan-confirmed), UNSUPPORTED → incomplete with the required row.
- Evidence: `cand-evidence.schema.json` and `cand-check.schema.json`
  accept `cand1-pointer-transport-v2` in `transport_rule_set`; feature-run
  evidence validates and replays.
- Qualification (evidence: `docs/pilots/POINTER-OUTPUT-PARETO.md`):
  - v2 pilot measurement with a reviewed 8-symbol bundle: 75 of 101
    addressable bundle rows converted; every refused row attributed
    (loop scope, uninitialized/live destination, unrecognized guard
    forms, and review refusals: cursor advancers, second inexpressible
    out-params, backend-dispatch writes); findings byte-identical in all
    five pilots;
  - five-pilot v1 zero-diff invariant re-verified (all pilots
    byte-identical to the pre-#41 baselines);
  - full fixture battery, agent-path policy matrix, CVE replay, mutation
    corpus (ASan-confirmed), fast fuzz, and extended fuzz (10k, seed
    12345) green.

### Milestone #39: declaration-annotation propagation through a reviewed manifest (ADR-0029)

- Body-less declarations carrying C& ownership annotations now seed external
  summaries **only** when a separately reviewed annotation-review manifest
  (`cand.annotation-review/v1`, new `--annotation-review` flag) lists the
  symbol with exactly the same facts. The manifest shares the contract
  parser, is policy-pinned through the existing trusted pin list in the
  agent path (unpinned/candidate/substituted manifests fail the policy and
  never reach the analyzer), appears as `annotation_reviews` evidence
  entries, and participates in the evidence freshness check
  (`annotation-review-set-substitution`, post-attestation mutation reports
  `stale`).
- Candidate-only annotations (no manifest, absent symbol, fact mismatch,
  conflicting redeclaration facts, out-of-range borrow index) never seed:
  their calls keep today's verdicts with distinct fail-closed kinds
  (`unreviewed-declaration-annotation:<sym>`,
  `conflicting-declaration-annotation:<sym>`). The recorded candidate-only
  attack control remains INCOMPLETE.
- Seeded summaries carry `SummaryOrigin::AnnotationTrusted`; seeding runs
  before contract loading, an agreeing contract keeps its provenance, and a
  disagreeing one fails closed with an `annotation/contract mismatch`
  `contract-body-conflict`. Visible bodies always win; seeding is refused
  for C++ TUs, K&R declarations, `realloc`, and pointer-to-pointer shapes
  (#41 scope).
- Qualification (evidence: `docs/pilots/DECL-ANNOTATION-PROPAGATION.md`):
  - Gate A census: 21,536 XTU-eligible rows / 2,045 symbols across the five
    pilots (sqlite 77% addressable);
  - fixture H-matrix: the 01/03/05 defect (PASS under contracts, INCOMPLETE
    under annotations) is repaired; manifest-less control unchanged;
  - five-pilot zero-annotation invariant: obligations and findings
    byte-identical;
  - full-project hiredis replay: H2 (annotations + manifest) tracks H1
    (contracts) within 8 of ~1,050 obligations, every difference attributed
    to body precedence and None/Unknown silence semantics;
  - nine-mutation corpus: 9/9 ASan-confirmed temporal defects now FAIL
    under H2 (previously INCOMPLETE), 0 false PASS; ABI/layout control
    unchanged;
  - fuzz: seven new `EXTERN_*` mutation operators against a fixed
    manifest-pinned strict workspace (fast campaign 1,000 cases: 0 false
    pass, 48/48 operators correct).
- Spec amendments: SPEC-0010 rule 11 (trusted external effects include
  reviewed annotation manifests), SPEC-0003 §17 (manifest format),
  `SAFETY_CLAIMS.md` (trusted-input list: accepted annotation-review
  manifests; LLM-generated manifests are not trusted merely because they
  exist). The v0.2.2 claim identity is unchanged.

## Unreleased

### Milestone #54: alias/storage evidence bar + local-alias destruction false FAIL repaired

- Gate A evidence (`docs/pilots/ALIAS-STORAGE-PARETO.md`, census harness
  `scripts/pilots/alias_storage_pareto.py`): issue #54's evidence bar for
  alias/storage adoption precision is NOT met — alias/storage is the #1
  sole-blocker family only in zlib (18 functions); the callee-summary
  cascade (milestone #61 residual + cross-TU) dominates the four large
  pilots, and neither CVE replay defect path is alias/storage-blocked.
  Adoption-precision work stays deferred; zlib's alias dominance is
  recorded as the leading indicator to re-run.
- the separately tracked local-alias destruction false FAIL (verifier
  bug, fail-closed direction) is repaired (ADR-0028): a consuming call
  (`free` or a Destroy/TakeOwnership callee) through a local that
  unambiguously holds one parameter's entry value — single-assignment
  declaration-init alias, never reassigned, address never taken,
  parameter never reassigned — now attributes its effect to that
  parameter, exactly as the direct `free(p)` form; measured 23 of 46
  pilot findings (50%) were CAND-O006 false FAILs of this class;
- measured five-pilot before/after: curl findings 42 → 23 (all 19
  removed findings adjudicated as the false FAIL; zero new findings;
  obligations byte-identical in every pilot), curl clear 345 → 347;
  four residual false FAILs remain by design (2 curl loop-reassigned
  shapes, 2 zlib assignment-established shapes) and are recorded as
  residual debt;
- 17 paired regression fixtures (`tests/interprocedural/parameter_alias_*.c`)
  pin every whitelist boundary, the fail-closed complement, and the
  caller-side destroy semantics; 6 new differential fuzz mutation
  operators (`PARAM_ALIAS_*`) cover the parameter-alias surface.

## 0.2.2 — 2026-09-23

### Post-incident release: incidents #62 and #64 repaired, C&1/v1 claim restored

- complete exact-head requalification of the release source (see
  `docs/CAND1-V0.2.2-RELEASE-EVIDENCE.md`); the `v0.2.1` tag's C&1/v1
  claim is suspended (not rewritten) and superseded by this release.

### Milestone #61: conditional borrow/none effect join (bounded same-TU summary precision)

- Gate A evidence (`docs/pilots/SAME-TU-SUMMARY-PRECISION-PARETO.md`,
  census harness `scripts/pilots/same_tu_precision.py`): on the
  post-#62/post-#64 baseline, conditional borrow/no-effect calls (H1) are
  the dominant addressable root cause of same-TU summary imprecision
  (54/50/357/669/424 undecided functions across the five pilots);
  same-origin multi-return, decided-wrapper composition and member returns
  are already sound, and local-return provenance has a near-zero
  population post-#64;
- fix (ADR-0027): a parameter passed to a borrow-effect or no-effect
  callee only under a condition keeps that param effect instead of
  collapsing the summary to Unknown — consistent with the existing
  treatment of conditional direct member/deref/subscript borrows;
  conditional consume/destroy and unresolved-callee effects still fail
  closed;
- measured five-pilot BLOCKED→CLEAR: +62 functions (zlib +3, curl +9,
  libgit2 +39, sqlite +11; hiredis +0 with 18 obligations removed),
  findings preserved exactly in every pilot, zero lost-clear, 431
  additional decided summaries;
- permanent paired regressions in `tests/interprocedural/`:
  `conditional_join_borrow_safe.c`, `conditional_join_none_safe.c`,
  `conditional_join_destroy_incomplete.c`,
  `conditional_join_uncond_destroy_safe.c`,
  `conditional_join_uaf_detect.c`,
  `conditional_join_branch_conflict_incomplete.c`.

### Incident #64: compound return expressions misattribute borrow origin (BLOCKER)

- confirmed false PASS inside the published C&1/v1 claim, pre-existing in
  v0.2.0 and v0.2.1 (introduced with the borrow-summary machinery in
  `bfc68a1`, the same commit as incident #62):
  `SummaryBuilder::borrowedParameterIndex` resolved a returned pointer's
  borrow origin from the **first parameter contained anywhere in the
  return expression**, and this fallback ran before the callee-mapping
  branch, so `return c ? a : b;` (distinct branch origins), `return
  (first(a), b);` (comma), value reads out of parameter storage
  (`return p->f;` with pointer member, `return p[i];` on `T**`), and
  calls with parameter arguments (`return dupit(p);` claiming a borrow of
  `p` while `dupit` returns a fresh allocation) all produced wrong or
  invented `borrow_from_arg` facts; callers that destroyed the true origin
  and used the returned pointer received PASS with zero findings on
  ASan-confirmed use-after-frees (see issue #64 for the four confirmed
  false-PASS shapes, the fail-closed family members, and the claim
  suspension);
- fix (fail-closed, no semantic-scope change): origins resolve only from
  expressions unambiguously derived from a single pointer parameter
  (direct references, interior member chains and array-member decay,
  `&p->f`/`&p[i]`, `p + n`, same-parameter conditionals, comma on the last
  operand); everything else — distinct-origin conditionals, value reads,
  non-parameter pointers, and all `CallExpr`s — fails closed to `Unknown`,
  restoring the callee-mapping branch as the only path for call-shaped
  returns (ADR-0026);
- permanent paired regressions in `tests/interprocedural/`:
  `compound_origin_conditional_incomplete.c`,
  `compound_origin_condptr_incomplete.c`,
  `compound_origin_comma_uaf.c`,
  `compound_origin_subscript_incomplete.c`,
  `compound_origin_callargs_incomplete.c`, and the controls
  `compound_origin_controls_safe.c` (accepted origin forms must stay
  decided) and `compound_origin_controls_detect.c` (origin destruction
  must stay detected);
- five-pilot before/after: all summary movement Unknown-ward or
  conflict-resolving; eight `CAND-B003` findings removed whose premises
  were the misattributed summaries themselves (defect artifacts); seven
  functions newly clear in libgit2/sqlite whose pre-fix blockers were
  cascade artifacts of conflict collapses triggered by the wrong borrow
  facts, with the fail-closed holding verified at the unknown returns'
  consumption sites; interior array-member returns that the old fallback
  failed to resolve (flexible array `ref->name`, `checksum[]`) now resolve
  soundly.

### Incident #62: borrow-origin misattribution through parameter reassignment (BLOCKER)

- confirmed false PASS inside the published C&1/v1 claim, pre-existing in
  v0.2.0 and v0.2.1 (`src/cand.cpp` identical between the v0.2.1 tag and
  the fix's parent main): `SummaryBuilder` resolved a returned pointer's
  borrow origin (and callee borrow-origin mappings) to the syntactically
  referenced parameter name without recognizing pointer-parameter
  reassignment, so `p = r; return p;` was summarized `borrow_from_arg@0`
  while the returned pointer aliases arg 1; a caller that destroyed the
  true origin and used the returned pointer received PASS with zero
  findings on an ASan-confirmed use-after-free (see issue #62 for the
  reproducer, the two confirmed false-PASS shapes, the fail-closed
  controls, and the claim suspension);
- fix (fail-closed, no semantic-scope change): when the parameter
  resolved as a return borrow origin — directly or through the
  return-call argument mapping — is the target of any assignment in the
  body, the return effect becomes `Unknown`;
- permanent paired regressions in `tests/interprocedural/`:
  `reassigned_borrow_origin_incomplete.c` and
  `reassigned_borrow_origin_helper_incomplete.c` (must never PASS) plus
  the correct-attribution control
  `reassigned_origin_direct_return_uaf.c` (must stay a detection);
- five-pilot before/after (obligation-diff method): findings preserved
  except two `CAND-B003` borrow-escape findings in curl `splay.c` that
  were derived from `splay()`'s misattributed summary (the returned
  pointer is a different tree node, not the parameter's pointee) — the
  premise of those findings was the incident defect itself; every
  obligation change is Unknown-ward at the creation sites of pointers
  that became untracked, matching the standard unknown-return semantics;
- v0.2.1's C&1/v1 claim is suspended/superseded pending the
  post-incident release and complete exact-head requalification
  (`docs/SAFETY_CLAIMS.md`).

### Reviewed external API boundary library (milestone #58)

- six reviewed contract bundles under `contracts/bundles/` (22 symbols:
  libc memory/string/ctype/stdio, POSIX io/socket) supersede
  `contracts/libc-borrow.yaml` (deleted; its parameter lists omitted
  by-value scalar positions, which fired spurious fail-closed escape
  obligations on argument expressions like `sizeof(*p)`);
- `contracts/libc.yaml` audited for parameter completeness (allocator
  scalar positions listed; `realloc` size parameter now
  `no_ownership_effect`);
- `scripts/contracts/merge_contracts.py`: deterministic single-file merge
  for `cand check --contracts` (duplicate symbols rejected, no last-wins;
  SHA-256 digests recorded; merged file is a build product, never
  committed);
- `scripts/contracts/check_bundles.py`: static bundle policy gate
  (parameter completeness against an authoritative signature table,
  scalar-only `no_ownership_effect`, excluded-symbol blacklist,
  provenance presence);
- `contracts/evidence/*.md`: per-symbol provenance records;
  `docs/contracts/EXTERNAL-API-TRUST-MODEL.md`: normative trust model;
- `tests/contracts/` conformance and adversarial harness (new CTest
  `cand-p0-5-contracts`); CVE-replay and interprocedural harnesses moved
  to the merge-tool bundle flow; `scripts/check.sh` gates bundle policy
  and merge determinism;
- measured adoption (obligation-diff method, zero analyzer semantic
  changes): hiredis 18→23 CLEAR (+5), curl 119→127 (+8), libgit2
  223→226 (+3), zlib 49→49; total +16 CLEAR across 2,017 functions,
  75 obligations removed, 0 added, findings unchanged, false PASS = 0;
  CVE replay re-verified BOUNDED-INCOMPLETE under the new bundles;
- corrected the earlier external-boundary attribution (sole-EXT is 7
  Hiredis functions, not 23; function universe 181, not 179) and the
  intermediate "+17 CLEAR" measurement artifact; see
  `docs/pilots/EXTERNAL-API-BOUNDARY-PARETO.md` and
  `docs/pilots/EXTERNAL-CONTRACT-ADOPTION-RESULTS.md`.

## 0.2.1 — 2026-09-22

Second post-incident qualified release. The public C&1/v1 scope is unchanged;
this is a soundness patch release: it repairs the variadic-argument false PASS
confirmed on v0.2.0 (incident #53, which suspends the v0.2.0 claim) and adds
the C&1 proof-program infrastructure. No new semantic claim is introduced.

### Soundness repair (incident #53)

- repaired the direct-call summary path so tracked pointers passed at
  argument positions beyond a callee's modelled parameter list (variadic
  slots) emit the same escape/borrow-retention obligations as the unknown-call
  path — v0.2.0 could emit authoritative PASS with zero obligations for a
  destroyed or escaping tracked pointer at such positions;
- permanent regression: `tests/interprocedural/variadic_argument_escape.c`
  (no case may PASS);
- the `v0.2.0` tag is immutable and not rewritten; its C&1/v1 claim is
  suspended and superseded by this release.

### Parameter-identity completion (ADR-0024)

- summary-`Unknown`/`None` pointer parameters are tracked as live-at-entry
  objects with no authority;
- conditional/loop parameter destruction followed by use now joins to
  `MaybeDead` and FAILs with certainty `possible` (reviewed strengthening,
  previously fail-closed INCOMPLETE);
- reviewed libc borrow bundle `contracts/libc-borrow.yaml` (strncpy
  borrowed-return declaration);
- red-team matrices: `parameter_unknown_capability_redteam.c`,
  `variadic_argument_escape.c`, `libc_borrow_bundle_safe.c`.

### Proof program (Phase A/B/C start)

- `docs/CAND1-PROOF-PLAN.md`: C1–C6 proof obligations and phased acceptance
  gates;
- Phase A: exhaustive PASS-path audit
  (`docs/CAND1-PASS-PATH-AUDIT.md`) with `scripts/pass-path-guard.sh` wired
  into `scripts/check.sh`;
- Phase B: nightly extended fuzzing with three date-derived rotating seeds
  (~3×10⁴ fresh cases per scheduled run toward the ≥10⁶ cumulative target);
  mutation-operator suite expanded from 14 to 35 operators (including the
  historical soundness-incident mechanisms as generator mechanisms:
  variadic-argument escapes from incident #53 and the parameter-lifetime
  classes from incident #46); machine-readable cumulative campaign
  accounting — every report embeds provenance (verifier SHA-256, generator
  SHA-256, source commit) and `tests/fuzz/accumulate.py` aggregates reports
  with a defined unique campaign case `(generator_sha256, seed, case index)`
  so duplicate seeds cannot inflate the cumulative total;
- Phase C: CVE replay suite `tests/cve-replay/` (registry, engine, weekly +
  on-demand CI job) with ASan ground-truth validation at both the vulnerable
  and fix revisions, per-entry build/compile commands (registry schema v2,
  supporting CMake-based upstream projects), and classifications
  DETECTED / BOUNDED-INCOMPLETE / MISSED with MISSED treated as a loud
  soundness incident; the engine merges the two reviewed contract bundles
  into a single per-run file (`cand check --contracts` accepts exactly one
  file; a repeated flag is last-wins — both entries were re-validated under
  the merged-bundle configuration with identical classifications and
  obligation counts); validated entries: CVE-2026-87933 (cJSON `merge_patch`
  heap-use-after-free) and CVE-2026-50219 (libexpat handler-reentry
  use-after-free, fix released in 2.8.2), both BOUNDED-INCOMPLETE with
  defect-path obligations and no false PASS;
- refreshed Hiredis adoption-blocker Pareto at function level
  (`docs/pilots/CAND1-ADOPTION-BLOCKER-PARETO.md`): same-project cross-TU
  effects are the largest sole-blocker family (26 functions), external-API
  effects touch the most functions (101), alias/storage precision ranks
  third (6 solely blocked).

### CI repair

- pinned `CMAKE_MAKE_PROGRAM=/usr/bin/ninja` in the host-building workflows
  (nightly extended fuzzing had been failing since 2026-09-20 because the
  GitHub runner image now ships a shadowing `/usr/local/bin/ninja`).

## 0.2.0 — 2026-09-21

First post-incident fully requalified C&1/v1 release. The C&1/v1 scope is
unchanged; this release restores sound implementation of the existing
SPEC-0010 qualified scope.

### Soundness repair

- fixed callee-side parameter lifetime tracking;
- added deterministic symbolic parameter object identity;
- separated object lifetime from parameter capability;
- defined distinct `TakeOwnership`, `Destroy`, and `Borrow` entry semantics;
- made proven aliases observe a common parameter-object lifetime;
- detected double destruction and post-destruction parameter access;
- preserved `Dead`/`MaybeDead` state at CFG joins;
- removed the direct-parameter summary fast-path bypass around lifetime state.

### Incident

- resolved [#46](https://github.com/cand-project/cand/issues/46);
- kept the immutable v0.1.0 tag unchanged;
- retained the affected historical claim as suspended/revoked;
- permanently preserved the original false-PASS reproducer and incident record.

### Qualification

- CTest: 19/19 PASS;
- deterministic fuzz: 20,000 cases across seeds 12345 and 67890;
- confirmed false PASS: zero;
- parameter-lifetime incident matrix: repaired, with CASE_F/G/H/I now FAIL;
- Clang 18.1.3 ASan/UBSan differential: no false PASS;
- policy, evidence, contract, and toolchain attacks: PASS;
- exact-head independent review and protected CI: PASS;
- reproducibility SHA256: `06aa9120a5cd0d33b15ebc0e6cfc465b73a60113ba086aca1a7437406c89cc0a`.

### Scope and non-claims

C&1/v1 scope is unchanged. This release adds no C&2, general memory-safety
claim, cross-TU ownership guarantee, callback-retention guarantee, or `realloc`
guarantee. v0.1.0 is historical and must not be cited as current C&1 evidence.

### Semantic implementation archive

The subsections below archive the semantic implementation phases shipped in
the 0.1.0 → 0.2.0 lineage; they were previously mis-filed under a stale
`Unreleased` heading. The ADR-0024 parameter-identity completion that landed
after v0.2.0 is summarized under 0.2.1 above and specified in full in
`docs/adr/ADR-0024-parameter-identity-completion.md`.

### P1 — Explicit unique ownership and move semantics

- added the normative P1 unique-owner state model and analysis-only
  `CAND_MOVE` transitions while preserving ordinary C ABI/runtime behavior;
- added explicit `CAND_OWN`/`CAND_TAKES`/`CAND_RETURNS_OWN`/`CAND_DESTROYS`
  integration with body summaries and trusted contracts;
- added deterministic `CAND-O001`–`CAND-O005` ownership diagnostics, CFG
  `MaybeMoved` joins, transition coverage, and GCC/Clang compatibility tests;
- kept aggregate ownership copies, retention, callbacks, `realloc`, spatial
  safety, and the C&1 claim outside this phase.

### P0.5 — Agent verification authority and reproducible evidence

- added strict `cand check --agent` mode with generated policy, trusted-base
  input, checked-scope and compiler-argument pins, and separate semantic/policy
  outcomes;
- added deterministic policy-diff classifications and trusted-contract
  digest/trust-class checks that prevent candidate self-promotion;
- added `cand.evidence/v1`, binding source, policy, contracts, frontend, base,
  and verifier identity, with stale-input checks and exact analysis replay;
- added adversarial checks for proof weakening, scope/compiler substitutions,
  fake trust, source/policy/contract/binary mutation, and recomputed evidence
  forgery; no general C memory-safety or C&1 claim is introduced.

### P0.4 — Interprocedural ownership summaries and trusted contracts

- added conservative same-translation-unit summaries for allocator, destructor,
  borrowed-return, read-only parameter, and supported ownership-transfer
  wrappers;
- added explicit trusted SPEC-0003 v1 YAML loading with fail-closed malformed
  contracts and body/contract conflict handling;
- added conservative recursion/cycle regressions and a 472-line natural packet
  router comparison (116 P0.3 unsupported operations versus 53 in P0.4).

### P0.2 — CFG-based flow-sensitive ownership

- added ADR-0011: ownership state is attached to CFG program points and propagated with a worklist fixed point over upstream `clang::CFG` (no fork, no new parser);
- introduced a five-state lattice (`Untracked`, `Owned`, `Dead`, `MaybeDead`, `Unknown`) with documented, deterministic join rules; `Unknown` is never downgraded to `Owned`;
- `if`/`else`, nested branches, early returns, multiple returns, cleanup `goto`/labels, `switch`, `break`, `continue`, `?:`, `&&`/`||` and simple loops are now analyzed instead of being rejected as unsupported;
- path-dependent use-after-destroy and double destruction now produce `CAND-T002`/`CAND-T003` with `certainty` (`definite`/`possible`) and flow evidence (`state_before_access`, `state_trace` events including `conditional_destruction`);
- added a `Null` storage state: `p = NULL` is a modeled release, so a later `free(p)` is a defined no-op while a later access is a null-dereference issue outside P0's claim;
- pointer-arithmetic access bases (`*(p + 1)`, `(p + i)[j]`, `(p + 1)->field`) are now checked against the tracked object instead of being silently dropped (independent review found seven ASan-confirmed use-after-free programs that previously returned PASS);
- the operand of `sizeof`/`alignof`/`typeof` is unevaluated and is no longer treated as an access (the idiomatic free-then-`malloc(sizeof *p)` reuse previously produced a spurious definite `CAND-T002`);
- a translation unit that produced any error-level diagnostic (including driver-level option errors that still allow a recovered AST) now returns exit 2 instead of a verdict, so C& never reports PASS/FAIL on code that did not compile;
- diagnostics are emitted in a post-convergence pass, so intermediate worklist iterations cannot leave stale findings or obligations behind;
- fixed-point loop analysis surfaces possible double destruction (`while (cond) { free(p); }` is FAIL, not a single-iteration assumption), while loops that do not change ownership state are provable;
- unreachable CFG blocks are not analyzed and cannot invent obligations;
- `tests/cfg/` corpus added (10 fixtures) with ASan cross-checks, wired into CTest as `cand-p0-2-cfg`;
- two P0.1 fail-closed fixtures improved to modeled results: `conditional_ownership.c` is now PASS and `short_circuit_ownership.c` now FAILs with `CAND-T003` (possible), because those semantics are modeled by the CFG;
- `contracts/schema/cand-check.schema.json`: findings gained `certainty` and `state_before_access`;
- unchanged and still INCOMPLETE: aliases, struct members, array elements, pointee stores, interprocedural ownership, callbacks, `realloc`, inline asm, statement expressions, computed `goto`, unknown pointer-return ownership, unknown calls with tracked pointers.

### P0.1 — Trustworthy PASS / fail-closed heap semantics

- added ADR-0010 defining the PASS-completeness invariant: PASS requires zero unresolved ownership/lifetime operations in checked scope; false INCOMPLETE is acceptable, false PASS is not;
- fixed three ASan-confirmed false negatives (struct member, array element, wrapper-return use-after-free) that previously returned PASS;
- `free()` is now fail-closed: `free(NULL)` is known safe; untracked pointer variables and non-variable expressions produce INCOMPLETE (`free-untracked-pointer`, `free-untracked-expression`) instead of being silently ignored;
- allocation into unmodelled storage (struct member, array element, pointee) produces INCOMPLETE (`allocation-to-untracked-storage`);
- pointer values from unmodelled pointer-returning calls produce INCOMPLETE (`unknown-pointer-return-ownership`) at initializers, assignments, call arguments and dereference sites;
- returning pointers to automatic storage (stack escapes, including through local pointer variables and members of locals) and inline asm / GNU statement expressions produce INCOMPLETE (`stack-pointer-return`, `inline-asm`, `statement-expression`) instead of passing silently;
- aggregate initializers carrying heap allocations or unmodelled pointer values are INCOMPLETE (`allocation-to-untracked-storage:initializer`, `unknown-pointer-return-ownership`); computed `goto` is INCOMPLETE (`indirect-goto`);
- ownership operations in conditionally evaluated positions (`?:` branches, short-circuited `&&`/`||` operands) produce INCOMPLETE (`conditional-expression`, `short-circuit-expression`) and no longer generate spurious `CAND-T003` findings;
- file-scope pointer initializers are classified defensively (ISO C constant initializers are known safe; non-constant initializers are rejected by the frontend with exit 2);
- frontend/input failures now return exit code 2, distinct from FAIL (1);
- coverage summary now reports `tracked_heap_objects` and `unsupported_ownership_operations`;
- added `tests/failclosed/` regression suite and `tests/differential/` ASan oracle suite (wired into CTest) that reject any `ASan violation + cand PASS` pair;
- extended `contracts/schema/cand-check.schema.json` with the new coverage fields and optional unsupported `symbol`.

### LLM-first architecture

- made **“LLMs synthesize. C& verifies.”** a core project thesis rather than an optional integration;
- added ADR-0008 defining coding agents as first-class untrusted synthesis engines;
- added ADR-0009 defining proof-policy protection against agent reward-hacking/shortcutting;
- added SPEC-0004 defining the deterministic machine-agent verification protocol;
- added `contracts/agent-policy.yaml` with a strict generated-code safety budget;
- made machine-readable ownership-state diagnostics, repair classes, policy deltas and evidence artifacts first-class design requirements;
- added generated-code strict mode and autonomous repair-loop gates to the roadmap;
- clarified that LLM-generated annotations are intent, candidate contracts are untrusted, and model-generated safety claims have no proof status;
- shifted the intended human review surface toward unsafe boundaries, trusted contracts, suppressions, unsupported code and semantic policy changes.

## 0.1.0 — 2026-09-14

First public architecture and compatibility baseline for C&.

### Included

- project identity: **C& — C with Ownership**;
- ADR-0001 defining C& as an analysis/enforcement stage, not a compiler fork;
- normative ownership, borrowing, pipeline, and external-contract specifications;
- portable C annotation header under `include/cand/cand.h`;
- machine-readable safety levels, diagnostics, libc ownership contracts, and contract schema;
- GCC and Clang compatibility fixtures;
- CI validation for contracts and ordinary-C compatibility;
- project security, contribution, and governance files;
- corrected canonical logo and project-purpose/build-pipeline diagrams.

### Safety status

Version 0.1.0 is a design and compatibility baseline. It does **not** claim that C&1 temporal ownership safety is implemented or proven sound. Such a claim requires the acceptance and evidence gates defined by the SPECs and roadmap.
