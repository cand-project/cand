# ADR-0012: Storage identity and alias model

Status: accepted (P0.3)

## Decision

C& separates the heap object from the place that stores a pointer to it:

* a heap object is an `ObjectId`;
* a pointer-containing place is a `StorageId`;
* the edge between them is a `PointerRelation` (`Owner`, `Alias`, or `Unknown`).

Object lifetime is authoritative in `ObjectId -> ObjectInfo`. It is never
copied into a variable spelling. Consequently, `p`, `q`, and a struct member
may all refer to the same object, and destruction through any of them changes
the object state observed by all of them.

Storage IDs are AST-derived and deterministic. Locals are `local:name`, member
paths are `field:root.path`, constant array elements are `array:root[i]`, and
the model reserves `deref:root` for future statically resolved pointee slots.
Dynamic indexes, unresolved pointees, ambiguous targets, and unknown calls are
reported `INCOMPLETE`; C& never guesses them into `PASS`.

P0.3 aliases are temporal relationships only. They are not shared/mutable
borrows, moves, exclusivity, or lifetime proofs. Those belong to P1/P2.

Allocation identity remains allocation-site based. A loop may therefore map
multiple runtime instances to one abstract object; the conservative result is
preferred until a future heap abstraction addresses that limitation.

Leaks and owner loss remain outside the temporal-lifetime profile.

## Consequences

Flow-state joins merge object states independently from storage bindings. A
storage bound to different non-null objects becomes unknown; a null/object join
retains the object identity while its object state is joined conservatively.
