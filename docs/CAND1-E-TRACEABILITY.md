# C&1-E Executable Traceability

This is the executable companion to the requirement matrix in
`CAND1-E-QUALIFICATION-PLAN.md`. A row is not PASS because a nearby test exists;
the named positive, negative, unsupported, adversarial, and evidence checks must
be run and recorded at the final exact head.

| Rule | Implementation path | Positive | Negative | Unsupported boundary | Adversarial | Evidence/result | Status |
|---|---|---|---|---|---|---|---|
| unique ownership | `FlowAnalyzer` ownership state | `tests/p1/run.sh` | temporal corpus | `tests/failclosed` | E ownership reshapes | findings/state trace | PENDING |
| move semantics | move transfer in `FlowAnalyzer` | P1 move-safe | move-after-use | move-unsupported storage | E move mutations | ownership transitions | PENDING |
| use-after-move | ownership access checks | safe move fixtures | temporal move fixtures | unknown move target | E move/use variants | `CAND-T*` | PENDING |
| destruction | destroy transfer handling | safe destroy | free/destroy regressions | untracked destroy | E destroy variants | destruction trace | PENDING |
| double destruction | generation/destroy checks | — | double-free corpus | uncertain target | E repeated destroy | `CAND-T003` | PENDING |
| object lifetime | object state lattice | lifetime-safe | UAF corpus | maybe-dead object | E lifetime variants | object trace | PENDING |
| shared borrow lifetime | `createBorrow`, `checkBorrowAccess` | P2 shared | parent-death | unknown parent | E borrow variants | borrow analysis | PENDING |
| mutable borrow exclusivity | `createBorrow`, `checkMutableOwnerAccess` | last-use controls | `mutable_conflict.c` | unknown storage | E overlap/last-use | `CAND-B004` | PENDING |
| owner/borrow invalidation | `invalidateBorrows` | invalidation-safe | live-borrow destruction | unsupported invalidation | E alias invalidation | invalidated borrows | PENDING |
| CFG joins | flow-state join/fixpoint | `tests/cfg/run.sh` | branch/join | unknown CFG state | E reshaped CFG | state traces | PENDING |
| loops | loop analysis/liveness | loop-safe | loop-after-death | loop widening | E nested loops | completeness | PENDING |
| heap generations | allocation-site generation state | `tests/cand1/heap` | stale aliases | widened generation | E generation reuse | heap coverage | PENDING |
| aliases | storage identity graph | storage-safe | storage UAF | unresolved alias | E alias reshapes | storage IDs | PENDING |
| structs | member storage resolution | struct-safe | member UAF | unsupported member | E nested structs | transport findings | PENDING |
| arrays | array storage/indices | multidim safe | array UAF | dynamic index | E index reshapes | unsupported/result | PENDING |
| aggregate transport | aggregate transfer logic | aggregate controls | aggregate UAF | unsupported aggregate | E aggregate mutation | transport rule set | PENDING |
| unions | union classification | declared controls | union lifetime | `union_member_incomplete.c` | E active-member | INCOMPLETE | PENDING |
| memcpy/memmove | byte transport handlers | modeled copies | stale copy | unsupported byte shape | E copy mutation | transport result | PENDING |
| pointer mutation | pointer assignment/advance | pointer-safe | stale pointer | storage incomplete | E expression reshapes | storage/result | PENDING |
| pointer/integer transport | provenance boundary | documented controls | provenance defect | provenance incomplete | E cast round trips | INCOMPLETE | PENDING |
| globals/statics | global/static handlers | global-safe | global UAF | global escape | E global variants | storage result | PENDING |
| out parameters | parameter transport/summaries | interprocedural safe | out-param UAF | unresolved out storage | E parameter reshapes | summary/evidence | PENDING |
| pointer-to-pointer | nested storage bindings | nested controls | nested stale pointer | unsupported depth | E nested aliases | transport result | PENDING |
| function arguments | call transfer summaries | safe calls | argument defects | unknown call | E wrapper args | summary trace | PENDING |
| function returns | return-effect handling | safe returns | invalid return | unresolved return | E return reshapes | finding/result | PENDING |
| interprocedural summaries | summary collector/remapping | `tests/interprocedural` | wrapper defects | recursion/unknown | E wrapper chains | summary fields | PENDING |
| trusted contracts | `agent_policy.cpp` validation | reviewed contracts | self-authored | invalid contract | E substitution/digest | contracts array | PENDING |
| unknown external calls | unknown-call retention | modeled controls | retention defects | unknown call | E external variants | INCOMPLETE/policy | PENDING |
| indirect calls | indirect-call classification | supported controls | invalid effects | unresolved target | E function pointers | result/policy | PENDING |
| callbacks/retention | callback/escape tracking | modeled callbacks | retained borrow | unsupported callback | E callback variants | borrow/ownership | PENDING |
| realloc | realloc transport boundary | documented controls | realloc borrow | `realloc_borrow.c` | E realloc mutations | INCOMPLETE | PENDING |
| cross-TU | one-TU boundary/contracts | contract control | cross-TU defect | uncontracted cross-TU | E9 corpus | scope/evidence | PENDING |
| atomics | explicit boundary | no authoritative PASS | atomic defect | `atomic_pointer_incomplete.c` | E transport variants | INCOMPLETE | PENDING |
| varargs | explicit boundary | no authoritative PASS | vararg defect | policy/tool error | E vararg variants | result | PENDING |
| setjmp/longjmp | explicit boundary | no authoritative PASS | nonlocal defect | `setjmp_nonlocal_incomplete.c` | E control variants | INCOMPLETE | PENDING |
| compiler extensions | C11/no-extension gate | strict C11 | extension input | frontend policy | E flag mutation | toolchain/evidence | PENDING |
| unsupported accounting | `Collector` + PASS predicate | zero unsupported | unsupported fixtures | all unsupported forms | E coverage mutations | completeness fields | PENDING |
| proof-policy authority | policy loading/diff | zero-budget policy | weakened policy | scope/level change | E policy attacks | policy delta | PENDING |
| evidence integrity | evidence build/replay | valid replay | digest/source drift | stale profile | E field mutations | integrity/status | PENDING |
| toolchain qualification | CMake + toolchain validation | D matrix | drift attacks | unsupported profile | E25 matrix | toolchain identity | PENDING |

## Central PASS predicate checklist

`canEmitCand1Pass` must reject findings, unsupported operations, frontend or
contract errors, transport uncertainty, unbound evidence, policy failure,
review-required changes, unsupported toolchains, non-generated/cand1 policy,
empty scope/digest, untrusted contracts, unsafe boundaries, and suppressions.
The E campaign records each predicate as an independent negative case.
