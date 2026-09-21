# C&1 PASS-Path Audit — Structural Soundness Argument (Phase A)

Status: completed audit at `283a80e7391ba2da47f8740aa1705aacdcf70507`.
Method source: [C&1 Proof Plan](CAND1-PROOF-PLAN.md), Phase A.
Invariant: **confirmed temporal violation + C&1 PASS = 0.**

This document is the structural half of the C1 soundness obligation: an
exhaustive, line-referenced inventory of every code path that can emit a
`pass` verdict, and the gate each path enforces. Empirical corpora (differential
fuzzing, CVE replay) bound the false-PASS rate from above; this audit is what
makes "fail-closed by construction" reviewable instead of asserted.

A CI drift-guard (`scripts/pass-path-guard.sh`, wired into
`scripts/check.sh`) fails the baseline check when the inventory and the source
drift, so any new or modified PASS-emitting path forces a re-audit.

## 1. The diagnostic funnel

Every diagnostic that can block a PASS flows through exactly two collector
containers, and the semantic verdict is computed only from them:

```
emitFinding()            (7 call sites)  -> Collector::addFinding  -> findings_list_
emitUnsupported()
markUnsupported()
markUnsupportedAt()
noteUnknownPointerCall() (73 call sites) -> Collector::addUnsupported -> unsupported_
noteFrontendError()                       -> frontend_error_
noteContractError()                       -> contract_error_
noteUnsupportedBorrow()                   -> unsupported_borrows_
```

Adding finding/obligation emitters can only make PASS harder to obtain
(soundness-monotone). The audit therefore pins the PASS-emitting sites, not
the diagnostic emitters.

## 2. PASS-emitting sites (exhaustive inventory)

There are exactly **7 occurrences of the `"pass"` string literal** in
`src/cand.cpp`, forming **4 emitting sites and 1 consumer-side gate**:

### Site 1 — the semantic verdict (sole plain-check PASS path)

```cpp
root["result"] =
    hasFindings() ? "fail" : (hasUnsupported() ? "incomplete" : "pass");
```

**Gate:** `findings_list_` is empty AND `unsupported_` is empty. This is the
only site that can produce a `pass` for ordinary `cand check`. Findings are
emitted by the temporal-violation detectors (use-after-destruction,
double-destroy, borrow-lifetime violations, …); obligations by every
unmodelled-construct path (unknown calls, escapes, variadic positions,
unresolved storages, transport boundaries, …). A verdict of `pass` therefore
asserts: *no ownership/lifetime rule was violated in checked scope, and every
construct the verifier could not decide was reported as an obligation.*

### Site 2 — agent-mode cand1 PASS

```cpp
const bool cand1_pass = cand1 && canEmitCand1Pass(collector, agent_state, evidence_bound);
const char *final_result = policy_fail ? "fail-policy" :
    (review ? "review-required" :
     (cand1 ? (cand1_pass ? "pass" :
               (semantic == "fail" ? "fail" : "incomplete"))
             : semantic.data()));
```

**Gate — `canEmitCand1Pass` (all must hold):**
1. no findings, no unsupported obligations, no frontend error, no contract
   error, zero unsupported transport;
2. evidence-bound: non-empty source inputs and frontend arguments, a pinned
   executable SHA-256, and a known verifier source commit;
3. no policy failure, no review required, no proof-policy weakening, no
   review-requiring delta;
4. toolchain supported;
5. profile is `generated` and safety level is `cand1`;
6. policy scope files and policy SHA-256 are present;
7. every contract input is trusted (`builtin`, `verified`, or `reviewed`);
8. zero unsafe boundaries and zero suppressions.

This is strictly stronger than Site 1: it additionally binds the PASS to
replayable input evidence and to an unweakened, toolchain-matched policy.

### Site 3 — the `policy_result` evidence field (two occurrences)

```cpp
evidence["policy_result"] = state.policy_failed || state.delta.weakened ? "fail" :
    (state.review_required || state.delta.review_required ? "review_required" : "pass");
```

**Gate:** no policy failure, no delta weakening, no review requirement (own
or delta). This is a *component field* inside the agent evidence and the
agent-check JSON, not a standalone verdict; the standalone verdict is Site 2,
which requires `policy_result == "pass"` transitively via the same state
flags.

### Site 4 — attestation consumer gate (rejector, not emitter)

```cpp
if (result->str() == "pass" &&
    (semantic_result->str() != "pass" || policy_result->str() != "pass" || *weakened)) {
```

**Gate:** a submitted attestation claiming `pass` is rejected unless the
semantic result is `pass`, the policy result is `pass`, and the proof policy
was not weakened. This site *consumes and rejects*; it cannot emit a PASS.

## 3. Fail-closed exits that can never produce PASS

- **Translation-unit compile error** → `exit 2`, no verdict emitted at all.
- **Internal serialization/evidence errors** → `exit 2`.
- **Non-agent cand1 mode** → the JSON `result` is coerced to
  `fail`/`incomplete` and `cand1_acceptance` explains that PASS requires
  trusted generated evidence and policy bindings; the process exits 1 or 3.
  A `pass` verdict is structurally unreachable without the agent evidence
  binding of Site 2.
- **Policy failure / review required (agent mode)** → `fail-policy` /
  `review-required` verdicts and exit 4 — never `pass`.

## 4. Conclusion

Every route to a `pass` verdict passes through one of the inventoried sites,
and each site's gate reduces to: *zero findings, zero unresolved obligations,
plus (for the agent/cand1 forms) evidence binding and unweakened policy.* No
site can emit `pass` while a diagnostic of any kind is outstanding. The
structural claim of ADR-0010 — no unresolved ownership operation may be
silently accepted — is therefore enforced at every PASS-emitting site, and
the drift-guard below keeps this inventory true as the code evolves.

## 5. Drift-guard contract

`scripts/pass-path-guard.sh` asserts, against `src/cand.cpp`:

| Assertion | Baseline |
|---|---|
| total `"pass"` literal occurrences | 7 |
| exact semantic-verdict ternary present (Site 1) | 1 |
| `canEmitCand1Pass` definition present with all 8 gate conditions | 1 |
| attestation rejector condition present (Site 4) | 1 |
| non-agent cand1 coercion (`requires trusted generated evidence`) present | 1 |

Any change to a PASS-emitting path in `src/cand.cpp` fails
`scripts/check.sh` until this document and the guard baseline are updated in
the same change set — forcing a re-audit as part of the review.
