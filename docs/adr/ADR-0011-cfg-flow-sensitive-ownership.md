# ADR-0011: CFG-Based Flow-Sensitive Ownership State

- **Status:** Proposed
- **Date:** 2026-09-15
- **Decision owners:** C& maintainers
- **Scope:** analyzer architecture, ownership state model, path sensitivity, diagnostics evidence
- **Depends on:** ADR-0001, ADR-0002, ADR-0010

## Context

P0.1 established the invariant that `PASS` may only be reported when every
ownership-affecting operation in checked scope is understood by the verifier
or covered by a trusted contract (ADR-0010). Its implementation, however,
interpreted statements largely in **source order** inside each function.

That design cannot express the most ordinary C lifetime shapes:

```c
int *p = malloc(sizeof *p);

if (error) {
    free(p);
    return 1;
}

*p = 42;
free(p);
```

The source contains `free(p)` textually before the later access, but the
later access is only reachable on the path where the free did not happen. A
linear walker must either emit a false positive or declare the whole
function INCOMPLETE. Neither is acceptable: the first destroys trust in
`FAIL`, the second pushes generated code away from normal C idioms.

## Decision

Ownership state SHALL be attached to **program points** and propagated over
the function's control-flow graph, not to source-order statements.

The pipeline becomes:

```text
Clang AST
   |
   v
clang::CFG  (upstream Clang CFG APIs; no fork, no new parser)
   |
   v
basic blocks
   |
   v
ownership dataflow state per block
   |
   +--> transfer function (per statement/expression element)
   |
   +--> join / merge at block entries
   |
   +--> worklist fixed point
   |
   v
CAND-T002 / CAND-T003 / CAND-U001
```

### State lattice

```text
Untracked   storage holds no tracked object and no known value
Null        storage definitely holds NULL (e.g. after `p = NULL`)
Owned       object alive on every represented path
Dead        object destroyed on every represented path
MaybeDead   alive on some represented paths, destroyed on others
Unknown     C& cannot soundly model this storage's ownership state
```

`Null` is a modeled value, not an unknown: `free(p)` after `p = NULL` is a
defined no-op (the same rule already applied to literal `free(NULL)`), and a
dereference through it is a null-dereference issue that P0 does not claim to
cover — so it is neither a lifetime violation nor an unresolved obligation.

`Unknown` is not `MaybeDead`: `MaybeDead` is a modeled join result, while
`Unknown` means the verifier lost the model and any ownership-relevant use
of that storage is INCOMPLETE.

### Join rules (deterministic)

```text
join(x, x)                 = x                     for x in {Untracked, Owned, Dead, Unknown}
join(Owned, Dead)          = MaybeDead
join(Dead, Owned)          = MaybeDead
join(MaybeDead, Owned)     = MaybeDead
join(MaybeDead, Dead)      = MaybeDead
join(MaybeDead, MaybeDead) = MaybeDead
join(Unknown, anything)    = Unknown
join(Untracked, tracked)   = Unknown
join(Null, Owned)          = Owned        (destruction is safe on both paths)
join(Null, Dead)           = MaybeDead    (safe on one path, violation on the other)
join(Null, MaybeDead)      = MaybeDead
```

Storage that is tracked on one incoming path and absent on another joins to
`Unknown`: the variable holds a pointer whose ownership differs per path,
and C& will not guess.

### Transfer functions

```text
p = malloc(...)      binds a fresh object id, state Owned
                     (assignment over an already-live binding is
                      tracked-owner-overwrite: INCOMPLETE)
free(p)              Owned      -> Dead
                     Dead       -> CAND-T003 (certainty definite)
                     MaybeDead  -> CAND-T003 (certainty possible)
                     Unknown    -> INCOMPLETE
                     untracked  -> INCOMPLETE (free-untracked-pointer)
*p, p[i], p->f       Dead       -> CAND-T002 (certainty definite)
                     MaybeDead  -> CAND-T002 (certainty possible)
                     Unknown    -> INCOMPLETE
p = NULL             storage becomes Null (release; leak not modeled in P0.2)
free(p) with Null    defined no-op (KNOWN SAFE)
```

