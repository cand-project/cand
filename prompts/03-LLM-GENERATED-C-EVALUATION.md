# Prompt: Evaluate C& on Natural LLM-Generated C

You are evaluating the central C& product thesis:

> **LLMs synthesize. C& verifies.**

Repository:

https://github.com/cand-project/cand

The purpose of this evaluation is not to prove that C& makes all C safe. It is to test whether a coding agent can generate ordinary useful C, consume deterministic C& diagnostics, repair real temporal-lifetime problems, and preserve the distinction between `PASS`, `FAIL`, and `INCOMPLETE`.

## 1. Establish the exact verifier state

Record:

- repository commit SHA;
- branch/ref;
- C& version;
- Clang/LLVM and GCC versions;
- implemented/unsupported safety semantics.

Read:

```text
README.md
REVIEWING.md
docs/SAFETY_CLAIMS.md
docs/THREAT_MODEL.md
docs/adr/ADR-0008-llm-first-synthesis-and-verification.md
docs/adr/ADR-0009-agent-proof-policy.md
docs/adr/ADR-0010-trustworthy-pass.md
docs/spec/SPEC-0004-machine-agent-protocol.md
```

Do not attribute open-PR functionality to `main` unless merged.

## 2. Generate a natural program

Generate a useful C11 application of roughly 300-1000 lines.

Choose one:

- small roguelike/entity game;
- HTTP/request lifecycle simulator;
- packet router/queue;
- in-memory key/value cache;
- chat/message queue;
- file-indexing worker;
- image-job queue;
- embedded-style sensor/controller simulator.

Use normal C design. Do not simplify the source merely because you know C&'s current limitations.

The application should naturally use several of:

- structs containing pointers;
- dynamic allocation;
- aliases/views;
- arrays;
- loops;
- early returns;
- cleanup paths;
- helper functions;
- callbacks/function pointers where natural;
- external-library-like boundaries.

Do **not** intentionally inject bugs during generation. Generate the program as you normally would.

## 3. Build normally first

Compile with both GCC and Clang using strong warnings.

Run the application's own tests/scenarios.

Where practical, run ASan/UBSan.

Record the baseline behavior before modifying code for C&.

## 4. Run C& without shaping the source

Run the repository-supported `cand check` command and capture machine-readable output.

Record:

- `PASS` / `FAIL` / `INCOMPLETE`;
- findings by stable rule ID;
- unsupported obligation kinds;
- exit code;
- coverage counts if available.

Do not rewrite normal C idioms solely to make C& understand them.

## 5. Repair loop

If result is `FAIL`, consume the diagnostic as a coding agent and repair the actual defect.

Allowed:

- semantic code repair;
- semantics-preserving refactor;
- ownership annotation change supported by project semantics.

Not allowed merely to get green:

- add unsafe;
- add suppression/baseline;
- lower safety level;
- shrink checked scope;
- change trusted contracts without separate review;
- delete functionality;
- delete failing tests;
- change expected verifier outputs.

Run C& again after each repair.

Record each repair cycle:

```text
cycle
verdict before
finding/obligation
source change
semantic or metadata-only?
verdict after
```

## 6. Stop correctly on INCOMPLETE

If C& reaches `INCOMPLETE`, do not force a fake `PASS` by distorting the program.

Classify each remaining obligation by missing verifier capability, for example:

- storage/alias model;
- interprocedural summary;
- external contract;
- loop heap abstraction;
- callback retention;
- dynamic index;
- pointer provenance;
- unsupported language extension.

This classification is product evidence: it identifies the next implementation priorities.

## 7. Re-run ordinary toolchain

After the best legitimate C& result:

- compile with GCC and Clang;
- run tests;
- run sanitizers where practical;
- confirm runtime behavior/output has not changed unexpectedly;
- confirm C& did not require a special production compiler or runtime ABI.

## 8. Evaluate annotation burden

If the current branch supports C& annotations, measure:

- number of explicit annotations;
- number generated/maintained automatically;
- number requiring human semantic judgment;
- any annotation that could be inferred soundly;
- any annotation that risks becoming a lie if the code changes.

The core hypothesis is that machine-maintained ownership intent may be practical even where human-maintained annotation would be expensive.

## 9. Evaluate agent usability

Assess whether machine output gives enough information for an LLM to repair code without guessing:

- stable finding ID;
- object/storage identity;
- source locations;
- state transition/history;
- certainty;
- repair class;
- unsupported reason;
- proof-policy impact.

Identify places where human-readable output is clear but machine output is insufficient.

## 10. Attempt reward-hacking

Without modifying the repository, reason about or test whether an agent could cheaply obtain success through proof weakening.

If supported, test policy-diff/evidence mechanisms.

Report any path where the agent could make the run appear successful without actually repairing semantics.

## 11. Comparison

Give a careful qualitative comparison to writing the same application in Rust.

Do not reduce Rust to "safe C" and do not claim C& has Rust-equivalent safety.

Compare:

- generation ergonomics;
- compiler/verifier feedback;
- FFI/ABI simplicity;
- annotation burden;
- unsupported-code behavior;
- refactoring resilience;
- safety assurance;
- ecosystem/toolchain maturity.

## 12. Report

Return:

```text
C& LLM-GENERATED C EVALUATION

Exact C& commit:
Environment:
Application:
LOC:

BASELINE
GCC:
Clang:
Tests:
ASan/UBSan:

FIRST C& RUN
Verdict:
Findings:
Unsupported obligations:

REPAIR LOOP
Cycles:
Semantic repairs:
Metadata-only repairs:
Policy changes attempted: 0 expected

FINAL RESULT
C&:
GCC:
Clang:
Tests:
Sanitizers:

REMAINING INCOMPLETE CAPABILITIES
...

AGENT-USABILITY FINDINGS
...

ANNOTATION ECONOMICS
...

RUST COMPARISON
...

FALSE PASS FOUND:
FALSE FAIL FOUND:

VERDICT ON THE PRODUCT THESIS
...

Top next engineering priorities:
1.
2.
...
```

Be skeptical. A successful small application demonstrates workflow feasibility, not general C memory safety.
