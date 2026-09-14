# C& Implementation Roadmap

C& should be built evidence-first. Each phase must prove its contract on real C code before the next safety claim is enabled.

## P0 — Feasibility and semantic core

Goal: prove the pipeline architecture without pretending to provide memory safety yet.

Deliver:

- standalone `cand` LibTooling executable;
- compilation database loading;
- C& annotation header;
- versioned contract schema + validator;
- frontend-neutral semantic IR prototype;
- basic allocation/free object identity;
- stable diagnostics JSON format;
- human and SARIF diagnostics;
- positive/negative fixture suite;
- upstream Clang only, no fork.

Exit gate:

- same example source compiles with unmodified Clang and GCC;
- C& can identify malloc/free lifecycle and source locations;
- unsupported constructs are reported explicitly.

## P1 — C&1 unique ownership

Deliver:

- unique owner capability;
- `CAND_OWN` and inference;
- `CAND_TAKES` / owned returns;
- `CAND_MOVE` capture;
- double-free/use-after-free/use-after-move;
- owner overwrite/leak-at-exit detection;
- branch and loop dataflow;
- function summaries;
- libc allocation-family models.

Exit gate:

- deterministic conformance suite;
- Juliet/CWE-style temporal cases evaluated;
- zero known false negatives in the project-owned mandatory suite;
- false-positive/annotation burden measured, not hand-waved.

## P2 — Borrowing and lifetimes

Deliver:

- shared borrows;
- mutable borrows;
- owner/borrow lifetime graph;
- borrowed return values;
- interior pointer derivation;
- borrow escape detection;
- destruction/move blocked by live invalidated borrow;
- field-sensitive support for common structs.

Exit gate:

- real C library pilot with a meaningful borrowed-view API;
- interprocedural lifetime fixtures;
- no safety claim for unsupported constructs.

## P3 — Legacy/API integration

Deliver:

- project/vendor contract bundles;
- callbacks and retained context;
- out-owner parameters;
- `realloc` path semantics;
- union/discriminator policy;
- baseline migration workflow;
- contract explain tooling;
- optional Clang build plugin using same core.

Exit gate:

- one nontrivial existing open-source C codebase adopted incrementally;
- annotation burden and analysis time published.

## P4 — C&2 spatial model

Goal: add explicit spatial safety rather than conflating it with ownership.

Candidate deliverables:

- bounds-aware slice/view contracts;
- object extent tracking;
- pointer arithmetic bounds rules;
- bounds propagation through common APIs;
- C&2 diagnostic family.

No C&2 claim until the formal contract is accepted.

## P5 — Nullability and provenance hardening

Deliver:

- nullable/non-null flow facts;
- pointer/integer cast policy;
- container-of/intrusive-structure supported patterns;
- explicit unsafe intrinsics for provenance-sensitive operations;
- kernel/low-level C case studies.

## P6 — Concurrency ownership

Deliver only after a separate SPEC:

- ownership transfer to threads/tasks;
- shared-state contract;
- atomic/reference-counted ownership adapters;
- no accidental cross-thread borrow lifetime violation.

C& should not promise general data-race freedom unless it can actually prove it.

## P7 — Production hardening

- multiple supported Clang analysis frontend versions;
- GCC-specific frontend adapter only if measurements justify it;
- incremental/cached analysis;
- large-repo performance work;
- package/release signing;
- reproducible proof reports;
- editor/LSP integration;
- formalized safe-subset claim and external review.

## Metrics tracked from P0

Every phase should report:

- lines/functions analyzed;
- checked vs unsafe vs unsupported coverage;
- analysis wall time and peak memory;
- cache hit rate;
- required annotations per KLOC;
- false positive rate on curated known-safe cases;
- false negative rate on mandatory known-unsafe suite;
- number of unmodelled external ownership boundaries;
- compiler/toolchain compatibility matrix.

A useful C& project is one developers can realistically adopt. Safety theory without adoption evidence is insufficient; adoption ergonomics without a defensible safety claim is also insufficient.
