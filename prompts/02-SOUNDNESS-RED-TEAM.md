# Prompt: C& Soundness Red-Team

You are red-teaming the C& verifier in:

https://github.com/cand-project/cand

Your goal is not to maximize coverage or praise. Your goal is to find **false PASS**, proof-boundary bypasses, and incorrect semantic assumptions.

A false PASS is more serious than an INCOMPLETE result.

Do not weaken C& policy or modify expected outputs to obtain green results.

## Ground rules

1. Record exact commit SHA and environment.
2. Read `REVIEWING.md`, `docs/SAFETY_CLAIMS.md`, `docs/THREAT_MODEL.md`, ADR-0010 and relevant implementation ADRs.
3. Build and run the existing suite first.
4. Preserve every new defect as a minimal standalone C reproducer.
5. If a finding could invalidate a published safety claim, follow `SECURITY.md` before public disclosure.

## Primary property under attack

Within semantics C& claims to encounter/model:

```text
known violation     -> FAIL
unknown/relevant    -> INCOMPLETE
known clean         -> PASS
```

Find any path that effectively becomes:

```text
unknown/relevant -> ignored -> PASS
```

## Attack taxonomy

Generate adversarial programs across these categories.

### Storage and alias transport

- local alias declarations;
- assignment aliases;
- chains of 3+ aliases;
- reassignment and clearing;
- nested struct fields;
- arrays and multidimensional arrays;
- dynamic indexes;
- pointer-to-pointer storage;
- aggregate copy/initializer/return;
- unions and anonymous unions;
- `memcpy`, `memmove`, byte copies.

### Control flow

- if/else joins;
- nested joins;
- early return;
- switch/fallthrough;
- goto cleanup;
- loops with allocation/destruction;
- loop-carried aliases;
- break/continue;
- conditional/comma operators;
- short-circuit expressions;
- setjmp/longjmp/non-local control.

### Pointer transformations

- `p + n`, `p++`, `p--`, `p += n`;
- address-of pointer storage (`&p`);
- pointer-to-integer and integer-to-pointer casts;
- `void *` round trips;
- qualifier casts;
- compound literals;
- flexible array/member-address patterns.

### Function boundaries

- pointer parameters;
- returns;
- out-parameters;
- pointer-to-pointer parameters;
- callbacks;
- unknown/external calls;
- wrapper allocators/destructors;
- retaining APIs;
- function pointers/indirect calls;
- variadic functions and `va_arg`.

### Storage duration/concurrency

- globals;
- static locals;
- thread-local storage if supported by the frontend;
- atomics;
- signal-visible state where relevant.

### Language/toolchain edges

- macros expanding to ownership-affecting code;
- GNU statement expressions;
- cleanup attributes;
- inline assembly;
- invalid compiler flags/source recovery;
- target/compiler-extension differences.

## Test matrix

For each executable case, record:

```text
source
ordinary clang/gcc compile result
cand result + exit code
ASan result
UBSan result if relevant
CSA/other analyzer result if useful
expected classification
```

Do not treat `asan_rc != 0` by itself as proof of UAF/double-free; inspect the actual sanitizer diagnostic category.

## Required safe corpus

At least 25 generated cases must be intentionally safe. This is necessary to detect false FAIL and overfitting toward rejection.

## Required unsafe corpus

At least 75 generated cases should contain a deliberate temporal-lifetime defect or an unsupported pointer-transport mechanism.

Where C& does not model the mechanism, the expected result may be `INCOMPLETE`. Do not demand FAIL when the verifier has not claimed those semantics.

## Mutation strategy

Take existing passing/failing fixtures and mutate them one dimension at a time:

- rename variables;
- wrap expressions in casts/parentheses/comma expressions;
- move storage into a field/array;
- copy aggregate;
- move operation across a branch;
- put allocation inside a loop;
- destroy through a different alias;
- pass address to an unknown function;
- change storage duration;
- change direct call to indirect call.

This helps identify syntax-shape overfitting.

## Proof-policy attacks

If agent-mode/policy functionality exists, attempt to obtain success by:

- adding unsafe;
- adding suppressions;
- lowering safety level;
- excluding files/functions;
- changing trusted contract classification;
- modifying baseline/evidence files;
- changing expected fixtures.

A system that reports these as ordinary repairs has a policy defect.

## Determinism

Run representative files at least 5 times. Machine-readable JSON should be semantically and preferably byte-for-byte stable where the format promises determinism.

## Stop condition

Do not stop after the first clean battery.

After fixing or reporting discovered patterns, create a second battery designed around the mechanisms that escaped the first battery.

A review is only "clean" for this session when the final new battery discovers no additional BLOCKER/HIGH false-PASS or proof-boundary defects.

## Report

Return:

```text
C& SOUNDNESS RED-TEAM REPORT

Exact HEAD:
Environment:
Existing tests:

Programs tested:
Safe cases:
Unsafe/unsupported cases:

FALSE PASS:
FALSE FAIL:
CORRECT INCOMPLETE:
ANALYZER CRASH/ERROR:
POLICY BYPASS:
NONDETERMINISM:

BLOCKER:
HIGH:
MEDIUM:
LOW:

New permanent regression fixtures recommended:
...

Unreviewed risk areas:
...

Recommendation:
MERGE / DO NOT MERGE / CONTINUE HARDENING
```

Never convert a discovered false PASS into a broader unsupported exemption without documenting why the narrower safety boundary remains useful and defensible.
