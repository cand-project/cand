# SPEC-0001: Ownership and Borrowing Semantics

- **Status:** Draft
- **Version:** 0.1.0
- **Depends on:** ADR-0001

## 1. Purpose

This specification defines the semantic model for C& level **C&1**, the first enforceable safety level.

C&1 targets temporal ownership safety for modeled objects. It does not claim complete C memory safety.

## 2. Normative language

The terms MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are normative.

## 3. Definitions

### 3.1 Object

An **object** is a C object or dynamically allocated storage region tracked by the C& analysis.

Objects have an abstract identity independent of a particular pointer variable.

### 3.2 Owner

An **owner** is the unique tracked capability responsible for the lifetime of an object under the active ownership contract.

Owning capability is affine: it MUST NOT be consumed more than once.

### 3.3 Borrow

A **borrow** is a non-owning reference whose validity depends on a parent object/owner lifetime.

### 3.4 Shared borrow

A **shared borrow** permits observation but does not grant ownership or destruction rights.

### 3.5 Mutable borrow

A **mutable borrow** permits mutation while enforcing exclusivity rules defined below.

### 3.6 Move

A **move** transfers owning capability from one program location to another. The source is invalid for operations requiring ownership after the move.

### 3.7 Destruction

A **destruction** ends the modeled object's lifetime, e.g. `free(p)` or a contract-defined destructor.

### 3.8 Escape

A pointer **escapes** when it can outlive the current analysis scope through a return value, global/static storage, heap field, retained callback/context, thread, or external call whose contract permits retention.

### 3.9 Unsafe boundary

An **unsafe boundary** is an explicit boundary at which C& stops making the active proof guarantee and requires programmer/library contract responsibility.

## 4. Source vocabulary

C&1 defines the canonical conceptual vocabulary below. The implementation syntax is macros/annotations, not new C keywords.

| Concept | Canonical macro | Meaning |
|---|---|---|
| owner | `CAND_OWN` | declaration holds owning capability |
| shared borrow | `CAND_BORROW` | declaration is a non-mutating borrow |
| mutable borrow | `CAND_BORROW_MUT` | declaration is exclusive mutable borrow |
| consumes argument | `CAND_TAKES` | callee receives owning capability |
| owned return | `CAND_RETURNS_OWN` | caller receives owning capability |
| borrowed return | contract / `CAND_RETURNS_BORROW_FROM(n)` | result lifetime depends on argument `n` |
| free/destructor | contract / `CAND_FREES(n)` | call destroys object from argument `n` |
| explicit move | `CAND_MOVE(x)` | intent to transfer owning capability |
| safe boundary | `CAND_SAFE` | function is checked under active safety level |
| unsafe boundary | `CAND_UNSAFE` | body/API is outside proof guarantee |

The header may map these to Clang analysis annotations only when `CAND_ANALYSIS` is active and to no-ops for other compiler invocations.

## 5. Ownership states

For each tracked object capability, the analysis maintains a conservative abstract state.

Minimum states:

- `Owned`
- `BorrowedShared(n)`
- `BorrowedMutable`
- `Moved`
- `Dead`
- `EscapedUnsafe`
- `Unknown`

An implementation may use richer internal states but MUST preserve the externally visible rules.

## 6. Core rules

### R1 — single logical owner

A C&1 owned object MUST have at most one owning capability at a program point unless a contract explicitly introduces a reference-counted/shared-ownership model outside C&1's unique-owner rules.

Plain pointer aliases do not automatically duplicate ownership.

### R2 — move invalidates source ownership

After owning capability is moved from expression/location `a` to `b`, `a` MUST NOT subsequently destroy the object, transfer it again, or be used where an owner is required.

### R3 — destruction requires ownership

A destructor/free operation MUST consume an owning capability unless its contract explicitly specifies another ownership regime. Destroying through a borrow is an error.

### R4 — destruction is single-use

Once an object reaches `Dead`, any second destruction is an error.

### R5 — no use after death

Dereference, field access, indexed access, ownership transfer, or other object access after `Dead` is an error. Comparing/storing the numerical pointer value after death does not restore validity.

### R6 — borrow cannot outlive owner

Every borrow has an origin relationship to one or more backing objects. A borrow MUST NOT remain live beyond the proven lifetime of its backing object.

### R7 — owner cannot be destroyed with live borrow

If destroying/moving the owner would invalidate a live borrow, the operation is rejected unless the borrow is proven dead before that point or the move preserves the backing object's lifetime in a way understood by the analysis.

### R8 — mutable borrow exclusivity

While a mutable borrow is live, no overlapping mutable borrow or conflicting shared borrow/use may be live. C& MAY initially enforce this at whole-object granularity and later add field/region sensitivity.

### R9 — shared borrows may coexist

Multiple shared borrows of the same object MAY coexist while the owner remains live, subject to the active mutation rules.

### R10 — ownership must be resolved at checked exits

An owning capability in safe code MUST, on every checked function exit, be returned/transferred according to contract, destroyed, stored into a location whose contract takes ownership, or intentionally escaped through an explicit unsafe boundary. Otherwise C& reports a potential owner leak.

