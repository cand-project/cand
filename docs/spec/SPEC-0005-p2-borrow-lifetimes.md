# SPEC-0005: P2 Borrow Relationships and Lifetime Verification

Status: Proposed
Rule-set identity: `p2-borrow-lifetime-v1`
Depends on: SPEC-0001, SPEC-0003, SPEC-0004, SPEC-0006; ADR-0011, ADR-0012, ADR-0013

## 1. Scope and claim boundary

P2 defines an analysis-only model for explicit non-owning relationships in
ordinary C. It extends the ObjectId/StorageId ownership model established by
P0/P1. C& remains a verifier that runs before the existing compiler; it is not
a C compiler and does not rewrite pointer values.

P2 does not introduce runtime reference counting, change ABI or production
code generation, or claim general C memory safety. It does not enable a C&1
claim. The permitted claim is:

> C& implements borrow/lifetime verification for its documented P2 supported
> subset.

Issue #11 remains the separate safety-claim gate.

## 2. Explicit model

C& MUST distinguish these abstract capabilities and states:

| Capability | Meaning |
|---|---|
| `Owner` | Unique lifetime authority for an object identity. |
| `SharedBorrow` | Explicit non-owning view of a parent object; shared borrows may coexist. |
| `MutableBorrow` | Explicit non-owning exclusive view of a parent object. |
| `InvalidBorrow` | A borrow whose parent is definitely dead or whose backing storage was definitely invalidated. |
| `UnknownBorrow` | A borrow whose parent, retention, provenance, or invalidation cannot be established soundly. |

Every modeled borrow has this logical record:

```text
Borrow {
    storage_id
    parent_object_id
    kind            // shared | mutable
    origin          // annotation | verified_summary | trusted_contract
    lifetime_source // object:<id> | param:<n> | trusted contract source
    state           // Live | Invalid | MaybeInvalid | Unknown
}
```

`parent_object_id` is the lifetime authority. A source variable name is never
the authority: an ownership move changes the owning storage, not the object
identity.

Ordinary C aliases do not become borrows implicitly. A borrow can originate
only from an explicit C& annotation, a verified body summary, or a trusted
contract. Candidate and LLM-generated contracts are not proof inputs.

## 3. Canonical source vocabulary

The canonical annotation vocabulary is:

```c
CAND_BORROW
CAND_BORROW_MUT
CAND_RETURNS_BORROW_FROM(n)
```

Examples:

```c
Header *packet_header(Packet *packet)
    CAND_RETURNS_BORROW_FROM(0);

Header *header CAND_BORROW = packet_header(packet);
Header *mutable_header CAND_BORROW_MUT = packet_header_mut(packet);
```

These annotations are analysis metadata only. They MUST be ABI- and
code-generation-neutral. `CAND_RETURNS_BORROW_FROM(n)` means that the return
storage derives from argument `n` and inherits that argument's ObjectId; it
does not create a new parent object.

## 4. Lifetime rules

1. A definite live borrow prevents destruction of its parent object.
2. Destruction transitions the parent ObjectId to `Dead` and dependent borrows
   to `Invalid`.
3. Access through an `InvalidBorrow` is a failure (`CAND-B002`).
4. `MaybeInvalid` and `UnknownBorrow` MUST NOT produce `PASS`; they produce a
   failure or `INCOMPLETE` according to the known operation.
5. Borrow liveness ends after the last modeled use. No runtime release is
   required. A variable remaining in lexical scope is not, by itself, a live
   borrow.
6. A pure ownership move preserves the object identity and object-derived
   borrows:

   ```text
   packet -> Moved
   other  -> Owner(obj:1)
   borrow -> derives from obj:1 and remains valid
   ```

7. Operations that may relocate storage, or whose effects cannot establish
   whether storage moved, are not silently treated as safe.

For simple CFG joins, states join conservatively:

```text
Live + Live               = Live
Invalid + Invalid         = Invalid
Live + Invalid            = MaybeInvalid
Unknown + anything        = Unknown
```

The analyzer SHOULD use an NLL-like last-modeled-use calculation where its
control-flow facts are sufficient. Otherwise it MUST retain the borrow until
the conservative end of the modeled region and may report `INCOMPLETE`.

The first implementation uses a source-order future-use check plus the
existing forward CFG joins. This is intentionally conservative at branch
joins: a borrow is retained when a path may still use it, and unsupported
transport or retention remains `INCOMPLETE`.

This can over-reject mutually exclusive paths involving conditional
destruction followed by an early return, or conditional `goto` paths. These
are precision limitations (`MEDIUM`), not safety claims: the implementation
must not turn such paths into `PASS` until path-sensitive liveness is added.

