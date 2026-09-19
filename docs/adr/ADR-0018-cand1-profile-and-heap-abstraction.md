# ADR-0018 — C&1 strict profile and bounded heap generations

Status: Accepted for C&1-A

## Context

P0–P2 used allocation-site identity as a convenient stable key. That is not a
sound C&1 identity when one allocation expression executes more than once:
an alias from iteration N must not silently refer to the object allocated in
iteration N+1.

C&1 also needs one acceptance decision. Independent result paths must not
turn an unsupported ownership/lifetime operation into `PASS`.

## Decision

### Strict profile

`--level cand1` selects the experimental `cand1/v1` profile. It enables the
P0/P1/P2 rules plus strict zero-unsupported acceptance. The implementation is
not a public C&1 claim until the later release gate.

The final result is computed by one central predicate over the semantic report,
policy state, and evidence bindings. Default P0–P2 behavior remains unchanged.

### Heap abstraction

Each allocator expression receives a deterministic site number. A flow state
also carries the highest generation observed for each site. Binding an
allocation proceeds as follows:

1. If the site's current abstract object is dead and no other storage or
   borrow retains its identity, generation zero may be reused.
2. If an old storage or borrow can still refer to the object, bind a fresh
   deterministic generation object.
3. If a fresh generation would continue growing through a loop/fixed point,
   widen that site's generation to `Unknown` and emit an incomplete
   `loop-heap-instance-widening` obligation.

Generation state joins by maximum generation and a monotone widened bit.
Object IDs are deterministic for `(site, generation)` and remain stable across
worklist iterations. A widened object is never treated as an owner target.

The model intentionally prefers `INCOMPLETE` over an identity guess. Safe
per-iteration allocate/free is representable when no alias survives the free;
stale aliases, nested-loop ambiguity, and unsupported recursive widening are
closed by the same unknown state.

### Fixed point and bounds

The dataflow remains a finite monotone fixed point. Each site has a bounded
generation budget of one fresh generation beyond reuse; a further ambiguous
instance widens permanently. This gives bounded state and deterministic
convergence. The budget is a precision boundary, not a claim that runtime
objects are bounded.

## Consequences

* A loop stale-alias case cannot receive `PASS` through allocation-site reuse.
* Safe loops with no surviving aliases can remain precise.
* Some safe repeated-allocation programs become `INCOMPLETE`; this is an
  explicit C&1 precision limitation.
* Full aggregate transport, `realloc`, callbacks, and other transport
  boundaries remain later C&1 sub-gates.

## Rejected alternatives

* Reusing one allocation-site ID forever: unsound for stale aliases.
* Creating an unbounded fresh ID on every fixed-point visit: non-convergent.
* Treating loop allocations as universally unsupported: safe per-iteration
  allocation/free cannot be verified and issue #8 remains unresolved.
