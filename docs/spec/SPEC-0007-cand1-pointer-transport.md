# SPEC-0007 — C&1 pointer transport boundary

Status: Draft release gate (C&1-B)

## 1. Purpose

`cand1/v1` must not lose ownership or borrow authority when a pointer is
transported through a C construct. A construct is either modeled by the
checked semantics or it creates an `INCOMPLETE` obligation. An unmodelled
transport is never ignored and cannot contribute to `PASS`.

This specification extends the experimental C&1 profile; it does not enable
the public C&1 claim.

## 2. Transport matrix

| Mechanism | Ownership/borrow transport | cand1/v1 status | Required behavior |
|---|---|---|---|
| local pointer assignment/reassignment | direct storage identity | SUPPORTED where the existing storage model proves it | ordinary P0–P2 rules |
| direct scalar argument/return | modeled parameter/return effect | SUPPORTED with a body-verified summary or trusted contract | summary/contract authority required |
| struct, nested struct, array, aggregate init/assignment/return/parameter | field or element state | UNSUPPORTED | `INCOMPLETE` when tracked state is involved |
| union, compound literal, designated aggregate | active-member or aggregate state | UNSUPPORTED | `INCOMPLETE` |
| `memcpy`, `memmove`, character-buffer copy | bytewise state transport | UNSUPPORTED | `INCOMPLETE` |
| typed, `void *`, or character-pointer cast | provenance/state transport | UNSUPPORTED unless a verified effect preserves identity | `INCOMPLETE` |
| pointer/integer round trip | provenance transport | UNSUPPORTED | `INCOMPLETE` |
| pointer arithmetic/interior pointer | derived region identity | UNSUPPORTED in v1 | `INCOMPLETE` |
| global or static storage | non-local lifetime transport | UNSUPPORTED unless an explicit supported escape rule proves it | `INCOMPLETE` or B003 |
| out parameter (`T **`) | ownership creation/transport | UNSUPPORTED in v1 | `INCOMPLETE` |
| function pointer, callback, plugin, foreign or unknown call | retention/effect transport | UNSUPPORTED without a trusted effect | `INCOMPLETE` |
| varargs | unknown transport and retention | UNSUPPORTED | `INCOMPLETE` |
| atomics | concurrent pointer transport | UNSUPPORTED | `INCOMPLETE` |
| `realloc` | possible relocation and invalidation | UNSUPPORTED | `INCOMPLETE` when tracked state is involved |
| `setjmp`/`longjmp` | non-local lifetime transition | UNSUPPORTED | `INCOMPLETE` |
| inline assembly/compiler extension | opaque state transition | UNSUPPORTED | `INCOMPLETE` |

The supported direct path does not infer ownership from names or ordinary C
aliasing. It relies on verified bodies, built-in contracts, or trusted
reviewed contracts with explicit effects such as `takes_ownership`,
`does_not_retain`, and `returns_borrow_from(n)`.

## 3. Completeness invariant

The existing `can_emit_cand1_pass` predicate remains the sole success
authority. C&1 `PASS` additionally requires:

```text
unsupported_transport_operations == 0
```

The count is recorded in the check result and evidence. Every emitted
unsupported transport includes a stable mechanism classification and primary
location. The transport count is deduplicated with the unsupported diagnostic.

## 4. Evidence and repair

Evidence binds the transport rule set and completeness count. A transport
obligation is actionable machine data, not an absent fact. An agent may repair
it by making the operation explicit and supported, moving the operation out of
the checked lifetime, or adding an independently trusted effect. It may not
repair it by changing policy, suppressing the finding, or deleting required
ownership intent.

## 5. Explicit exclusions

This boundary does not claim spatial safety, general pointer provenance,
thread safety, arbitrary compiler-extension behavior, or foreign-language
safety. Those remain outside cand1/v1 until separately modeled and qualified.
