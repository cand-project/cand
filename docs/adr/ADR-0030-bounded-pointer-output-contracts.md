# ADR-0030 — Bounded `produces_out_owner` Pointer-Output Contracts (Issue #41)

Status: accepted (2026-09-24, milestone #41)

## Context

Issue #41: the single largest remaining `INCOMPLETE` cause class at the
external boundary is the out-parameter (`T **`) transport row
(`unknown-call-with-pointer-output`; SPEC-0007 lists it UNSUPPORTED in v1).
The Gate A census (`docs/pilots/POINTER-OUTPUT-PARETO.md`) measured the
population across the five pilots: 15,219 pointer-write rows, of which
1,687 are true out-parameter rows and 948 have the bounded rule's
addressable shape (`&local` destination, direct callee, non-variadic).

The issue text constrains any design: a contract may claim an out-owner
produce only where **destination storage, success/failure behavior,
nullability, and destruction responsibility are all identifiable** from
the review; unknown or conditional output effects stay `INCOMPLETE`;
there is no generic pointer-to-pointer rule; no allocator family or
ownership fact is ever inferred from a name. A semantic-scope expansion
of this size is a new qualified profile version, not a C&1/v1 patch.

The authority constraint from ADR-0029 and `docs/SAFETY_CLAIMS.md`
applies unchanged: a candidate author must not be able to make its own
code pass by asserting facts. The pointer-output rule set is therefore
a **measurement profile** that can never emit a C&1 PASS.

## Decision

### 1. Contract vocabulary

`produces_out_owner` gains a mandatory `output:` block:

```yaml
- symbol: po_status
  kind: function
  params:
    - index: 0
      effect: produces_out_owner
      output:
        write: always | on_success
        success: zero | nonzero      # required iff write: on_success
        nullable: true | false
```

The schema (`contracts/schema/cand-api-contract.schema.json`) requires
`write` and `nullable`; `success` is mandatory exactly when
`write: on_success`. At most one `produces_out_owner` parameter per
symbol. A `nullable` key at the legacy indent (a v1 returns-block
position) on a produces symbol is a load-time error, not a silent
default. `T ***`, function-pointer, and array shapes for the out slot
are load-time errors. A symbol with no visible translation-unit
declaration is dropped (fail-closed); a symbol whose body is visible in
the checked translation unit is dropped for that unit (the
body-verified summary path owns that unit). Variadic callees and
realloc-named callees are refused at call time.

### 2. Profile identity and PASS authority

With the feature on, the report carries `profile: cand1/v1.1-draft`,
`coverage.transport_rule_set: cand1-pointer-transport-v2`, and a root
`"pointer_output_contracts": true` field. `canEmitCand1Pass` returns
false for every feature run: **no feature-enabled run, agent or
non-agent, can emit an authoritative C&1 PASS.** The non-agent cand1
result word is only `fail | incomplete`. This keeps the expansion
strictly on the measurement side of the C&1/v1 claim boundary
(SPEC-0010).

### 3. Authority paths

- CLI modifier `--pointer-output-contracts`: non-authoritative,
  `--level=cand1` only (exit 2 usage error otherwise, emitted before
  any policy load so it can never pollute `policy_failed` accounting).
- Policy `features.pointer_output_contracts: true`: the sole rule-set
  authority in agent mode. Any change to the feature against the
  reviewed base policy is `REVIEW_REQUIRED` in `comparePolicies`
  (a rule-set change). Enabling the modifier while the policy lacks
  the feature is a `fail-policy` condition — never a silent downgrade;
  the policy feature alone enables the rule set — never a silent
  upgrade. Plain `cand check` never loads a policy.

### 4. Acceptance predicate (call site)

A produces call is accepted only when all of the following hold; every
failure is a refusal that falls through to the ordinary unknown-call
path, so refused rows are byte-identical to a v1 run:

1. direct, non-variadic callee whose name is not a realloc form;
2. the out-slot argument is `&local` for a function-local pointer
   variable, and the destination address is not taken anywhere else in
   the function (caller-side alias-join hazard);
3. the call is not lexically inside a loop (syntactic pre-pass; see
   §7);
4. the destination is not also the call's result slot
   (`dest = f(&dest)`);
5. destination pre-state: absent, `Null`, `Moved`, or `MaybeMoved` for
   `write: always`; exactly `Null` for `write: on_success`
   (an uninitialized destination is refused for `on_success` because
   the failure edge would read an unrepresentable slot value).

### 5. Produce-state semantics

An accepted produce binds the destination through the allocation-site
machinery with a `produced-maybe` join bit:

- `write: always` + `nullable: false` → `{fresh, Owner, unmarked}` —
  immediately usable;
- `write: always` + `nullable: true` → `{fresh, MaybeNull, marked}`;
- `write: on_success` (either polarity) → `{fresh, MaybeNull, marked}`.

Every marked binding used before a recognized refinement emits the
fail-closed `unrefined-out-owner-use` obligation; the use is never
silently accepted and never a null-safety `FAIL`.

### 6. Guard refinement

Pending guards are recorded only for `on_success` produces whose result
flows into a function-local, non-escaped variable (C2/C3). Four
single-form caller guards refine the success and failure edges of
`if`/`while`/`for` terminators:

- **C1** guard on the call itself (`if (f(&out) == 0)`);
- **C2** embedded assignment (`if ((r = f(&out)) == 0)`);
- **C3** stored-then-tested result;
- **C4** destination guard (`if (out != NULL)`), which recognizes
  truthiness, `== NULL`, `!= NULL`, and `!= 0`, and applies only to
  marked bindings.

Success edge → `{fresh, Owner, unmarked}`; failure edge →
`{kNullObjectId, Null, marked}`. `==K` edge ⊆ success iff
(zero ∧ K==0) ∨ (nonzero ∧ K≠0); `≠K` edge ⊆ success iff
(nonzero ∧ K==0); `≠K` edge ⊆ failure iff (zero ∧ K==0); truthiness is
the K=0 `!=` form with negation parity. Relational guards (`<`, `>`),
compound conditions (`&&`/`||`), explicit casts, and constant-on-left
comparisons are unrecognized — the binding keeps its mark and the use
stays `unrefined-out-owner-use`.

Kill rules: (a) any write to the guarded result or destination (with
one exemption, §8); (b) a call whose arguments mention the destination,
sparing only an accepted produce's own out-slot; (c) loop terminators
clear pending guards from both out-edges after per-edge refinement.

### 7. Loop refusal is syntactic

A produces call lexically inside a loop is refused outright. A
state-based acceptance (live-destination pre-state) is non-monotone
under worklist iteration — a back edge can join a produced state into
the loop header after the call was accepted on an earlier pass — so
the syntactic pre-pass is the only stable rule. Residual hazard: in an
acyclic region an accept-then-refuse flip remains possible in exotic
join orders; the refusal direction is fail-closed (the v1 row
reappears), never a false obligation removal.

### 8. C2 CFG-split kill exemption

Clang's CFG materializes `if ((r = f(&out)) == 0)` as two elements —
the call, then the assignment — with the call processed first. The
assignment's kill would otherwise destroy the pending guard its own
RHS call just recorded. Each pending entry therefore carries the
recording `CallExpr *`, and the kill spares exactly that entry; a
plain reassignment `r = f2(...)` still kills `f1`'s stale entry.

### 9. Short-circuit chains and the defensive terminator pass

For `if (A || B)`, Clang materializes each operand's side effects as
elements of the operand's own block but gives every chain block the
full condition as its terminator. The defensive terminator pass must
not process a later block's element call with this block's state: the
element pass in the call's own block would then see its destination as
live and refuse (a spurious refusal, observed in real pilots at
`blame_git.c:442` and `attr_file.c:172-173`). Element calls are
pre-marked processed for the defensive pass; with the feature off the
set is empty and the pass is unchanged. The fixture
`tests/interprocedural/pointer_output_short_circuit.c` pins this.

## Consequences

- With the feature off, the analyzer is bit-identical to v1: the full
  qualification battery re-ran the five pilots byte-identically against
  the pre-#41 baselines.
- ABI control follows the #39 convention
  (`docs/pilots/DECL-ANNOTATION-PROPAGATION.md` §8): the pristine-vs-
  patched tree comparison reduces here to a zero-modification claim —
  #41 changes only the analyzer and its schemas and adds external
  contract fixtures; no pilot source file is patched, so struct sizes
  and exported symbols are unchanged by construction.
- The v2 pilot measurement (reviewed bundle: 8 symbols across 4
  pilots; `docs/pilots/POINTER-OUTPUT-PARETO.md` §"v2 measurement")
  converted 75 of 101 addressable bundle rows; every unconverted row
  is attributed to a refusal predicate or a review refusal (cursor
  advancers, second inexpressible out-params, backend-dispatch writes).
  Findings are unchanged in every pilot.
- The rule set ships as `cand1/v1.1-draft` behind the modifier and the
  policy feature; promoting it into a PASS authority requires a new
  ADR and a full requalification of the C&1 claim.
- The evidence and check schemas accept
  `cand1-pointer-transport-v2` alongside v1 (widened enum), so a
  feature-run evidence document validates and replays.
