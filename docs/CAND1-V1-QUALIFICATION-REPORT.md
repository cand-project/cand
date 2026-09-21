# C&1/v1 Qualification Report — Final Qualification

> Historical qualification status: the original v0.1.0 qualification was
> suspended by the parameter-lifetime false PASS. The exact-head repair and
> supplemental requalification recorded below restore the current protected
> main claim after merge; the immutable v0.1.0 tag is not rewritten.

Status: final qualification complete; C&1/v1 is restored on protected main at
repair HEAD `bffe48e8d884d074825e2b762557d7fe05d14a12`, merged as
`ba12416810d940e8e4ba32ac3022728580cdd5a6`.

## Release identities

- D-qualified base: `36b2547b7bc364572d02e97e459b7eeeb626935c`
- Technical E precursor: `e929372c18a55ab364e2089a55d230222b0c8bc8`
- Reviewed qualification head: `7a6f4b65fb6e7506d073f9c93aa615c0e6bf8860`
- Merge/release commit: `3a2672b6b6a6742c8ac19c2894a698cdd1970b7a`
- Branch: `cand1-e-final-qualification` (merged)
- PR: #34
- Parameter-lifetime repair HEAD: `bffe48e8d884d074825e2b762557d7fe05d14a12`
- Parameter-lifetime repair merge: `ba12416810d940e8e4ba32ac3022728580cdd5a6` (PR #48)
- Repair reviewer: `senolcolak`

The precursor contains the qualified implementation and no semantic changes
are made by the claim-activation commit. The active merge authority is
protected pull-request review plus required CI; the historical trusted-agent
attestation workflow is not merge authority.

## Normative claim

C&1/v1 provides qualified temporal ownership and borrow safety only for
ownership/lifetime operations fully analyzed within the declared checked scope,
supported semantic subset, and qualified Ubuntu 24.04 x86_64/C11 toolchain
profile. An authoritative C&1 PASS means no covered temporal
ownership/borrow violation was found. Unsupported or unresolved
ownership/lifetime semantics fail closed and cannot contribute to PASS.

The normative sources are [SPEC-0010](spec/SPEC-0010-cand1-v1-claim.md) and
[ADR-0022](adr/ADR-0022-cand1-v1-final-qualification.md). The executable
mapping is [C&1-E traceability](CAND1-E-TRACEABILITY.md).

## Explicit non-claims

C&1/v1 does not claim spatial or bounds safety, arbitrary pointer arithmetic,
null safety, integer safety, general pointer/integer provenance safety, inline
assembly correctness, general concurrency or data-race safety, unsupported
language/compiler constructs, unsupported cross-TU ownership semantics,
correctness inside unsafe/unsupported regions, or semantics outside the
qualified profile. It does not claim “memory-safe C”, Rust-equivalent
whole-language safety, or that all temporal bugs are impossible.

## Qualification evidence

- CTest: 19/19 passed, including P0–P2, C&1-A/B/C/D, and four E gates.
- C&1-C conformance: 120 fixtures — 40 SAFE, 50 KNOWN_VIOLATION, 30 UNSUPPORTED.
- Independent adversarial corpus: 128 cases — 96 negative, 32 controls; zero negative false PASS.
- Extended fuzz: seeds 303 and 404; 10,000 cases per seed; each produced 3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE, zero false PASS, zero coverage gap, zero harness error, and deterministic replay.
- Sanitizer differential: 40/40 independent temporal defects rejected; each fuzz seed also confirmed 3,333/3,333 temporal defects with zero C&1 false PASS.
- Cross-TU: safe no-ownership control PASS; uncontracted ownership/lifetime effects INCOMPLETE.
- Policy/evidence attacks: weakened policy, scope, frontend, base, source, policy, verifier, toolchain, digest, and evidence mutations fail closed.
- Toolchain attacks: launcher, external-toolchain, config, target, sysroot, compiler, CMake, Ninja, PATH-shadow, response-file, plugin, include-root, frontend, and evidence substitutions fail closed.

## Parameter-lifetime repair requalification

The repair seeds deterministic symbolic parameter objects into each modeled
callee entry state. Object liveness is distinct from parameter capability;
aliases share the parameter object identity, destruction transitions that
object to `Dead`, and later accesses or repeated destruction are reported.
Borrowed parameters cannot acquire destruction or ownership-transfer
authority. Conditional and loop destruction whose summary remains
unrepresentable stays `INCOMPLETE`.

- Pre-fix false PASS cases F/G/H/I: 4; post-fix authoritative false PASS: 0.
- Parameter red-team CASE_A–O: A/B PASS; C–L and M FAIL; N/O INCOMPLETE.
- Individual parameter regressions: all definite temporal invalid cases FAIL;
  conditional/loop cases remain fail-closed INCOMPLETE.
- Existing Hiredis temporal mutation controls: 9/9 INCOMPLETE, 0 PASS.
- Complete CTest: 19/19 passed.
- Deterministic fuzz: seeds 12345 and 67890, 10,000 cases each; zero false
  PASS, coverage gap, false positive, harness error, or wrong failure class.
- Two clean reproducibility builds: SHA256
  `06aa9120a5cd0d33b15ebc0e6cfc465b73a60113ba086aca1a7437406c89cc0a` for
  both binaries.
- Qualified environment: Ubuntu 24.04/x86_64, Clang 18.1.3, C11, with the
  existing toolchain, policy, evidence, contract, and sanitizer gates.

The repair is within the existing SPEC-0010 checked parameter-lifetime scope;
it does not add cross-TU summaries, pointer-output ownership, realloc, callback
retention, or a broader public claim. The immutable v0.1.0 release remains
historically suspended. The first post-incident qualified release identity is
v0.2.0; this release does not expand the SPEC-0010 C&1/v1 scope.

## Qualified toolchain and reproducibility

- Ubuntu 24.04, x86_64, C11, extensions disabled.
- Clang/LLVM 18.1.3; production GCC 13.3.0.
- CMake 3.28.3.
- Ninja `/usr/bin/ninja`, version 1.11.1.
- Target `x86_64-pc-linux-gnu`.
- Sysroot `ubuntu-24.04-default`.
- Reference manifest digest `496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4`.
- Two independent clean verifier builds: `df46a102dcb302ea8c381f1cf40cdb56d18c0f7a6a5ee016decb9764c20667f` and the same SHA; identical: `true`.

## Known limitation and findings

Issue #25 remains open as conservative mutable-borrow last-use precision debt:
safe last-use controls may be rejected, while overlapping invalid mutable
borrows do not PASS. This is a demonstrated precision limitation, not a false
PASS.

Final residual severity is BLOCKER 0 for implementation soundness, HIGH 0,
MEDIUM 1 (#25), LOW 0. Release acceptance still requires one fresh approval on
the exact candidate head, required CI, and protected merge.

During E, two pre-final defects were fixed and permanently regressed: evidence
replay now reuses the recorded safety level, and explicit external include
flags are rejected by the build authority boundary. Neither produced a
confirmed temporal false PASS.

## False-PASS response

`docs/CAND1-FALSE-PASS-RESPONSE.md` defines claim suspension, evidence
invalidation, affected-release identification, minimization, permanent
regression, repair, and complete exact-head requalification after any
confirmed false PASS.

## Release gate

The final claim requires exact-head `scripts/check.sh`, complete CTest,
conformance, adversarial, sanitizer, fuzz, policy/evidence, toolchain,
reproducibility, and `git diff --check` results; required GitHub CI; and a
fresh human approval reviewing the implementation, SPEC, ADR, evidence,
traceability, limitations, and public wording. Issues #11 and #26 are closed;
issue #25 remains open as conservative precision debt only. The exact-head
qualification, review, required CI, and protected merge conditions are complete.