## 5. Shared and mutable relationships

The first P2 semantic slice supports one owner with any number of compatible
shared borrows. It does not infer read-only behavior from an ordinary C pointer
type; the explicit `CAND_BORROW` relationship supplies the shared-borrow
intent.

Mutable borrows are whole-object exclusive relationships in P2. While a
`MutableBorrow` is live, another mutable borrow, an incompatible shared borrow,
or an owner access that may affect the same object is a conflict. Such a
conflict is `CAND-B004`. P2 does not claim disjoint field/subobject reasoning.

## 6. Returns, wrappers, and contracts

`CAND_RETURNS_BORROW_FROM(n)` and the trusted contract form below establish
the same relationship:

```yaml
returns:
  ownership: borrowed
  lifetime:
    from_param: 0
  borrow_kind: shared
```

The return borrow's `parent_object_id` is the argument's existing ObjectId.
Verified wrappers compose this relationship through their argument mapping;
they do not create an artificial parent. A trusted contract may establish the
relationship only when its trust class and policy permit it.

An unknown or candidate contract cannot establish a trusted lifetime proof.

## 7. Escape and unknown retention

A borrow stored in global, static, or otherwise longer-lived storage is a
borrow escape. If the escape is known, C& reports `CAND-B003`. If the storage
or retention behavior cannot be modeled soundly, C& reports `INCOMPLETE`.

Passing a borrow to an unknown call, callback registration, `memcpy`, an
unsupported aggregate/array transport, or an unsupported cast MUST NOT discard
the borrow metadata. If a trusted summary proves that a call does not retain
the borrow, the call may remain within the supported subset; otherwise the
result is `INCOMPLETE`.

`realloc` is `INCOMPLETE` while a live borrow may be affected. A successful
reallocation can move storage and invalidate interior pointers, so P2 does not
assume that `realloc` preserves a borrow.

## 8. Stable diagnostics

The canonical P2 diagnostic IDs are:

| ID | Name | Meaning |
|---|---|---|
| `CAND-B001` | `owner-destroyed-with-live-borrow` | Destruction is attempted while a dependent borrow is live. |
| `CAND-B002` | `borrow-use-after-parent-death` | A borrow is accessed after its parent became invalid. |
| `CAND-B003` | `borrow-escapes-parent-lifetime` | A borrow is stored or returned beyond the modeled parent lifetime. |
| `CAND-B004` | `conflicting-mutable-borrow` | A mutable borrow conflicts with another modeled relationship. |

Machine findings MUST expose enough information for an agent to repair the
program without parsing prose:

```json
{
  "id": "CAND-B002",
  "rule_id": "p2-borrow-lifetime-v1",
  "object_id": "obj:12",
  "borrow": {
    "storage": "local:header",
    "kind": "shared",
    "origin": "packet_header(packet)",
    "lifetime_source": "object:obj:12",
    "state": "Invalid",
    "created_at": {"file": "router.c", "line": 51, "column": 20}
  },
  "parent": {
    "object_id": "obj:12",
    "invalidated_at": {"file": "router.c", "line": 61, "column": 5}
  },
  "invalid_access": {"file": "router.c", "line": 65, "column": 12},
  "repair_class": "SEMANTIC_REPAIR"
}
```

## 9. Supported first slice and deliberate incompleteness

The first implementation slice covers explicit shared borrows, ObjectId-based
parentage, borrowed returns from parameters, wrapper summary composition,
last-modeled-use liveness, destruction/use-after-death diagnostics, basic CFG
joins, and fail-closed unknown escape.

The following remain `INCOMPLETE` unless a later rule-set explicitly models
them: field-sensitive disjointness, dynamic array indexes, unsupported
aggregate and union transport, `memcpy`, relocation through `realloc`, unsafe
or provenance-erasing casts, callbacks and unknown retention, concurrency,
and general C pointer arithmetic. A false `INCOMPLETE` is acceptable; a false
`PASS` in the claimed subset is not.

## 10. Evidence and policy

P2 evidence uses `p2-borrow-lifetime-v1` and reports:

```json
{
  "borrow_analysis": {
    "borrows_created": 17,
    "shared_borrows": 14,
    "mutable_borrows": 3,
    "invalidated_borrows": 0,
    "unsupported_borrow_operations": 0
  }
}
```

P0.5/P0.5.1 policy remains mandatory. An agent MUST NOT make a borrow finding
disappear by removing a borrow annotation, weakening a trusted contract,
changing scope, adding unsafe/suppression, changing the verifier or policy,
or forging evidence. Any reduction in claimed checked semantics is a policy
and evidence change, not a successful semantic repair.
