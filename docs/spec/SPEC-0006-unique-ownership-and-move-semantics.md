# SPEC-0006 — P1 Unique Ownership and Move Semantics

Status: Implemented P1 subset

This specification defines the first explicit ownership capability checked by
C&. It does not define a new C grammar or a runtime ownership library.

## Canonical annotations

The public vocabulary is provided by `include/cand/cand.h`:

```c
Packet *p CAND_OWN = packet_new();
void queue_push(Packet *p CAND_TAKES);
CAND_RETURNS_OWN Packet *packet_new(void);
void packet_free(Packet *p CAND_DESTROYS);
queue_push(CAND_MOVE(p));
```

The annotations are Clang `annotate` metadata only while C& is analyzing. In
ordinary GCC/Clang builds they compile away. `CAND_MOVE(x)` is the ordinary C
expression `(x)`; it is never a runtime assignment, destructor, allocation, or
ABI operation.

## State model

C& keeps object lifetime separate from pointer storage. The heap object is
`Owned` while live and `Dead` after destruction. Each pointer storage also has
an ownership capability:

```text
Owner       authoritative owner of a live object
Alias       non-owning pointer relation
Moved       storage whose former owner transferred the capability
MaybeMoved  CFG join of Owner and Moved
Null        definitely null
MaybeNull   null on some paths
Unknown     ownership cannot be established
```

The supported transitions are:

```text
owned allocation/owned return -> Owner + live object
CAND_MOVE(owner)              -> Moved source + transferred Owner destination
destroy(Owner)                -> Dead object
use(Moved/MaybeMoved)         -> CAND-O001
move(Moved/MaybeMoved)        -> CAND-O002
destroy(Moved/MaybeMoved)     -> CAND-O003
owner overwrite while live   -> INCOMPLETE
unknown ownership transfer    -> INCOMPLETE
```

At a CFG join, `Owner` and `Moved` become `MaybeMoved`; C& never chooses the
more permissive state. Existing temporal `Dead`/`MaybeDead` diagnostics remain
separate from moved-from diagnostics.

## Explicit consuming calls

`CAND_TAKES` identifies a parameter that receives ownership. In the generated
and agent profiles, a tracked caller must write `CAND_MOVE(argument)`. The
missing marker is a semantic repair obligation, not an implicit move. The
legacy semantic profile retains P0.4's conservative compatibility behavior for
unannotated transfers; subsequent use is still `INCOMPLETE`, never PASS.

## Scope boundary

This phase supports local pointer storage, simple owned returns, consuming and
destroying summaries, wrappers, and CFG-sensitive moves. Aggregate owner
copies, callbacks/retention, complex casts, `memcpy`, globals/statics,
`realloc`, concurrency, spatial safety, and borrow exclusivity remain
unsupported or belong to later phases and therefore fail closed.

P1 does not claim C&1, general C memory safety, Rust-equivalent borrowing, or
runtime enforcement.
