# C& Implementation Roadmap

C& should be built evidence-first and **LLM-first**. Each phase must prove its contract on real C code before the next safety claim is enabled, and each core capability must work in an automated generate -> verify -> repair loop rather than only as a human-facing analyzer.

The project assumes two first-class users:

1. existing C codebases adopting ownership checks incrementally;
2. coding agents generating new C under strict C& verification from the first commit.

The strategic principle is:

> **LLMs synthesize. C& verifies.**

## P0 — Feasibility, semantic core and machine protocol

Goal: prove the pipeline architecture and the agent-verifier loop without pretending to provide memory safety yet.

Deliver:

- standalone `cand` LibTooling executable;
- compilation database loading;
- C& annotation header;
- versioned contract schema + validator;
- frontend-neutral semantic IR prototype;
- basic allocation/free object identity;
- stable diagnostics JSON format;
- human and SARIF diagnostics;
- deterministic machine result envelope from SPEC-0004;
- repair-class vocabulary;
- positive/negative fixture suite;
- differential ordinary-C/ASan/C& proof harness;
- generated-code strict profile skeleton;
- proof-policy/safety-budget configuration skeleton;
- base-aware policy-diff skeleton;
- evidence artifact skeleton;
- upstream Clang only, no fork.

Exit gate:

- same example source compiles with unmodified Clang and GCC;
- C& can identify malloc/free lifecycle and source locations;
- unsupported constructs are reported explicitly;
- identical inputs produce deterministic machine output;
- a model-neutral script/agent can invoke C&, parse JSON and distinguish `pass`, implementation failure, policy failure and unsupported scope;
- an attempted unapproved proof weakening is distinguishable from a code repair.

### P0.1 — Trustworthy PASS / fail-closed heap semantics (done)

Goal: eliminate false-PASS holes before any broader safety claim. An
independent evaluation found three ASan-confirmed heap use-after-free
programs (struct member, array element, wrapper-return) that P0 passed
silently. P0.1 makes every heap-relevant operation classify as SUPPORTED,
KNOWN SAFE, KNOWN VIOLATION or UNSUPPORTED/INCOMPLETE — there is no
"unknown but PASS" (see [ADR-0010](docs/adr/ADR-0010-trustworthy-pass.md)).

Delivered:

- `free()` is fail-closed: tracked objects transition; `free(NULL)` is
  known safe; untracked variables and other expressions are INCOMPLETE
  (`free-untracked-pointer`, `free-untracked-expression`);
- allocation into unmodelled storage is INCOMPLETE
  (`allocation-to-untracked-storage`);
- pointer values from unmodelled pointer-returning calls are INCOMPLETE
  (`unknown-pointer-return-ownership`);
- exit code 2 is reserved for tool/input errors, distinct from FAIL(1);
- coverage summary reports `functions_analyzed`, `tracked_heap_objects`
  and `unsupported_ownership_operations`;
- `tests/failclosed/` regression suite and `tests/differential/` ASan
  oracle suite run in CI and reject any `ASan violation + cand PASS` pair.

Known unsupported semantics (honest list): aliases, flow-sensitive control
flow (loops/branches beyond simple null guards), struct members, array
elements, interprocedural ownership, `realloc`, callbacks, inline asm, GNU
statement expressions, ownership operations in conditionally evaluated
positions, stack pointers carried through returned structs, and any external
call lacking a trusted contract. These produce INCOMPLETE, not
PASS, and are the P1+ roadmap inputs. The verifier must evolve toward real C;
application code must not be contorted into a C& dialect to obtain PASS.

## P1 — C&1 unique ownership + first autonomous repair loop

Deliver:

- unique owner capability;
- `CAND_OWN` and inference;
- `CAND_TAKES` / owned returns;
- `CAND_MOVE` capture;
- double-free/use-after-free/use-after-move;
- owner overwrite/leak-at-exit detection;
- branch and loop dataflow;
- function summaries;
- libc allocation-family models;
- structured ownership-state traces in diagnostics;
- machine-readable repair classes;
- generated-code strict mode;
- initial `cand evidence` output;
- reference model-neutral agent loop for mandatory temporal fixtures.

Exit gate:

- deterministic conformance suite;
- Juliet/CWE-style temporal cases evaluated;
- zero known false negatives in the project-owned mandatory suite;
- false-positive/annotation burden measured, not hand-waved;
- an agent can generate or repair the project-owned use-after-free/double-free fixtures using structured C& output and converge to a passing implementation;
- the successful run emits evidence proving no new unsafe boundary/suppression was used to achieve the pass.

## P2 — Borrowing, lifetimes and richer agent repair

Deliver:

- shared borrows;
- mutable borrows;
- owner/borrow lifetime graph;
- borrowed return values;
- interior pointer derivation;
- borrow escape detection;
- destruction/move blocked by live invalidated borrow;
- field-sensitive support for common structs;
- structured borrow-origin/lifetime traces;
- safe annotation-only fix-its where mechanically provable;
- semantic repair suggestions for lifetime/order problems without silent application.

Exit gate:

- real C library pilot with a meaningful borrowed-view API;
- interprocedural lifetime fixtures;
- no safety claim for unsupported constructs;
- reference agent can resolve mandatory borrow fixtures without parsing human diagnostic prose.

## P3 — Legacy/API integration + trusted contract workflow

Deliver:

- project/vendor contract bundles;
- callbacks and retained context;
- out-owner parameters;
- `realloc` path semantics;
- union/discriminator policy;
- baseline migration workflow;
- contract explain tooling;
- optional Clang build plugin using same core;
- `cand contract propose` candidate workflow for LLM-assisted migration;
- contract provenance/trust classes;
- contract diff/review projection;
- enforcement that candidate AI-generated facts cannot silently become trusted proof inputs.

Exit gate:

- one nontrivial existing open-source C codebase adopted incrementally;
- annotation burden and analysis time published;
- one LLM-assisted contract migration case study published with candidate vs reviewed/trusted contract provenance.

## P4 — Agent proof-policy enforcement and scale

Deliver:

- base-aware proof-policy diff;
- configurable safety budgets;
- explicit detection of new unsafe boundaries and suppressions;
- checked-scope/coverage regression detection;
- trusted-contract change detection;
- mandatory proof-fixture change detection;
- CI/report projection focused on trust-boundary changes;
- cached incremental analysis suitable for high-frequency coding-agent loops;
- clean/full verification mode for release evidence.

Exit gate:

- mandatory fixtures prove an agent cannot make a failing case green by silently adding unsafe/suppression/lowering safety level/reducing checked scope;
- incremental and clean full analysis agree on pass/fail semantics across the conformance dependency graph;
- performance supports practical multi-iteration agent workflows on a real project.

## P5 — C&2 spatial model

Goal: add explicit spatial safety rather than conflating it with ownership.

Candidate deliverables:

- bounds-aware slice/view contracts;
- object extent tracking;
- pointer arithmetic bounds rules;
- bounds propagation through common APIs;
- C&2 diagnostic family;
- structured spatial proof obligations consumable by coding agents.

No C&2 claim until the formal contract is accepted.

## P6 — Nullability and provenance hardening

Deliver:

- nullable/non-null flow facts;
- pointer/integer cast policy;
- container-of/intrusive-structure supported patterns;
- explicit unsafe intrinsics for provenance-sensitive operations;
- kernel/low-level C case studies;
- policy visibility for agent-generated provenance escapes.

## P7 — Concurrency ownership

Deliver only after a separate SPEC:

- ownership transfer to threads/tasks;
- shared-state contract;
- atomic/reference-counted ownership adapters;
- no accidental cross-thread borrow lifetime violation;
- agent-facing concurrency proof obligations.

C& should not promise general data-race freedom unless it can actually prove it.

## P8 — Production hardening

- multiple supported Clang analysis frontend versions;
- GCC-specific frontend adapter only if measurements justify it;
- large-repo performance work;
- package/release signing;
- reproducible proof reports;
- evidence signing/attestation;
- editor/LSP integration;
- model-neutral agent SDK/examples;
- formalized safe-subset claim and external review;
- reproducible benchmark suite comparing human-written, LLM-generated and mixed code workflows.

## Metrics tracked from P0

Every phase should report:

### Safety and coverage

- lines/functions analyzed;
- checked vs unsafe vs unsupported coverage;
- false positive rate on curated known-safe cases;
- false negative rate on mandatory known-unsafe suite;
- number of unmodelled external ownership boundaries;
- compiler/toolchain compatibility matrix.

### Adoption

- required annotations per KLOC for legacy code;
- explicit ownership metadata generated per KLOC for new agent-authored code;
- unsafe-boundary count and delta;
- suppression count and delta;
- trusted contract count/change rate.

### Performance

- analysis wall time and peak memory;
- cache hit rate;
- cold full-check time;
- warm changed-file check time.

### Agent loop

- iterations to resolve a finding set;
- findings resolved/introduced per iteration;
- percentage of diagnostics resolved without human intervention;
- number of proof-policy changes proposed by agents;
- number of attempted unsafe/suppression/scope escapes blocked by policy;
- candidate-contract proposals vs approved contracts;
- deterministic result reproducibility across repeated runs.

A useful C& project is one developers and coding agents can realistically adopt. Safety theory without adoption evidence is insufficient; adoption ergonomics without a defensible safety claim is also insufficient; and LLM automation without a verifier that resists reward-hacking is not a safety architecture.
