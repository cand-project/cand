# Changelog

All notable project changes are recorded here.

## Unreleased

### P0.2 — CFG-based flow-sensitive ownership

- added ADR-0011: ownership state is attached to CFG program points and propagated with a worklist fixed point over upstream `clang::CFG` (no fork, no new parser);
- introduced a five-state lattice (`Untracked`, `Owned`, `Dead`, `MaybeDead`, `Unknown`) with documented, deterministic join rules; `Unknown` is never downgraded to `Owned`;
- `if`/`else`, nested branches, early returns, multiple returns, cleanup `goto`/labels, `switch`, `break`, `continue`, `?:`, `&&`/`||` and simple loops are now analyzed instead of being rejected as unsupported;
- path-dependent use-after-destroy and double destruction now produce `CAND-T002`/`CAND-T003` with `certainty` (`definite`/`possible`) and flow evidence (`state_before_access`, `state_trace` events including `conditional_destruction`);
- added a `Null` storage state: `p = NULL` is a modeled release, so a later `free(p)` is a defined no-op while a later access is a null-dereference issue outside P0's claim;
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
