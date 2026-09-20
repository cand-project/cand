# C&1-E Final Qualification Plan

Status: release-candidate documentation; exact-head approval and protected merge remain pending. No semantic implementation is changed by this branch.
Base: `36b2547b7bc364572d02e97e459b7eeeb626935c`
Primary authorities: issues #11 and #26; related review: #6 and #25.
Normative claim boundary: [SPEC-0010](spec/SPEC-0010-cand1-v1-claim.md); decision record: [ADR-0022](adr/ADR-0022-cand1-v1-final-qualification.md).
Executable traceability: [C&1-E traceability](CAND1-E-TRACEABILITY.md).
Invariant: confirmed temporal violation + cand1 PASS = 0.
Unknown ownership/lifetime behavior is never ignored: it must be SUPPORTED / KNOWN SAFE, KNOWN VIOLATION -> FAIL, UNSUPPORTED -> INCOMPLETE, or TOOL/POLICY ERROR.
Sanitizers are differential bug oracles only, never proof. The public C&1 claim text is enabled on this release-candidate branch only; protected main remains unchanged until the final exact-head review and merge gate.

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
| unique ownership | ADR-0016; SPEC-0006 | ownership state engine | ownership safe corpus | duplicate owner cases | alias/move mutation | check evidence ownership rules | PASS (qualified subset) |
| move semantics | SPEC-0006 | move transfer logic | move-safe fixtures | use-after-move | move transport mutations | state trace | PASS (qualified subset) |
| use-after-move | SPEC-0006 | moved-state diagnostics | — | temporal regressions | generated mutation | finding ID/state trace | PASS (qualified subset) |
| destruction | SPEC-0006 | destroy transition | destroy-safe fixtures | invalid destruction | alias destroy variants | ownership transitions | PASS (qualified subset) |
| double destruction | SPEC-0006 | generation/lifetime checks | — | double-free fixtures | repeated destroy mutation | finding ID | PASS (qualified subset) |
| object lifetime | SPEC-0001; SPEC-0006 | lifetime state | lifetime-safe fixtures | expired object cases | scope/return mutations | checked scope | PASS (qualified subset) |
| shared borrow lifetime | SPEC-0005; ADR-0017 | borrow tracker | shared-borrow-safe fixtures | expired shared borrow | return/escape mutation | borrow analysis | PASS (qualified subset) |
| mutable borrow exclusivity | SPEC-0005 | mutable-borrow conflict checks | exclusive mutable fixtures | conflict fixtures | last-use mutation | borrow state trace | KNOWN LIMITATION (#25) |
| owner/borrow invalidation | SPEC-0005 | invalidation propagation | invalidation-safe fixtures | owner death with borrow | alias/aggregate mutation | invalidated borrow counts | PASS (qualified subset) |
| CFG joins | ADR-0011 | flow-sensitive join state | branch/switch safe fixtures | join violations | branch mutation | state traces | PASS (qualified subset) |
| loops | ADR-0011 | loop fixed-point/widening | loop-safe fixtures | loop lifetime violations | loop allocation mutation | widening evidence | PASS (qualified subset) |
| heap generations | ADR-0018 | allocation-site generations | per-iteration safe fixtures | stale-generation cases | reuse/stale alias fuzz | heap generation evidence | PASS (qualified subset) |
| aliases | ADR-0012 | storage identity model | alias-safe fixtures | alias UAF/double destroy | pointer alias mutation | storage identity | PASS (qualified subset) |
| structs | SPEC-0007 | member storage tracking | struct-safe fixtures | member lifetime violations | member alias mutation | checked scope | PASS (qualified subset) |
| arrays | SPEC-0007 | array transport/storage tracking | array-safe fixtures | array lifetime violations | index/alias mutation | transport evidence | PASS (qualified subset) |
| aggregate transport | ADR-0019; SPEC-0007 | aggregate transport rules | aggregate-safe fixtures | aggregate violations | field permutation mutation | transport rule set | PASS (qualified subset) |
| unions | SPEC-0007 | union transport boundary | supported union fixtures | unsafe union cases | active-member mutation | unsupported/evidence | UNSUPPORTED -> INCOMPLETE |
| memcpy/memmove | SPEC-0007 | byte transport classification | modeled copies | lifetime-invalid copies | copy-size/alias mutation | transport findings | UNSUPPORTED -> INCOMPLETE |
| pointer mutation | SPEC-0007 | pointer storage updates | safe pointer updates | stale pointer updates | mutation operators | state trace | PASS (qualified subset) |
| pointer/integer transport | SPEC-0007 | transport boundary policy | supported conversions | unsafe provenance cases | cast mutation | unsupported status | UNSUPPORTED -> INCOMPLETE |
| globals/statics | SPEC-0007 | global storage model | safe global use | invalid global lifetime | cross-call/global mutation | scope/evidence | PASS (qualified subset) |
| out parameters | SPEC-0007 | parameter transport summaries | safe out parameters | invalid out ownership | argument mutation | interprocedural summary | UNSUPPORTED -> INCOMPLETE |
| pointer-to-pointer transport | SPEC-0007 | nested transport model | safe nested transport | invalid nested transport | depth/alias mutation | transport evidence | PASS (qualified subset) |
| function arguments | ADR-0013 | call-site summaries | safe argument calls | invalid ownership calls | argument permutation | summary evidence | PASS (qualified subset) |
| function returns | ADR-0013 | return ownership model | safe returns | borrowed/owned return violations | return mutation | finding/state trace | PASS (qualified subset) |
| interprocedural summaries | ADR-0013 | summary collection/remapping | summary-safe corpus | summary violation corpus | wrapper/recursive mutation | summary counts | PASS (qualified subset) |
| trusted contracts | ADR-0006; SPEC-0003 | contract loader/policy | reviewed trusted contracts | self-authored/invalid contracts | contract substitution | contract digests | PASS (qualified subset) |
| unknown external calls | ADR-0006 | unknown-call boundary | modeled safe calls | unknown retention cases | external-call mutation | unsupported/error result | UNSUPPORTED -> INCOMPLETE |
| indirect calls | SPEC-0002 | indirect-call policy | supported indirect cases | unresolved indirect cases | target substitution | policy result | UNSUPPORTED -> INCOMPLETE |
| callbacks/retention | SPEC-0002 | retention/escape tracking | modeled callbacks | retained-borrow violations | callback mutation | borrow/ownership trace | UNSUPPORTED -> INCOMPLETE |
| realloc | SPEC-0007 | realloc transport policy | supported realloc cases | realloc-borrow cases | size/alias mutation | unsupported/finding | UNSUPPORTED -> INCOMPLETE |
| cross-TU behavior | SPEC-0002 | translation-unit boundary | cross-TU safe corpus | cross-TU violations | declaration mismatch | TU scope evidence | UNSUPPORTED -> INCOMPLETE |
| atomics | SPEC-0001 non-claims | explicit boundary policy | documented unsupported result | no authoritative PASS | atomic syntax mutation | INCOMPLETE/tool error | UNSUPPORTED -> INCOMPLETE |
| varargs | SPEC-0001 non-claims | explicit boundary policy | documented unsupported result | no authoritative PASS | vararg transport mutation | INCOMPLETE/tool error | UNSUPPORTED -> INCOMPLETE |
| setjmp/longjmp | SPEC-0001 non-claims | explicit boundary policy | documented unsupported result | no authoritative PASS | control-flow mutation | INCOMPLETE/tool error | UNSUPPORTED -> INCOMPLETE |
| compiler extensions | SPEC-0006 profile | C11/no-extension gate | strict C11 fixtures | extension inputs | frontend flag mutation | tool/policy result | UNSUPPORTED -> INCOMPLETE |
| unsupported boundary accounting | ADR-0010; issue #26 | fail-closed result classifier | known-safe scope | unsupported never PASS | unknown/coverage mutation | completeness + unsupported | PASS (qualified subset) |
| agent proof-policy authority | ADR-0009; ADR-0010 | generated profile/policy gate | policy-valid PASS | weakened policy fail-policy | policy/base substitution | policy delta/evidence | PASS (qualified subset) |
| evidence integrity | ADR-0014 | deterministic evidence/replay | valid evidence | digest/tamper drift | field-by-field replay | integrity + identity fields | PASS (qualified subset) |
| toolchain qualification | ADR-0021; SPEC-0009 | exact CMake/toolchain guards | qualified profile | version/path/env drift | launcher/Ninja/frontend attacks | profile/build identity | PASS (qualified subset) |

## Decision rule

C&1-E is complete only with zero BLOCKER/HIGH findings, zero confirmed temporal false PASS results, complete final-head evidence, and a reviewed exact head. The candidate branch may carry the narrow claim text; protected main must remain non-claiming until that reviewed merge.

## Central PASS audit

The only C&1 success authority is `canEmitCand1Pass` in `src/cand.cpp`. Its
required predicates are recorded in the traceability document and must be
covered by executable tests. A semantic pass without generated policy,
evidence, exact toolchain, trusted scope, and zero unsupported/error/policy
conditions is not an authoritative C&1 PASS.


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
