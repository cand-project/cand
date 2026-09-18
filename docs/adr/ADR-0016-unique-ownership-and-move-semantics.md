# ADR-0016 — Explicit Unique Ownership and Move Semantics

Status: Accepted for P1

## Decision

C& represents explicit ownership transfer as analysis state attached to pointer
storage. `CAND_MOVE(x)` remains `(x)` in ordinary C and is recognized only as
source intent by the verifier. A successful move changes the source storage to
`Moved` and creates/updates one authoritative `Owner` storage for the same
object identity.

The live object's lifetime is not changed by a move. This prevents a stale
owner diagnostic from being confused with destruction and preserves the P0
object/storage separation.

## Rules

`CAND_TAKES` requires an explicit `CAND_MOVE` in generated/agent mode. Trusted
contracts and body-derived summaries provide the callee effect but cannot
invent ownership authority for candidate code. A missing or ambiguous transfer
is a finding or `INCOMPLETE`, never a permissive PASS.

The finite join of an owner and moved-from path is `MaybeMoved`. Unknown,
aggregate, callback, retention, cast, and other unsupported transfers remain
fail-closed. Stable machine diagnostics are `CAND-O001` through `CAND-O005`.

## Consequences

The annotation vocabulary is ordinary C-compatible metadata and has no effect
on production ABI, object layout, or runtime behavior. P1 extends evidence with
the `p1-unique-ownership-v1` rule-set identifier and transition counters while
retaining the existing `p0-temporal-lifecycle` safety-level claim boundary.
