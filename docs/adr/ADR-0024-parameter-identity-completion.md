# ADR-0024 — Parameter-Identity Completion (Unknown-Capability Parameters)

Status: accepted (2026-09-21, PR #51)

## Context

ADR-0023 established the parameter-lifetime model for pointer parameters whose
body-derived summary classifies them `Borrow`, `TakeOwnership`, or `Destroy`.
The remaining class — `ParamEffect::Unknown` (and `ParamEffect::None`) — was
explicitly left outside the entry-state model: `seedParameterState()` emitted no
`StorageBinding` for such parameters.

That gap produced two opposite defects on ordinary, unannotated C:

1. **Under-reporting (a trustworthy-PASS violation).** An unbound parameter is
   invisible to `containsTrackedStorage()`, so passing it to an unknown external
   or indirect call emits *no* obligation. A function whose pointer parameter
   only escapes to an opaque callee received authoritative PASS with zero
   obligations, violating the README PASS contract and ADR-0010. The identical
   shape with a local pointer correctly yields
   `unknown-call-with-tracked-pointer` → INCOMPLETE (measured: `g1_only.c`
   reproduced on qualified v0.2.0).

2. **Over-reporting (adoption blocker).** With no binding, every member/deref/
   subscript through the parameter hit `checkAccess`'s null-binding path and
   emitted `unmodelled-pointer-parameter` — even for reads lexically before the
   operation that created the uncertainty (function-granularity poisoning). This
   was the single largest obligation class on the Hiredis pilot: 674 of 1,247
   obligations across 90 of 147 blocked functions.

Both defects share one root cause: a parameter that is live at function entry
was left outside the object model entirely.

## Decision

Complete the parameter-entry model: seed every pointer parameter — regardless of
summary effect — as a tracked object that is **live at function entry** with
**no ownership authority** (`ParameterCapability::Unknown`):

- `ParamEffect::Unknown` and `ParamEffect::None` are assigned
  `ParameterCapability::Unknown` instead of being skipped.
- The seeded object enters with `ObjectState::Owned`, `ObjectOrigin::Parameter`,
  alias (non-owner) relation — the same shape ADR-0023 gives `Borrow`/`Destroy`
  for liveness, minus any destructive or transfer authority.
- `ParamEffect::None` parameters are never referenced by the verified body, so
  seeding them is behaviorally a no-op; it is kept for uniformity.

Liveness (whether the object is alive at the current program point) is the only
fact reads depend on, and it is decidable from the entry state plus observed
destroys. Authority (who may destroy or transfer) is a separate fact and stays
explicitly unavailable for `Unknown`, so every ownership-transfer operation
remains fail-closed via the existing capability checks.

## Resulting semantics

- **Reads/writes through the parameter are decidable** while the object is live;
  conditional/loop destruction joins to `MaybeDead` and a following use is a
  `possible` use-after-destruction FAIL — exactly the qualified local-variable
  semantics (cf. `tests/cfg/branch_null_or_free_possible_uaf.c`).
- **An escape to an unknown or indirect callee now emits
  `unknown-call-with-tracked-pointer`** at the call site (parity with locals),
  closing the ADR-0010 gap. That obligation is the soundness anchor and must
  never be made droppable.
- **Ownership transfer remains fail-closed:** an attempt to move an
  `Unknown`-capability parameter to an opaque/indirect consuming sink cannot be
  refined to a body `TakeOwnership`, so the capability stays `Unknown` and the
  tracked pointer arriving at the sink emits
  `unknown-call-with-tracked-pointer:indirect` (INCOMPLETE) — never a silent
  transfer. A transfer to a *known* consuming callee is correctly reclassified
  `TakeOwnership` by summary inference. `free(p)` is modeled through the normal
  state machine (so free-then-read and double-free become definite FAILs,
  strictly stronger than the previous `free-untracked-pointer` INCOMPLETE).
- **Allocation escapes into parameter-reachable storage keep their dedicated
  obligations** (`allocation-to-untracked-storage:*`, `free-untracked-pointer`).
- **Caller-side summaries are unchanged:** an `Unknown` param effect still makes
  a same-TU caller INCOMPLETE when passing a tracked pointer; cross-TU callers
  remain fail-closed at the boundary.

## Qualification-visible effects (reviewed, directionally stronger)

Conditional and loop destruction of a parameter followed by a use — previously
INCOMPLETE because the parameter was unbound — now produce a `possible`
use-after-destruction FAIL:

- `parameter_owner_conditional_destroy_fail.c` and
  `parameter_owner_loop_destroy_fail.c`: INCOMPLETE → FAIL.
- incident matrix `parameter_lifetime_redteam.c` CASE_N / CASE_O:
  INCOMPLETE → FAIL.

Both directions remain "never PASS"; the change strictly strengthens rejection.
Three Hiredis `CAND-B003` boundary detections (undeclared borrow-return on
`redisAsyncInitialize`, `redisCommand`, `sdscatvprintf`) became visible because
the parameters involved are now tracked; each is a genuine return-ownership
boundary question. (A fourth, `seekNewline` in `read.c`, surfaces when the
reviewed libc borrow bundle is also active.)

## Companion soundness fix: variadic argument positions (false-PASS incident)

Qualification review of this change set found a **false PASS** in the
direct-call summary path: the parameter-effect loop stopped at
`summary->params.size()`, so arguments at positions beyond the callee's
modelled parameter list — variadic slots, e.g. `snprintf(buf, n, "%s", p)` —
were never examined. A tracked pointer passed at such a position produced no
escape obligation and no liveness check, even when the pointee had already
been freed.

- **Reproduction (definite)**: with the reviewed libc borrow bundle active,
  `free(p); snprintf(b, 16, "%s", p);` returned PASS with zero obligations
  while ASan confirms a heap-use-after-free read of `p` inside `snprintf`.
- **Pre-existing**: the hole reproduces on the qualified v0.2.0 baseline with
  a local variadic function and no contracts at all
  (`static void sink(const char *fmt, ...)` called as `sink("%s", p)` after
  `free(p)` → PASS). The bundle's `snprintf` entry extended the exposure from
  local variadic callees to contract-modelled external ones; it did not create
  the hole. The summary-scan side was already fail-closed (it iterates all
  arguments and defaults unmodelled positions to `ParamEffect::Unknown`).
- **Fix**: the direct-call summary path now reports the same
  `unknown-call-with-tracked-pointer` (and, for borrows,
  `unknown-call-borrow-retention`) obligations the unknown-call path reports
  for tracked storage at unmodelled positions, so these positions can never
  yield a trustworthy PASS. Benign variadic arguments without tracked storage
  (e.g. `snprintf(b, 16, "%d", 42)`) remain decidable and PASS.
- **Pinned by** `tests/interprocedural/variadic_argument_escape.c` (ESCAPE /
  DEAD / SNPRINTF, all INCOMPLETE, never PASS).
- **Real-code validation**: on the Hiredis pilot corpus the fix surfaces
  exactly one new obligation — `snprintf(buf, sizeof(buf), "%s: ", prefix)` in
  `__redisSetErrorFromErrno` (`net.c:108`), where `prefix` is an
  Unknown-capability parameter read by `snprintf` at a variadic position.
  Before this fix that read carried no obligation at all.

## Compatibility and scope

- No schema, annotation vocabulary, profile, or contract format change.
- No new trust decision; the change only adds obligations/findings and removes
  spurious read obligations. PASS becomes strictly harder to obtain.
- Cross-TU summaries, pointer-output ownership, realloc semantics, callback
  retention, and aggregate/field aliasing remain out of scope (per ADR-0023 and
  SPEC-0003), unchanged by this decision.
- The public C&1/v1 claim remains suspended/qualified per the exact-head
  qualification and independent review policy. This ADR's change requires the
  full qualification re-run (19/19 suites on the qualified toolchain) and the
  incident-matrix addendum for the strengthened CASE_N / CASE_O verdicts.

### Known pre-existing limitation (not introduced by, nor fixed in, this ADR)

The summary scan does not trace initialization aliases through `free`: in
`void run(int *p){ int *q = p; free(q); }` the `int *q = p` initializer classifies
`p` as `ParamEffect::Borrow`, and `free(q)` (through a local the scan does not map
back to `p`) then trips `destroy-destroy-parameter` ("borrowed parameter cannot be
destroyed"), a false POSITIVE on otherwise-valid single-free code. The identical
behavior reproduces on the qualified v0.2.0 baseline, so this is pre-existing and
independent of this change; it is a decidability gap rather than a false PASS. It
is deliberately out of scope here (fixing it requires alias-tracing in the
summary scan, a separate change) and is tracked as a follow-up. The
`CASE_UNKNOWN_ALIAS_FREE_READ` fixture intentionally prefixes an opaque escape
(`external(p)`) so the parameter stays genuinely `Unknown`, keeping the red-team
case on this change's semantics instead of exercising that pre-existing
limitation.
