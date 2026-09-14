# Before and after C&

C& does **not** make unsafe C safe merely by adding annotations. The safety mechanism is the analysis gate: invalid ownership/lifetime states are rejected before a checked build is admitted.

This distinction is deliberate and testable.

## Example 1: use after free

### Ordinary C

```c
int *p = malloc(sizeof *p);
*p = 7;
free(p);
printf("%d\n", *p);   /* temporal memory defect */
```

Ordinary C compilers can accept this program even with strong warning flags. A runtime detector such as AddressSanitizer observes the defect only if the bad path executes.

### C& metadata without enforcement

```c
int *p CAND_OWN = malloc(sizeof *p);
*p = 7;
free(p);
printf("%d\n", *p);
```

If `cand check` is bypassed, production-profile C& metadata is code-generation-neutral. The program still contains the same defect. This is required by ADR-0004.

### With C& enforcement

The future C&1 checker is expected to derive a state sequence similar to:

```text
malloc -> p owns object #1
free(p) -> object #1 becomes Dead
*p      -> illegal access to Dead object
```

and reject the checked build with:

```text
CAND-T002 use-after-destroy
```

The binary is not “patched” at runtime. The unsafe source is prevented from passing the checked build gate.

### Source repair

A valid repair preserves the use while the object is live and destroys it afterward:

```c
int *p CAND_OWN = malloc(sizeof *p);
*p = 7;
printf("%d\n", *p);
free(p);
```

C& can explain this repair. Moving `free()` is a semantic edit, so under ADR-0003 it is a review-required suggestion rather than a silent automatic rewrite.

## Example 2: ownership transfer / use after move

```c
Packet *p CAND_OWN = packet_new();
packet_send(CAND_MOVE(p));
packet_debug(p); /* ownership-state error */
```

Assume the trusted `packet_send` contract says it consumes the packet.

C& state:

```text
packet_new       -> p = Owned
CAND_MOVE(p)     -> ownership transferred to packet_send
return from call -> p = Moved
packet_debug(p)  -> CAND-T001 use-after-move
```

This example may not be a runtime use-after-free in every implementation: the callee might queue or retain the object. It is still an ownership-contract violation. This is why C& cannot define correctness solely through runtime sanitizers.

A developer must decide the intended design:

- do not use `p` after transfer;
- move the debug operation before transfer;
- change the API so it borrows instead of consumes, if that is the true contract;
- return/reacquire ownership through an explicit API.

C& should not guess among these designs.

## Example 3: live borrow and destruction

```c
Packet *p CAND_OWN = packet_new();
Header *h CAND_BORROW = packet_header(p);

packet_free(p);
inspect(h);
```

Expected ownership graph:

```text
p owns Packet #1
   |
   +---- h borrows Header inside Packet #1
   |
packet_free(p)
   |
   X owner cannot die while h remains live
```

Expected diagnostic:

```text
CAND-T005 owner-destroyed-with-live-borrow
```

The diagnostic should point to owner creation, borrow creation, destruction, and the later borrow use.

Possible repair:

```c
Packet *p CAND_OWN = packet_new();
Header *h CAND_BORROW = packet_header(p);

inspect(h);
packet_free(p);
```

Again, C& may suggest the lifetime ordering but should not automatically move behavior-changing operations without approval.

## What can be fixed automatically?

C& divides remediation into classes.

### Safe automatic edits

`cand fix --safe` may eventually apply edits that preserve ordinary runtime semantics, for example:

```c
Packet *p = packet_new();
```

becoming:

```c
Packet *p CAND_OWN = packet_new();
```

or making an already-proven consuming call explicit:

```c
packet_send(p);
```

becoming:

```c
packet_send(CAND_MOVE(p));
```

when the trusted contract and dataflow already prove that the call consumes `p` and the edit merely records that existing fact.

### Review-required repairs

C& should preview but not silently apply changes such as:

- moving `free()` or cleanup ordering;
- deleting a destructor call;
- copying instead of borrowing;
- changing a function from consuming to borrowing;
- adding reference counting;
- changing callback retention;
- restructuring object ownership.

Those are program-design changes.

## The complete C& prevention loop

```text
existing C
    |
    v
cand scan / contract discovery
    |
    v
ownership + borrow model
    |
    v
cand check --level cand1 --strict
    |
    +---- PASS --------------------------+
    |                                    |
    |                                    v
    |                           ordinary clang/gcc build
    |                                    |
    |                                    v
    |                               normal C ABI
    |
    +---- FAIL
             |
             v
       stable diagnostic
       ownership/lifetime trace
       safe fix-it or review-required suggestion
             |
             v
       developer changes source
             |
             +----> cand check again
```

## How we prove the value

C& uses paired differential evidence:

```text
unsafe ordinary C
  -> GCC/Clang accept
  -> ASan demonstrates deterministic runtime defect where applicable

same defect under C& checked scope
  -> cand check must reject with stable diagnostic

corrected C
  -> cand check passes
  -> GCC/Clang pass
  -> ASan regression run passes
```

The repository already contains the first ordinary-C/ASan proof fixtures under `tests/proof/temporal/`. The `cand check` leg is intentionally documented as pending until the analyzer is implemented.

See:

- `docs/adr/ADR-0002-differential-safety-evidence.md`
- `docs/adr/ADR-0003-diagnostics-and-fixit-policy.md`
- `docs/adr/ADR-0004-annotation-noninterference.md`
- `tests/proof/README.md`
