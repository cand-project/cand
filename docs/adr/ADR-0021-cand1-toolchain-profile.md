# ADR-0021 — Narrow reproducible C&1 toolchain profile

Status: Accepted for C&1-D

## Decision

Qualify only Ubuntu 24.04 x86_64 with Clang/LLVM 18.1.3, C11 without
extensions, target `x86_64-pc-linux-gnu`, the pinned default sysroot identity,
Clang 18.1.3 and GCC 13.3.0 production compilers, and the pinned CMake/Ninja
build tools. The profile is represented by a versioned JSON manifest and an
immutable container digest.

The verifier embeds and emits toolchain identity. Strict cand1 checks validate
the linked frontend and runtime target against the profile; evidence replay
rejects mismatched toolchain identity. CMake rejects a build configured with a
different analyzer or build compiler before a binary can be qualified.

## Consequences

* A precise, small support claim is available.
* Host-default headers are represented by an explicit sysroot identity rather
  than an unsafe ad hoc hash of an arbitrary host filesystem.
* LLVM/Clang and target expansion require a new qualification profile.
* The qualified public C&1 claim uses this profile; expansion requires a new
  qualification profile and evidence.

## Rejected alternatives

* Advertising `ubuntu-latest` plus unpinned apt packages: version drift makes
  evidence non-reproducible.
* Treating a binary hash as sufficient frontend identity: dynamic libraries,
  target defaults, and sysroot behavior can still differ.
* Claiming GCC analysis compatibility: GCC is tested only for annotation
  erasure and ordinary C/ABI behavior.
