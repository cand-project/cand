# Changelog

All notable project changes are recorded here.

## Unreleased

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