### Loop convergence

The lattice is finite (five states per storage, finite storage set), and the
transfer function is monotone, so the worklist reaches a fixed point. Loop
bodies that do not change ownership state converge to `Owned`; bodies that
destroy execute again with the join of the back edge, which surfaces
possible double destruction rather than assuming a single iteration.

### What remains unsupported (deliberately)

Aliases, struct-member and array-element object tracking, pointee stores,
interprocedural ownership, callbacks, `realloc`, threads, spatial bounds
and computed `goto` remain INCOMPLETE. The CFG rewrite must not convert any
of them into PASS.

## Diagnostic emission

Diagnostics are emitted in a **post-convergence pass**, re-running the
transfer function over every reached block with its final in-state. Emitting
during intermediate worklist iterations would leave stale findings and
obligations behind when a state later moves up the lattice (for example when
a join finally reveals that a storage is `Unknown`). Diagnostics therefore
describe the fixed point, deterministically, once per program point.

## Diagnostics

Path-dependent findings carry the certainty and the state that produced
them:

```json
{
  "id": "CAND-T002",
  "rule_id": "cand1.no-use-after-death",
  "certainty": "possible",
  "state_before_access": "MaybeDead",
  "object_id": "obj:3",
  "state_trace": [
    {"event": "allocation", "state": "Owned"},
    {"event": "conditional_destruction", "state": "MaybeDead"},
    {"event": "access", "state": "MaybeDead"}
  ]
}
```

`certainty: definite` means every represented path reaches the point with
`Dead`. `certainty: possible` means at least one represented path does. An
LLM repairing the code can distinguish "this always happens" from "this
happens on one path".

Object identity is currently per allocation **site**, not per dynamic
instance: every execution of the same `malloc` call reuses one `obj:N`
label. This is adequate for the P0.2 scope and is documented as a limit.

## Consequences

### Positive

- ordinary `if`/`else`, early returns, multi-return cleanup, `goto`
  cleanup, `switch`, `break`/`continue` and simple loops are analyzed
  instead of being rejected wholesale;
- path-dependent use-after-free and double destruction become `FAIL` with
  a certainty classification, not INCOMPLETE;
- safe branch-specific destruction stops producing false negatives *and*
  false positives;
- the same CFG layer is the foundation for alias/member/interprocedural
  work in later phases.

### Negative

- more implementation surface and more ways to be subtly wrong; mitigated
  by the differential ASan corpus and the CFG adversarial corpus;
- conservative joins produce INCOMPLETE for variables whose ownership
  differs across paths;
- findings must be deduplicated across worklist iterations, and the last
  (most conservative) classification for a program point wins.

## Invariants

1. Every ownership-relevant operation still classifies as SUPPORTED,
   KNOWN SAFE, KNOWN VIOLATION or UNSUPPORTED/INCOMPLETE (ADR-0010 holds).
2. Joins are deterministic and documented; no predecessor state is silently
   preferred.
3. `Unknown` is never downgraded to `Owned`.
4. Unreachable CFG blocks are not analyzed and do not invent obligations.
5. Aliases, members, arrays, wrappers and unknown calls remain INCOMPLETE.
6. Machine output stays deterministic for identical inputs.

## Acceptance criteria

ADR-0011 is implemented when:

1. `if`/`else` no longer implies INCOMPLETE;
2. early-return lifetime patterns are analyzed correctly;
3. path-dependent UAF and double destruction produce FAIL;
4. safe branch-specific destruction produces neither FAIL nor INCOMPLETE;
5. loops converge under the fixed-point with conservative treatment of
   ownership changes;
6. unsupported constructs from P0.1 remain INCOMPLETE;
7. the differential corpus still contains no `ASan violation + cand PASS`;
8. JSON findings carry `certainty` and flow evidence.
