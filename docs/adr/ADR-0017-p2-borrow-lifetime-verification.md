# ADR-0017: P2 Borrow Relationships and Lifetime Verification

- Status: Proposed
- Date: 2026-09-18
- Scope: P2 borrow model, lifetime dataflow, diagnostics, contracts, and evidence
- Depends on: ADR-0004, ADR-0006, ADR-0007, ADR-0009, ADR-0010, ADR-0011, ADR-0012, ADR-0013

## Context

P0/P1 establish explicit ownership, ObjectId/StorageId separation, moves,
CFG state, and interprocedural summaries. P2 adds explicit non-owning
relationships while remaining ordinary C. The central safety property is that
a borrow is valid only while its parent object and backing storage remain
valid.

## Decision

C& SHALL model `Owner`, `SharedBorrow`, `MutableBorrow`, `InvalidBorrow`, and
`UnknownBorrow`. Each modeled borrow SHALL carry `storage_id`,
`parent_object_id`, `kind`, `origin`, `lifetime_source`, and `state`.

Borrow relationships SHALL originate only from explicit C& annotations,
verified summaries, or trusted contracts. Ordinary aliases SHALL NOT become
implicit borrows. The canonical return annotation is
`CAND_RETURNS_BORROW_FROM(n)`, and the trusted contract equivalent is
`returns.ownership: borrowed` with `returns.lifetime.from_param`.

Parentage SHALL use ObjectId, not variable spelling. A pure ownership move
preserves an object-derived borrow. Relocation, unknown retention, unsupported
aggregate transport, `memcpy`, and provenance-erasing casts SHALL be
`INCOMPLETE` until modeled.

A live dependent borrow SHALL block parent destruction. Parent death SHALL
invalidate dependent borrows, and access through an invalid borrow SHALL fail.
`MaybeInvalid` and `UnknownBorrow` SHALL never produce `PASS`. Liveness SHALL
be based on the last modeled use where practical, with conservative CFG joins.
The initial implementation realizes this with a source-order future-use check
and the existing forward CFG dataflow; branch joins retain a possible borrow
and may therefore over-reject rather than claim safety.

Independent mutation review found three instances of this expected precision
limit: mutually exclusive conditional destruction/early-return and conditional
`goto` paths can report `CAND-B001` even when the later borrow use is unreachable
from the destruction path. They are tracked as `MEDIUM` precision findings;
no false `PASS` was observed.

Shared borrows may coexist. Mutable borrows are exclusive at whole-object
granularity in this slice; field-level disjointness is not inferred.

The canonical diagnostics are `CAND-B001` through `CAND-B004`. Structured
findings SHALL expose the borrow storage, kind, origin, creation location,
parent ObjectId, invalidation location, and invalid access location where
applicable.

P2 evidence SHALL use rule-set identity `p2-borrow-lifetime-v1` and include
`borrow_analysis` counters. Existing agent-proof policy remains in force, with
GitHub protected pull-request review as the active merge authority.

## Consequences

The model rejects destruction with a live borrow and use after parent death,
preserves borrow identity across pure moves, and fails closed on unsupported
retention or relocation. Some valid C programs will be `INCOMPLETE` until
their storage and call effects are modeled.

Annotations remain analysis-only, so ABI and production code generation do not
change. P2 does not claim C memory safety or C&1 completion.

## Non-goals

P2 does not introduce a compiler, runtime ownership, reference counting,
pointer rewriting, spatial safety, concurrency safety, general alias
inference, or a Rust borrow checker for C.

## Acceptance criteria

- the normative P2 model and canonical vocabulary are versioned;
- shared borrows and borrowed returns have ObjectId-based parentage;
- destruction with a live borrow and use after parent death are diagnosable;
- known escapes fail and unknown retention is `INCOMPLETE`;
- CFG joins and last-modeled-use behavior are documented;
- machine diagnostics carry a complete lifetime trace;
- JSON/YAML contracts validate and remain below the C&1 claim boundary;
- implementation remains limited to the documented P2 supported subset; no
  policy relaxation or C&1 claim is implied.
