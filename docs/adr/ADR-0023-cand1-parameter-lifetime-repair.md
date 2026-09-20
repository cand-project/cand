# ADR-0023 — C&1 Parameter-Lifetime Soundness Repair

Status: proposed for the #46 repair branch

## Decision

Modeled pointer parameters enter each function as symbolic objects in the
callee's initial `FlowState`. A parameter's liveness and its capability are
separate facts:

- `ObjectInfo.state` records whether the symbolic object is live, dead, or
  conditionally dead;
- object origin distinguishes an allocation from a parameter entry object;
- parameter capability distinguishes `Borrow`, `TakeOwnership`, and `Destroy`.

`FunctionSummary::params` establishes the entry capability only. It does not
authorize later access or destruction without consulting the current
`FlowState` binding and object state.

## Parameter entry

For each pointer parameter with a supported effect, the entry state creates:

- a deterministic `StorageId` rooted at the `ParmVarDecl`;
- a symbolic parameter object with initial live state;
- `Owner` relation for `TakeOwnership`;
- non-owner alias relation for `Borrow` and `Destroy`.

`Borrow` can be read according to existing rules but cannot destroy or move the
object. `Destroy` can perform its destructive operation but cannot manufacture
an owner or transfer ownership. `TakeOwnership` is the only parameter entry
capability that can move the owner.

## Object identity

Allocation object IDs remain in the low unsigned namespace. Parameter object IDs
use a reserved high-bit namespace. Allocation ID generation rejects IDs that
would enter the parameter namespace, and parameter ID construction rejects
overflow into the unknown sentinel. This makes the namespaces disjoint by
construction rather than by expected source size.

The parameter ID is stable for the function's parameter index across CFG
iterations. It is local to a `FlowAnalyzer` instance; equal numeric IDs in
different functions are never joined.

## State transitions

For `TakeOwnership`:

```text
entry -> Owner/Live
free(p) -> Dead
use(p) or free(p) after death -> existing temporal finding
CAND_MOVE(p) -> Moved; destination/callee receives the same object
```

Simple local aliases retain the same object ID and observe the same state.
Derived pointers, aggregate fields, callbacks, and other unsupported storage
remain fail-closed.

For `Destroy`:

```text
entry -> non-owner destructive capability/Live
first destruction -> Dead
later destruction or access -> existing temporal finding
ownership move -> unsupported
```

For `Borrow`, destruction is rejected as an ownership violation or remains
unsupported; it can never silently become an owner.

CFG joins use the existing `ObjectState` lattice, so `Owned` joined with `Dead`
becomes `MaybeDead`, and later access or destruction remains diagnostic rather
than becoming definitely live.

## Compatibility and scope

This repair removes direct-parameter permission fast paths and routes modeled
parameter operations through the existing binding/lifetime machinery. It does
not add cross-TU summaries, pointer-output ownership, realloc semantics,
callback retention, aggregate/field aliasing, or general derived-pointer
support. PR #45 contract/body reconciliation remains unchanged.

The public C&1/v1 claim stays suspended until the complete exact-head
qualification and independent review required by the false-PASS policy pass.
