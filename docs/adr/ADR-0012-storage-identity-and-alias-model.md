# ADR-0012: Storage identity and alias model

Status: accepted (P0.3)

## Decision

C& separates the heap object from the place that stores a pointer to it:

* a heap object is an `ObjectId`;
* a pointer-containing place is a `StorageId`;
* the edge between them is a `PointerRelation` (`Owner`, `Alias`, `Null`, `MaybeNull`, or `Unknown`).

Object lifetime is authoritative in `ObjectId -> ObjectInfo`. It is never
copied into a variable spelling. Consequently, `p`, `q`, and a supported
struct member may all refer to the same object, and destruction through any
of them changes the object state observed by all of them.

Storage IDs are AST-derived and deterministic. Locals are `local:name`, member
paths are `field:root.path`, and constant array elements encode the complete
index path (for example `array:root[1][0]`). The model reserves `deref:root`
for future statically resolved pointee slots.

`Null` is distinct from `Unknown`. A storage that is definitely null uses the
null relation/object sentinel. A storage whose target cannot be established
uses a separate unknown sentinel. Two different non-null targets therefore
never collapse into the null representation.

A null/object CFG join is represented as `MaybeNull`. Because P0.3 does not
carry edge predicates, an operation whose object-state and nullness are
correlated may be reported `INCOMPLETE` (`nullable-alias-state-correlation`)
rather than generating a potentially false temporal failure.

Dynamic indexes, unresolved pointees, ambiguous targets, unknown calls,
aggregate copies carrying tracked pointers, overlapping union pointer
members, pointer/integer provenance escapes, nonlocal `setjmp`/`longjmp`
control flow, and pointer-valued atomic transport are reported `INCOMPLETE`;
C& never guesses them into `PASS`.

File-scope pointer storage and static-local pointer storage are also
`INCOMPLETE` in P0.3. Their state can span function invocations, while P0.3
flow state is per function invocation. Treating them as ordinary locals would
create a false-PASS path across calls.

P0.3 aliases are temporal relationships only. They are not shared/mutable
borrows, moves, exclusivity, or lifetime proofs. Those belong to P1/P2.

## Allocation-site abstraction

Object identity remains allocation-site based inside a function. Reusing one
abstract ID for multiple runtime loop instances can resurrect a previous
iteration's destroyed instance if an alias escapes across the back edge.
Therefore **allocations syntactically contained in loops are explicitly
INCOMPLETE in P0.3** (`loop-allocation-site`). A future heap abstraction may
lift this restriction; until then the verifier fails closed.

Composite allocation expressions receive stable synthetic IDs so repeated
worklist evaluation cannot manufacture a different abstract object on each
iteration.

Leaks and owner loss remain outside the temporal-lifetime profile.

## Aggregate and overlapping storage

C structure assignment, aggregate initialization/return, or memory-copy calls
can transport pointer fields without spelling each field. P0.3 does not yet
clone field bindings through those operations, so any aggregate transport that
carries tracked pointer storage is `INCOMPLETE`.

Union fields overlap physically. Modeling `u.p` and `u.q` as independent
struct fields would be unsound, so union pointer storage remains
`INCOMPLETE` until a dedicated union/discriminator policy exists.

## Consequences

Flow-state joins merge object states independently from storage bindings. A
storage bound to different non-null objects becomes unknown. A null/object
join becomes `MaybeNull`, preserving the possible object identity while
preventing the verifier from claiming path correlation it does not model.

The project-wide ADR-0010 rule remains authoritative: an unsupported storage,
alias, provenance, lifetime, or control-flow effect makes the result
`INCOMPLETE`, never `PASS`.
