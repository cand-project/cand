# C&1 Parameter-Lifetime Soundness Incident

Status: **BLOCKER — public C&1/v1 claim suspended pending repair and exact-head requalification.**

Incident issue: [#46](https://github.com/cand-project/cand/issues/46)

## Reproducer and authority

The incident was found on protected main
`5199206fe67e9ddeec4048d14edf7bec66235283`, using
`tests/interprocedural/parameter_lifetime_redteam.c` on the
`parameter-lifetime-audit` branch.

Environment: Ubuntu 24.04/x86_64, Clang 18.1.3, C11. The verifier was built
from the protected-main source with the normal CMake build.

The affected implementation is also present in the immutable `v0.1.0` source
tag. The tag is not moved or rewritten, but its active public safety claim and
dependent evidence are suspended until requalification.

## Red-team matrix

| Case | Scenario | C& result | ASan/UBSan | Classification |
|---|---|---:|---:|---|
| A | borrowed parameter read | PASS | clean | safe control |
| B | borrowed parameter write | PASS | clean | safe control |
| C | borrowed parameter freed then read | INCOMPLETE | temporal violation | fail-closed |
| D | destroyed parameter read | INCOMPLETE | temporal violation | fail-closed |
| E | destroyed parameter written | INCOMPLETE | temporal violation | fail-closed |
| F | destroyed parameter freed twice | **PASS** | double-free | false PASS |
| G | owning parameter freed then read | **PASS** | use-after-free | false PASS |
| H | owning parameter freed then written | **PASS** | use-after-free | false PASS |
| I | owning parameter freed twice | **PASS** | double-free | false PASS |
| J | alias read after owning parameter destruction | INCOMPLETE | temporal violation | fail-closed |
| K | original read after alias destruction | INCOMPLETE | temporal violation | fail-closed |
| L | consumed through wrapper then reused | INCOMPLETE | temporal violation | fail-closed |
| M | trusted external destructor then read | FAIL | temporal violation | detected |
| N | conditional destruction then read | INCOMPLETE | temporal violation | fail-closed |
| O | loop destruction then read | INCOMPLETE | temporal violation | fail-closed |

The four bold results are confirmed false PASS cases. The sanitizer controls
used Clang 18.1.3 with:

```sh
clang -std=c11 -O0 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer tests/interprocedural/parameter_lifetime_redteam.c \
  -DCASE_F -o /tmp/parameter-case
```

Each case was compiled and run independently with
`ASAN_OPTIONS=detect_leaks=0`. Cases C through O all produced an independent
temporal sanitizer finding; M used a small executable stub for the trusted
`vendor_destroy` contract.

## Root cause

`FlowAnalyzer::currentParameter` identifies a direct parameter by declaration,
but no callee-side `StorageBinding` or `ObjectId` is created for that
parameter. Direct parameter paths in `checkAccess` and `handleFree` consult
`FunctionSummary` effects and return without a tracked lifetime transition.
The modeled call path similarly avoids `destroyBinding` for matching direct
`Destroy` and `TakeOwnership` parameters.

Consequently, repeated destruction and accesses after destruction cannot observe
an `ObjectState::Dead` transition. Local aliases remain conservatively
unsupported, but direct parameter use bypasses the same fail-closed boundary.

## Required response

- All adoption work, Redis analysis, C&2 work, and v0.2.0 release work are
  suspended.
- The public C&1/v1 claim and affected evidence/release identities are
  suspended; `v0.1.0` is not rewritten or retagged.
- A permanent safe control and invalid regressions must be retained after the
  repair is designed.
- The repair must preserve fail-closed behavior and cannot reclassify the
  cases as unsupported merely to restore PASS.
- Complete exact-head C&1 qualification, independent review, fresh evidence,
  and protected-main approval are required before claim restoration.

No semantic repair has been attempted in this incident response.

## Repair and resolution evidence

The repair replaces the unsound direct-parameter fast paths with real
callee-side lifetime state. Each modeled parameter receives a deterministic
symbolic object in a reserved parameter-object namespace, with separate
origin, liveness, and capability fields. `TakeOwnership` creates the owning
capability, `Destroy` creates destructive authority without unrestricted
ownership transfer, and `Borrow` creates no destruction authority. Local
aliases preserve the same object identity; destruction is therefore visible
through every proven alias. Flow-state joins preserve `Dead`/`MaybeDead`
states rather than restoring a definitely-live parameter. Candidate
declaration annotations remain non-authoritative.

The affected release range is the immutable `v0.1.0` tag and the protected
main lineage through incident-suspended commit
`5199206fe67e9ddeec4048d14edf7bec66235283` (including documentation-only
incident-response merge `36670f1ceb7555e967dec7c23e098186f3e8204d`). The tag
is not rewritten or retagged.

On the repaired candidate, the permanent matrix is:

| Case | Repaired C& result | ASan/UBSan | Outcome |
|---|---:|---:|---|
| A/B | PASS | clean | safe controls |
| C–L | FAIL | temporal violation | detected |
| M | FAIL | temporal violation | detected |
| N/O | INCOMPLETE | temporal violation | fail-closed |

In particular, CASE_F, CASE_G, CASE_H, and CASE_I are `FAIL`, not
`INCOMPLETE`. The nine existing Hiredis temporal mutation controls remain
independently sanitizer-confirmed and all return `INCOMPLETE`, with zero PASS.
The added permanent parameter suite covers borrow, destroy, ownership,
aliases, moves, nullability, multiple parameters, typedef/const parameters,
conditional/loop joins, and same-argument aliasing. Conditional and loop
destruction remain intentionally fail-closed because their summary behavior is
not representable by the current qualified contract model.

The repair qualification records 19/19 CTest tests passed, zero confirmed
false PASS, zero false PASS in 20,000 deterministic fuzz cases across seeds
12345 and 67890, zero policy/evidence/contract/toolchain attack escapes, and
two clean verifier builds with identical SHA256
`43bda36b7472661d0bea942e5e5471297eff7a6914ffed6786b350218f61b8b0`.
The exact reviewed repair HEAD, reviewer, and protected-main merge SHA are
recorded in the final #46 resolution comment. Claim restoration is effective
only after that exact candidate passes protected review and merges. No v0.2.0
release is authorized by this repair.
