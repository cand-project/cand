# SPEC-0009 — C&1-D toolchain qualification

Status: Draft release gate; C&1 public claim remains disabled

## Normative profile

C&1/v1 is release-qualified only on the exact profile in
`toolchains/cand1-v1-linux-x86_64.json`: Ubuntu 24.04 x86_64, C11 without
extensions, Clang/LLVM 18.1.3, target `x86_64-pc-linux-gnu`, the declared
default sysroot identity, and the pinned reference build tools.

The profile is a support boundary, not a semantic expansion. Other frontend
versions, targets, standards, sysroots, include roots, plugins, response
files, and compiler extensions are unsupported and must produce a tool/policy
failure or `INCOMPLETE`, never an authoritative cand1 PASS.

## Evidence identity

Release evidence binds all of the following:

* verifier source commit and binary SHA-256;
* C++ build compiler identity and version;
* Clang and LLVM versions;
* target triple and C standard;
* frontend arguments;
* sysroot identity and reference environment digest.

Replay requires exact agreement with the running verifier and re-executes the
recorded frontend configuration. A changed toolchain identity is stale even
when the source and binary hashes are unchanged.

## Reproducibility and compatibility

The reference Docker image and apt package versions are pinned. Two builds in
the reference environment are expected to have the same binary SHA-256; the
qualification workflow records the digest and fails if the build identity is
not profile-qualified. GCC and Clang compatibility fixtures prove annotation
erasure, ABI neutrality, and normal program behavior; they do not make GCC an
analysis frontend.

## CI

The dedicated toolchain workflow configures, builds, runs the repository and
C&1-C tests, compiles the compatibility fixture with both production
compilers, and runs evidence identity/drift tests. Extended fuzzing remains a
scheduled/manual C&1-C job.
