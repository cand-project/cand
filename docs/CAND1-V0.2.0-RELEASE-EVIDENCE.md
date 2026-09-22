# C&1/v1 v0.2.0 Release Evidence

Status: **SUSPENDED / SUPERSEDED by v0.2.1** — incident
[#53](https://github.com/cand-project/cand/issues/53) confirmed that tracked
pointers at variadic argument positions could receive authoritative PASS with
zero obligations on this release. The qualification evidence below is
preserved unchanged for historical record; it must not be cited as a current
C&1 soundness claim. The `v0.2.0` tag is immutable and is not moved or
retagged. The current qualified release evidence is
[the v0.2.1 release evidence](CAND1-V0.2.1-RELEASE-EVIDENCE.md).

Previous status: **release candidate — metadata-only finalization from fully
qualified protected main**.

## Release identity

- Release version: `0.2.0`
- Qualified semantic source: `f8f9e7ae71133abcb6f74b71dd94814b0e2ddc63`
- Repair PR #48 merge: `ba12416810d940e8e4ba32ac3022728580cdd5a6`
- Reviewed repair HEAD: `bffe48e8d884d074825e2b762557d7fe05d14a12`
- Claim-restoration PR #49 merge: `f8f9e7ae71133abcb6f74b71dd94814b0e2ddc63`
- Release candidate source: this exact metadata candidate is based on the
  qualified protected-main source above; its final release commit is the
  protected merge commit recorded with tag `v0.2.0`.
- Previous identity: immutable `v0.1.0`, historical and suspended/revoked for
  the affected C&1 claim; it is not rewritten or retagged.

## Claim

v0.2.0 carries the existing SPEC-0010 C&1/v1 claim: qualified temporal
ownership and borrow safety only within the declared checked scope, supported
semantic subset, qualified Ubuntu 24.04/x86_64/C11 profile, and exact evidence
and policy boundaries. Unsupported or unresolved ownership/lifetime semantics
cannot contribute to PASS.

The scope is unchanged. This release adds no C&2, general memory-safety claim,
cross-TU ownership guarantee, callback-retention guarantee, or `realloc`
guarantee.

## Qualification evidence

- CTest: 19/19 PASS.
- Deterministic fuzz: 20,000 cases, seeds 12345 and 67890; zero false PASS.
- Parameter incident corpus: CASE_F/G/H/I repaired to FAIL; no confirmed
  parameter-lifetime false PASS.
- ASan/UBSan: Clang 18.1.3 differential confirmed executable temporal defects;
  no authoritative C&1 PASS was emitted for a confirmed violation.
- Existing Hiredis mutation controls: 9/9 INCOMPLETE, 0 PASS.
- Policy, evidence, contract, and toolchain attack suites: PASS.
- `scripts/check.sh`: PASS; `git diff --check`: PASS.
- Reproducibility: two clean verifier builds identical.
- Verifier binary SHA256:
  `06aa9120a5cd0d33b15ebc0e6cfc465b73a60113ba086aca1a7437406c89cc0a`.
- Release-candidate metadata rebuild SHA256:
  `c582daaf5142b0666980342cadd39dc9d367f3d77596df61a752aa12646a8a47`;
  two clean builds produced this identical hash.

## Qualified toolchain

- Ubuntu 24.04, x86_64, C11, extensions disabled.
- Clang/LLVM 18.1.3; production GCC 13.3.0.
- CMake 3.28.3; Ninja 1.11.1.
- Target `x86_64-pc-linux-gnu`; sysroot `ubuntu-24.04-default`.

## Review and CI identities

- Repair reviewer: `senolcolak`.
- Repair PR #48 required checks: runs `35541114797` and `35541114838`, both
  successful.
- Claim-identity PR #49 required checks: runs `35542083263` and `35542083230`,
  both successful.
- Release PR review and merge identities are recorded in the final release
  report and GitHub release metadata after protected merge.

This document is evidence metadata only. It does not change verifier
semantics, the public claim scope, or the frozen adoption/Redis program.
