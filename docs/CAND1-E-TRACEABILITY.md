# C&1-E Executable Traceability

This is the executable companion to the requirement matrix in
`CAND1-E-QUALIFICATION-PLAN.md`. A row is not PASS because a nearby test exists;
the named positive, negative, unsupported, adversarial, and evidence checks were
run and recorded at the final exact head.

Qualification identities:

- Reviewed qualification HEAD: `7a6f4b65fb6e7506d073f9c93aa615c0e6bf8860`.
- Merge/release commit: `3a2672b6b6a6742c8ac19c2894a698cdd1970b7a`.
- Confirmed temporal false PASS count: `0`.
- Issue #25 remains a conservative mutable-borrow precision limitation only.

| Rule | Implementation path | Positive | Negative | Unsupported boundary | Adversarial | Evidence/result | Status |
|---|---|---|---|---|---|---|---|
| unique ownership | `FlowAnalyzer` ownership state | `tests/p1/run.sh` | temporal corpus | `tests/failclosed` | E ownership reshapes | findings/state trace | PASS (qualified subset) |
| move semantics | move transfer in `FlowAnalyzer` | P1 move-safe | move-after-use | move-unsupported storage | E move mutations | ownership transitions | PASS (qualified subset) |
| use-after-move | ownership access checks | safe move fixtures | temporal move fixtures | unknown move target | E move/use variants | `CAND-T*` | PASS (qualified subset) |
| destruction | destroy transfer handling | safe destroy | free/destroy regressions | untracked destroy | E destroy variants | destruction trace | PASS (qualified subset) |
| double destruction | generation/destroy checks | — | double-free corpus | uncertain target | E repeated destroy | `CAND-T003` | PASS (qualified subset) |
| object lifetime | object state lattice | lifetime-safe | UAF corpus | maybe-dead object | E lifetime variants | object trace | PASS (qualified subset) |
| shared borrow lifetime | `createBorrow`, `checkBorrowAccess` | P2 shared | parent-death | unknown parent | E borrow variants | borrow analysis | PASS (qualified subset) |
| mutable borrow exclusivity | `createBorrow`, `checkMutableOwnerAccess` | last-use controls | `mutable_conflict.c` | unknown storage | E overlap/last-use | `CAND-B004` | KNOWN LIMITATION (#25) |
| owner/borrow invalidation | `invalidateBorrows` | invalidation-safe | live-borrow destruction | unsupported invalidation | E alias invalidation | invalidated borrows | PASS (qualified subset) |
| CFG joins | flow-state join/fixpoint | `tests/cfg/run.sh` | branch/join | unknown CFG state | E reshaped CFG | state traces | PASS (qualified subset) |
| loops | loop analysis/liveness | loop-safe | loop-after-death | loop widening | E nested loops | completeness | PASS (qualified subset) |
| heap generations | allocation-site generation state | `tests/cand1/heap` | stale aliases | widened generation | E generation reuse | heap coverage | PASS (qualified subset) |
| aliases | storage identity graph | storage-safe | storage UAF | unresolved alias | E alias reshapes | storage IDs | PASS (qualified subset) |
| structs | member storage resolution | struct-safe | member UAF | unsupported member | E nested structs | transport findings | PASS (qualified subset) |
| arrays | array storage/indices | multidim safe | array UAF | dynamic index | E index reshapes | unsupported/result | PASS (qualified subset) |
| aggregate transport | aggregate transfer logic | aggregate controls | aggregate UAF | unsupported aggregate | E aggregate mutation | transport rule set | PASS (qualified subset) |
| unions | union classification | declared controls | union lifetime | `union_member_incomplete.c` | E active-member | INCOMPLETE | UNSUPPORTED -> INCOMPLETE |
| memcpy/memmove | byte transport handlers | modeled copies | stale copy | unsupported byte shape | E copy mutation | transport result | UNSUPPORTED -> INCOMPLETE |
| pointer mutation | pointer assignment/advance | pointer-safe | stale pointer | storage incomplete | E expression reshapes | storage/result | PASS (qualified subset) |
| pointer/integer transport | provenance boundary | documented controls | provenance defect | provenance incomplete | E cast round trips | INCOMPLETE | UNSUPPORTED -> INCOMPLETE |
| globals/statics | global/static handlers | global-safe | global UAF | global escape | E global variants | storage result | PASS (qualified subset) |
| out parameters | parameter transport/summaries | interprocedural safe | out-param UAF | unresolved out storage | E parameter reshapes | summary/evidence | UNSUPPORTED -> INCOMPLETE |
| pointer-to-pointer | nested storage bindings | nested controls | nested stale pointer | unsupported depth | E nested aliases | transport result | PASS (qualified subset) |
| function arguments | call transfer summaries | safe calls | argument defects | unknown call | E wrapper args | summary trace | PASS (qualified subset) |
| function returns | return-effect handling | safe returns | invalid return | unresolved return | E return reshapes | finding/result | PASS (qualified subset) |
| interprocedural summaries | summary collector/remapping | `tests/interprocedural` | wrapper defects | recursion/unknown | E wrapper chains | summary fields | PASS (qualified subset) |
| trusted contracts | `agent_policy.cpp` validation | reviewed contracts | self-authored | invalid contract | E substitution/digest | contracts array | PASS (qualified subset) |
| unknown external calls | unknown-call retention | modeled controls | retention defects | unknown call | E external variants | INCOMPLETE/policy | UNSUPPORTED -> INCOMPLETE |
| indirect calls | indirect-call classification | supported controls | invalid effects | unresolved target | E function pointers | result/policy | UNSUPPORTED -> INCOMPLETE |
| callbacks/retention | callback/escape tracking | modeled callbacks | retained borrow | unsupported callback | E callback variants | borrow/ownership | UNSUPPORTED -> INCOMPLETE |
| realloc | realloc transport boundary | documented controls | realloc borrow | `realloc_borrow.c` | E realloc mutations | INCOMPLETE | UNSUPPORTED -> INCOMPLETE |
| cross-TU | one-TU boundary/contracts | contract control | cross-TU defect | uncontracted cross-TU | E9 corpus | scope/evidence | UNSUPPORTED -> INCOMPLETE |
| atomics | explicit boundary | no authoritative PASS | atomic defect | `atomic_pointer_incomplete.c` | E transport variants | INCOMPLETE | UNSUPPORTED -> INCOMPLETE |
| varargs | explicit boundary | no authoritative PASS | vararg defect | policy/tool error | E vararg variants | result | UNSUPPORTED -> INCOMPLETE |
| setjmp/longjmp | explicit boundary | no authoritative PASS | nonlocal defect | `setjmp_nonlocal_incomplete.c` | E control variants | INCOMPLETE | UNSUPPORTED -> INCOMPLETE |
| compiler extensions | C11/no-extension gate | strict C11 | extension input | frontend policy | E flag mutation | toolchain/evidence | UNSUPPORTED -> INCOMPLETE |
| unsupported accounting | `Collector` + PASS predicate | zero unsupported | unsupported fixtures | all unsupported forms | E coverage mutations | completeness fields | PASS (qualified subset) |
| proof-policy authority | policy loading/diff | zero-budget policy | weakened policy | scope/level change | E policy attacks | policy delta | PASS (qualified subset) |
| evidence integrity | evidence build/replay | valid replay | digest/source drift | stale profile | E field mutations | integrity/status | PASS (qualified subset) |
| toolchain qualification | CMake + toolchain validation | D matrix | drift attacks | unsupported profile | E25 matrix | toolchain identity | PASS (qualified subset) |

## Central PASS predicate checklist

`canEmitCand1Pass` must reject findings, unsupported operations, frontend or
contract errors, transport uncertainty, unbound evidence, policy failure,
review-required changes, unsupported toolchains, non-generated/cand1 policy,
empty scope/digest, untrusted contracts, unsafe boundaries, and suppressions.
The E campaign records each predicate as an independent negative case.


## Final candidate evidence

- Technical qualification precursor: `e929372c18a55ab364e2089a55d230222b0c8bc8`.
- Final claim candidate: one documentation-only commit after this precursor.
- Independent adversarial corpus: 128 cases (96 negative, 32 controls), zero negative false PASS.
- Conformance: 120 fixtures; fuzz: seeds 303 and 404, 10,000 cases each, zero false PASS.
- Sanitizer differential: 40/40 confirmed temporal defects rejected; fuzz sanitizer violations rejected.
- Cross-TU ownership effects without reviewed contracts: `UNSUPPORTED -> INCOMPLETE`.
- Exact clean-build verifier SHA: `df46a102dcb302ea8c381f1cf40cdb56d18c0f7a6a5ee016decb9764c20667f`; two builds identical.
- #25 remains a demonstrated conservative precision limitation only.
- Final claim activation requires exact-head CI and one fresh approval; no issue is closed by this document-only change.