### R11 — owner overwrite is consumption-sensitive

Assigning a new owned object over a variable/location that still owns a live object is an error unless the previous ownership has been discharged.

### R12 — unmodelled ownership effects fail closed

A strict safe function MUST NOT pass a tracked owner/borrow to an external function that may retain/free/transfer it when the function has no usable C& contract. The call must be modelled, proven effect-free for ownership, or explicitly unsafe.

## 7. Inference rules

Annotations should be minimized where inference is unambiguous.

### 7.1 Allocation

A return value from a contract-defined allocator introduces an `Owned` capability. The explicit local annotation MAY be omitted if inference can prove the variable receives the allocator's owning return and no ambiguity is created.

### 7.2 Destructor

A contract-defined destructor consumes the owner and transitions the object to `Dead`.

### 7.3 Plain assignment

For tracked owning pointers, plain C assignment is ambiguous because C itself defines bitwise pointer copying rather than ownership semantics. C& SHALL choose borrow or move only when proven by context. Where inference cannot distinguish them soundly, strict code requires an explicit `CAND_MOVE` or borrow declaration.

### 7.4 Address-of and interior pointers

Pointers derived from an owner through address-of, field access, or pointer arithmetic are borrows by default unless a contract explicitly creates new ownership. Their lifetime is bounded by the backing object and any narrower known subobject lifetime.

## 8. Function contracts

Each function summary can describe call-scoped borrows, consumed/destroyed/retained parameters, owned or borrowed returns, out-parameter ownership, conditional effects, and ownership-pure behavior.

## 9. Interprocedural analysis

C&1 MUST analyze ownership effects across function boundaries using summaries/contracts. Project-local summaries SHOULD be inferred and cached. External/library summaries come from validated contract bundles or trusted built-ins. Recursive call graphs MAY require fixed-point computation; non-convergence in strict code fails closed.

## 10. Control flow

At branches, loops, early returns and `goto`, C& computes conservative ownership joins/fixed points. A value that may be dead/moved on one predecessor cannot be considered safely owned after the join.

## 11. Aggregates

Owned struct fields create ownership obligations. Moving a struct containing owners transfers those capabilities consistently. Arrays of owners are collections of independent capabilities unless a collection contract exists. Unions with tracked pointer variants require discriminator knowledge or an explicit contract; otherwise strict safe use fails closed.

## 12. Globals and statics

Global ownership requires explicit contract/configuration because initialization order, aliasing and concurrency complicate proof. Initial C&1 MAY reject or heavily constrain mutable borrowed globals.

## 13. Callbacks and retained context

Function-pointer APIs require contracts indicating whether pointer/context arguments are call-scoped borrows, retained borrows with a release event, or ownership transfers. Unknown retention behavior is unsafe for a tracked pointer.

## 14. Threads

C&1 does not claim data-race freedom. Unique ownership may be transferred into a thread when the thread API contract is explicit; shared cross-thread borrows remain outside C&1 unless a later concurrency level defines them.

## 15. `realloc`

`realloc` is path-sensitive and MUST NOT be modeled as a simple free+alloc. Success transfers ownership to the returned object; failure with nonzero size preserves the original owner; size-zero behavior is runtime-profile specific.

## 16. Casts and pointer arithmetic

Casts do not create ownership. Pointer arithmetic derives interior borrows from a backing object. Integer-to-pointer and provenance-destroying casts cannot automatically inherit safe ownership; strict code requires a supported intrinsic/contract or unsafe boundary.

## 17. `setjmp`/`longjmp`, signals, inline assembly

These features can bypass ordinary lexical/control-flow assumptions. Initial C&1 classifies them as unsupported in strict safe functions unless an explicit supported model exists. Unsupported constructs must produce diagnostics, not silent success.

## 18. Unsafe semantics

`CAND_UNSAFE` means the active proof property is not claimed inside the boundary. Calls crossing from safe to unsafe still require a safe-side ownership summary. Unsafe is not equivalent to “ignore analysis.”

## 19. Diagnostic requirements

Diagnostics MUST include a stable ID, primary source location, tracked object/capability where known, violating operation, ownership/borrow origin where known, relevant path/call context where practical, and safety level.

## 20. Soundness and false positives

C& prioritizes sound safety claims over accepting every valid C program. When C& cannot prove a required property in strict safe code, rejection or explicit unsafe is preferable to unsound success. Release gates must measure both unsafe cases rejected and known-safe cases accepted/annotation burden.

## 21. C&1 acceptance suite

C&1 may be declared implemented only when the conformance suite proves allocator-owner inference, destruction/death, use-after-free, double-free, use-after-move, leak-at-exit, borrow-outlives-owner, free-through-borrow, branch/loop path sensitivity, function summaries, owned aggregate fields, callback retention, `realloc` path semantics, unknown-external fail-closed behavior, explicit unsafe boundaries, and unchanged compilation by unmodified supported C compilers.
