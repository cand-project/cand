# ADR-0019 — C&1 fail-closed pointer transport boundary

Status: Accepted for C&1-B

## Context

Ownership and borrow facts are attached to abstract storage and object
identity. C has many operations that can copy, hide, retain, relocate, or
reinterpret a pointer. Dropping that metadata at any one boundary can turn an
invalid lifetime into a false `PASS`.

## Decision

Keep the existing single `can_emit_cand1_pass` authority and add an explicit
transport-completeness obligation to its input. The analyzer classifies
tracked-state transport as either:

1. a supported direct storage/effect operation already modeled by P0–P2; or
2. an unsupported transport diagnostic, which forces `INCOMPLETE`.

The initial C&1-B boundary is deliberately conservative. Aggregate storage,
arrays, unions, byte copies, casts that may change provenance, interior
pointers, `realloc`, non-local storage, out parameters, unknown/callback/
function-pointer calls, varargs, atomics, non-local jumps, and opaque assembly
are fail-closed when tracked state is involved. Trusted contracts and verified
interprocedural summaries remain the only authority for external effects.

Unsupported transport is represented in machine output by `CAND-U001` plus a
stable `mechanism`, optional source/destination storage, and tracked-state
fields. The evidence analysis contains
`unsupported_transport_operations`; a C&1 evidence artifact cannot claim
`PASS` while it is nonzero.

## Consequences

* Metadata cannot silently disappear through the C&1 transport boundary.
* Safe code using an unsupported transport may be `INCOMPLETE`, by design.
* Existing P0–P2 semantic findings remain failures; this ADR does not weaken
  them or claim that every unsupported operation is safe.
* Full aggregate transport, provenance, callbacks, and `realloc` modeling are
  deferred to later qualification work.

## Rejected alternatives

* Treating unknown transport as a normal pointer: unsound.
* Treating every aggregate copy as a new owner: invents ownership and is
  unsound.
* Relying on sanitizer execution to establish static ownership semantics:
  incomplete and path-dependent.
