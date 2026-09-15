# Prompt: Independent C& Repository Review

You are performing an independent technical review of:

https://github.com/cand-project/cand

Project: **C& — C with Ownership**

Core thesis:

> **LLMs synthesize. C& verifies.**

Your task is to determine whether the repository's architecture, implementation, evidence, documentation, and current claims agree with one another.

Do not optimize for politeness or praise. Optimize for correctness.

Do not stop after reading the README.

## 1. Establish the exact review target

Record:

- repository commit SHA;
- branch/ref;
- operating system;
- Clang/LLVM version;
- GCC version;
- CMake/Ninja versions;
- any local modifications.

Review `main` and open PRs separately. Never attribute an open PR's behavior to `main` unless it is merged.

## 2. Read the project authority

At minimum inspect:

```text
README.md
REVIEWING.md
SECURITY.md
CONTRIBUTING.md
docs/SAFETY_CLAIMS.md
docs/THREAT_MODEL.md
docs/adr/
docs/spec/
contracts/
src/
include/
tests/
examples/
scripts/check.sh
.github/workflows/
```

Pay special attention to:

- ADR-0001: pipeline layer, not compiler;
- ADR-0008: LLM-first synthesis;
- ADR-0009: agent proof policy;
- ADR-0010: Trustworthy PASS;
- ADR-0011: CFG/dataflow semantics;
- SPEC-0004: machine-agent protocol.

## 3. Build and reproduce

Build from source using the repository-supported workflow.

Run all tests and validation scripts.

Do not treat green CI as proof of soundness. Confirm that documented commands actually work on a clean checkout.

Record failures exactly.

## 4. Architecture review

Evaluate whether these design decisions are coherent:

1. C& is an analysis/verifier layer, not a new compiler.
2. C& annotations should not alter executable C semantics or ABI.
3. upstream Clang tooling is used for parsing/CFG analysis while GCC/Clang may remain production compilers.
4. `PASS`, `FAIL`, and `INCOMPLETE` are distinct semantic outcomes.
5. unsupported ownership-relevant semantics fail closed.
6. LLM-generated code is untrusted input.
7. trusted contracts and policy changes are separate from ordinary code repair.

Look for code that contradicts any of these decisions.

## 5. Implementation review

Review the analyzer for:

- silent returns that can discard ownership-relevant semantics;
- AST constructs that transport pointer values outside the modeled state;
- incorrect CFG joins;
- unstable object/storage identity;
- stale diagnostics emitted before convergence;
- nondeterministic iteration/output;
- assumptions about NULL that hide ambiguous aliases;
- per-function state incorrectly used for global/static state;
- allocation-site abstraction errors in loops/re-entry;
- aggregate, union, array and pointer-arithmetic holes;
- unknown external calls treated as harmless;
- invalid compiler input still producing PASS;
- schema/output drift.

Search for cases where the implementation effectively does:

```text
unknown -> ignore -> PASS
```

Any such ownership-relevant path is a priority finding.

## 6. Documentation/claim review

Compare README, ADRs, SPECs, implementation reports, source comments and tests.

Find contradictions such as:

- docs claiming support that code does not implement;
- implementation supporting behavior the schema forbids;
- stale HEAD/test counts;
- broad safety language unsupported by evidence;
- limitations hidden only in implementation reports rather than architecture docs.

Current P0/P0.x behavior must not be described as a proven C&1 guarantee.

## 7. Test-quality review

Check whether tests:

- contain real unsafe and repaired pairs;
- independently verify negative cases where practical;
- distinguish sanitizer crash/leak/UB categories correctly;
- cover safe programs to detect false FAIL;
- persist previously discovered analyzer bugs as regression fixtures;
- test determinism;
- test ordinary GCC/Clang compatibility;
- test unsupported semantics as INCOMPLETE rather than merely accepting any nonzero exit.

## 8. Adversarial spot checks

Create at least 30 additional small C variants not already present.

Vary:

- aliases;
- control-flow joins;
- loops;
- struct fields;
- aggregate copies;
- unions;
- array indexing;
- pointer arithmetic/mutation;
- globals/statics;
- function parameters/returns/out-parameters;
- unknown calls;
- callbacks;
- casts/provenance;
- atomics;
- varargs;
- non-local control flow;
- compiler extensions.

Classify each as:

```text
CORRECT PASS
CORRECT FAIL
CORRECT INCOMPLETE
FALSE PASS
FALSE FAIL
ANALYZER BUG
```

A false PASS is the most serious result.

## 9. Independent oracles

Where practical compare with:

- AddressSanitizer;
- UBSan;
- Clang Static Analyzer;
- compiler warnings;
- another static-analysis tool.

Do not assume the external oracle is automatically correct. Use disagreements as investigation triggers.

## 10. LLM-first architecture review

Evaluate whether the repository genuinely supports the thesis:

```text
LLM -> ordinary C -> cand -> structured obligation -> repair -> cand -> compiler/tests
```

Check whether machine-readable diagnostics are sufficiently stable and precise for an agent.

Check whether an agent could obtain a green result by changing proof policy rather than repairing implementation semantics.

## 11. Report format

Return:

```text
C& INDEPENDENT REVIEW

Exact commit:
Branch/ref:
Environment:

BUILD/TEST STATUS
...

ARCHITECTURE
strengths:
contradictions:

IMPLEMENTATION FINDINGS
BLOCKER:
HIGH:
MEDIUM:
LOW:

FALSE PASS CASES
...

FALSE FAIL CASES
...

CORRECT INCOMPLETE CASES
...

DOCUMENTATION/SCHEMA DRIFT
...

TEST/EVIDENCE QUALITY
...

LLM-FIRST ASSESSMENT
...

TOP REQUIRED FIXES
1.
2.
...

CURRENT SAFETY CLAIM YOU BELIEVE THE CODE ACTUALLY SUPPORTS
...

VERDICT
DO NOT MERGE / RESEARCH-READY / REVIEW-READY / OTHER

Confidence: 0-100%
```

If you discover a potentially security-sensitive soundness flaw capable of invalidating a published safety claim, do not publish exploit detail publicly; follow `SECURITY.md`.
