# C&1-E Final Qualification Plan

Status: qualification planning only. No semantic implementation is changed by this branch.
Base: `36b2547b7bc364572d02e97e459b7eeeb626935c`
Primary authorities: issues #11 and #26; related review: #6 and #25.
Invariant: confirmed temporal violation + cand1 PASS = 0.
Unknown ownership/lifetime behavior is never ignored: it must be SUPPORTED / KNOWN SAFE, KNOWN VIOLATION -> FAIL, UNSUPPORTED -> INCOMPLETE, or TOOL/POLICY ERROR.
Sanitizers are differential bug oracles only, never proof. The public C&1 claim remains disabled.

## Campaign order

1. Re-read and map issues #11, #26, #6, and #25 against the exact branch head.
2. Run the positive, negative, adversarial, unsupported-boundary, proof-policy, evidence, and toolchain suites independently.
3. Confirm every negative case fails closed and every sanitizer-confirmed temporal violation is rejected.
4. Investigate any false PASS as a BLOCKER; record conservative false FAIL/INCOMPLETE as non-blocking only when safe.
5. Re-run the complete authoritative matrix at the final reviewed head.
6. Activate no public claim unless every gate passes on that exact reviewed head.

## Requirement matrix

| C&1 requirement | Normative source | Implementation | Positive tests | Negative tests | Adversarial tests | Evidence | Status |
|---|---|---|---|---|---|---|---|
| unique ownership | ADR-0016; SPEC-0006 | ownership state engine | ownership safe corpus | duplicate owner cases | alias/move mutation | check evidence ownership rules | PENDING C&1-E |
| move semantics | SPEC-0006 | move transfer logic | move-safe fixtures | use-after-move | move transport mutations | state trace | PENDING C&1-E |
| use-after-move | SPEC-0006 | moved-state diagnostics | — | temporal regressions | generated mutation | finding ID/state trace | PENDING C&1-E |
| destruction | SPEC-0006 | destroy transition | destroy-safe fixtures | invalid destruction | alias destroy variants | ownership transitions | PENDING C&1-E |
| double destruction | SPEC-0006 | generation/lifetime checks | — | double-free fixtures | repeated destroy mutation | finding ID | PENDING C&1-E |
| object lifetime | SPEC-0001; SPEC-0006 | lifetime state | lifetime-safe fixtures | expired object cases | scope/return mutations | checked scope | PENDING C&1-E |
| shared borrow lifetime | SPEC-0005; ADR-0017 | borrow tracker | shared-borrow-safe fixtures | expired shared borrow | return/escape mutation | borrow analysis | PENDING C&1-E |
| mutable borrow exclusivity | SPEC-0005 | mutable-borrow conflict checks | exclusive mutable fixtures | conflict fixtures | last-use mutation | borrow state trace | PENDING C&1-E |
| owner/borrow invalidation | SPEC-0005 | invalidation propagation | invalidation-safe fixtures | owner death with borrow | alias/aggregate mutation | invalidated borrow counts | PENDING C&1-E |
| CFG joins | ADR-0011 | flow-sensitive join state | branch/switch safe fixtures | join violations | branch mutation | state traces | PENDING C&1-E |
| loops | ADR-0011 | loop fixed-point/widening | loop-safe fixtures | loop lifetime violations | loop allocation mutation | widening evidence | PENDING C&1-E |
| heap generations | ADR-0018 | allocation-site generations | per-iteration safe fixtures | stale-generation cases | reuse/stale alias fuzz | heap generation evidence | PENDING C&1-E |
| aliases | ADR-0012 | storage identity model | alias-safe fixtures | alias UAF/double destroy | pointer alias mutation | storage identity | PENDING C&1-E |
| structs | SPEC-0007 | member storage tracking | struct-safe fixtures | member lifetime violations | member alias mutation | checked scope | PENDING C&1-E |
| arrays | SPEC-0007 | array transport/storage tracking | array-safe fixtures | array lifetime violations | index/alias mutation | transport evidence | PENDING C&1-E |
| aggregate transport | ADR-0019; SPEC-0007 | aggregate transport rules | aggregate-safe fixtures | aggregate violations | field permutation mutation | transport rule set | PENDING C&1-E |
| unions | SPEC-0007 | union transport boundary | supported union fixtures | unsafe union cases | active-member mutation | unsupported/evidence | PENDING C&1-E |
| memcpy/memmove | SPEC-0007 | byte transport classification | modeled copies | lifetime-invalid copies | copy-size/alias mutation | transport findings | PENDING C&1-E |
| pointer mutation | SPEC-0007 | pointer storage updates | safe pointer updates | stale pointer updates | mutation operators | state trace | PENDING C&1-E |
| pointer/integer transport | SPEC-0007 | transport boundary policy | supported conversions | unsafe provenance cases | cast mutation | unsupported status | PENDING C&1-E |
| globals/statics | SPEC-0007 | global storage model | safe global use | invalid global lifetime | cross-call/global mutation | scope/evidence | PENDING C&1-E |
| out parameters | SPEC-0007 | parameter transport summaries | safe out parameters | invalid out ownership | argument mutation | interprocedural summary | PENDING C&1-E |
| pointer-to-pointer transport | SPEC-0007 | nested transport model | safe nested transport | invalid nested transport | depth/alias mutation | transport evidence | PENDING C&1-E |
| function arguments | ADR-0013 | call-site summaries | safe argument calls | invalid ownership calls | argument permutation | summary evidence | PENDING C&1-E |
| function returns | ADR-0013 | return ownership model | safe returns | borrowed/owned return violations | return mutation | finding/state trace | PENDING C&1-E |
| interprocedural summaries | ADR-0013 | summary collection/remapping | summary-safe corpus | summary violation corpus | wrapper/recursive mutation | summary counts | PENDING C&1-E |
| trusted contracts | ADR-0006; SPEC-0003 | contract loader/policy | reviewed trusted contracts | self-authored/invalid contracts | contract substitution | contract digests | PENDING C&1-E |
| unknown external calls | ADR-0006 | unknown-call boundary | modeled safe calls | unknown retention cases | external-call mutation | unsupported/error result | PENDING C&1-E |
| indirect calls | SPEC-0002 | indirect-call policy | supported indirect cases | unresolved indirect cases | target substitution | policy result | PENDING C&1-E |
| callbacks/retention | SPEC-0002 | retention/escape tracking | modeled callbacks | retained-borrow violations | callback mutation | borrow/ownership trace | PENDING C&1-E |
| realloc | SPEC-0007 | realloc transport policy | supported realloc cases | realloc-borrow cases | size/alias mutation | unsupported/finding | PENDING C&1-E |
| cross-TU behavior | SPEC-0002 | translation-unit boundary | cross-TU safe corpus | cross-TU violations | declaration mismatch | TU scope evidence | PENDING C&1-E |
| atomics | SPEC-0001 non-claims | explicit boundary policy | documented unsupported result | no authoritative PASS | atomic syntax mutation | INCOMPLETE/tool error | PENDING C&1-E |
| varargs | SPEC-0001 non-claims | explicit boundary policy | documented unsupported result | no authoritative PASS | vararg transport mutation | INCOMPLETE/tool error | PENDING C&1-E |
| setjmp/longjmp | SPEC-0001 non-claims | explicit boundary policy | documented unsupported result | no authoritative PASS | control-flow mutation | INCOMPLETE/tool error | PENDING C&1-E |
| compiler extensions | SPEC-0006 profile | C11/no-extension gate | strict C11 fixtures | extension inputs | frontend flag mutation | tool/policy result | PENDING C&1-E |
| unsupported boundary accounting | ADR-0010; issue #26 | fail-closed result classifier | known-safe scope | unsupported never PASS | unknown/coverage mutation | completeness + unsupported | PENDING C&1-E |
| agent proof-policy authority | ADR-0009; ADR-0010 | generated profile/policy gate | policy-valid PASS | weakened policy fail-policy | policy/base substitution | policy delta/evidence | PENDING C&1-E |
| evidence integrity | ADR-0014 | deterministic evidence/replay | valid evidence | digest/tamper drift | field-by-field replay | integrity + identity fields | PENDING C&1-E |
| toolchain qualification | ADR-0021; SPEC-0009 | exact CMake/toolchain guards | qualified profile | version/path/env drift | launcher/Ninja/frontend attacks | profile/build identity | PASS C&1-D; recheck E |

## Decision rule

C&1-E is complete only with zero BLOCKER/HIGH findings, zero confirmed temporal false PASS results, complete final-head evidence, and a reviewed exact head. Until then, README and `docs/SAFETY_CLAIMS.md` must continue to state that C&1 is not publicly qualified.
