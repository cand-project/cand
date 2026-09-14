# ADR-0001: C& Is a Pipeline Safety Layer, Not a C Compiler

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** repository-wide architecture

## Context

C& aims to add Rust-inspired ownership and borrowing enforcement to C while preserving the C programming and deployment model.

The obvious implementation choices include:

1. fork Clang and add new C syntax/type rules;
2. build a new C-compatible compiler;
3. write a preprocessor that rewrites annotated C;
4. instrument binaries/runtime pointers;
5. implement an external semantic analysis stage integrated with the normal build pipeline;
6. combine an external analysis engine with optional native compiler plugins that perform no code generation.

The project requirement is incremental adoption by existing C codebases. Replacing the compiler would make adoption of the safety tool dependent on adoption of an entire new toolchain and would create permanent compiler-maintenance cost.

## Decision

C& SHALL be implemented as a **semantic analysis and enforcement layer in the C build pipeline**.

C& SHALL NOT:

- implement machine-code generation;
- own linking;
- define a new C ABI;
- require a fork of Clang, GCC, LLVM, or binutils;
- require a new source language parser independent of mature C frontends for production checking;
- require C&-specific object files;
- silently rewrite program semantics in order to make code pass.

The canonical architecture is:

```text
                  source (.c/.h)
                       |
                       v
              build-command discovery
           compile_commands.json / adapter
                       |
                       v
            +-----------------------+
            |   C& analysis engine  |
            |-----------------------|
            | frontend adapter      |
            | semantic C IR         |
            | ownership graph       |
            | CFG/dataflow          |
            | borrow/lifetime model |
            | call summaries        |
            | contract resolver     |
            +-----------+-----------+
                        |
               pass ----+---- fail
                |               |
                |               +--> stable diagnostics
                |                    JSON/SARIF/human
                v
              existing build
          +---------+----------+
          |                    |
        Clang                 GCC
          |                    |
          +---------+----------+
                    |
                native ABI
```

### Initial frontend

The first frontend SHALL use upstream Clang tooling APIs (LibTooling/AST/CFG and, where useful, the Clang Static Analyzer infrastructure) to obtain semantic information about C translation units.

Using Clang as an analysis frontend does not make Clang the required final code generator.

### Optional Clang plugin

C& MAY ship an optional upstream-Clang plugin that runs the same analysis during a Clang compilation and can make the compilation fail on C& diagnostics.

The plugin MUST share the analysis core and contract semantics with standalone mode. It MUST NOT become the only supported way to run C&.

### Future GCC frontend

A GCC plugin/frontend adapter MAY be added later if real projects require analysis of GCC-only constructs that Clang cannot model accurately.

The analysis semantics MUST remain frontend-independent. Clang AST types must not become the canonical C& ownership model.

## Source representation

C& SHALL avoid requiring new grammar.

Ownership intent is represented through one or more of:

- annotations/macros in `include/cand/cand.h`;
- external machine-readable API contracts;
- inference from allocation/deallocation/call behavior;
- configuration declaring checked modules/functions;
- optional pragmas when a frontend supports them.

The normal final compiler must either ignore these constructs safely or see them expanded to normal C/no-ops.

The C& checker may enable analysis-only definitions such as `CAND_ANALYSIS=1` so that Clang-specific `annotate` attributes are visible during the analysis pass without forcing them on a different production compiler.

## Safety boundary

C& distinguishes:

- **legacy/unclassified code:** no safety claim;
- **safe checked code:** must satisfy the active C& safety level;
- **unsafe boundary:** explicit code/API edge where the checker cannot prove the property and the programmer accepts responsibility.

Unchecked legacy code MUST NOT be automatically promoted to a safe claim merely because C& emitted no finding.

## Contract boundary

External functions that affect ownership must have a trusted model before strict safe code may rely on them.

Contracts can describe:

- allocation;
- deallocation;
- ownership transfer;
- borrowed return values;
- lifetime dependence;
- retained callbacks/context;
- out-parameter ownership;
- conditional ownership behavior such as `realloc`.

Unknown ownership-affecting behavior in strict safe code MUST fail closed or require an explicit unsafe boundary.

## Analysis architecture

The implementation SHOULD contain these logical components:

```text
cand CLI / plugin entry
        |
        v
Frontend Adapter
  - Clang AST/CFG initially
  - PP callbacks for CAND_MOVE and analysis markers
        |
        v
C& Semantic IR
  - objects
  - pointer identities
  - ownership capability
  - borrow edges
  - lifetime origins
  - escape/call effects
        |
        +--------------------+
        |                    |
        v                    v
Contract Resolver       Function Summary Builder
        |                    |
        +----------+---------+
                   v
          Interprocedural Solver
                   |
                   v
             Diagnostics
```

The semantic IR is required so C& does not become structurally coupled to one compiler AST forever.

## Build integration

Standalone C& must support compilation databases as the primary integration path.

Typical CMake pipeline:

```text
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...
cand check -p build
cmake --build build
```

CI:

```text
configure
-> cand contract validate
-> cand check --level cand1
-> ordinary build
-> tests
-> sanitizers/fuzzing as independent defense
```

C& does not replace ASan, UBSan, fuzzing, or hardening. Static proof and runtime defenses are complementary.

## Compiler neutrality

The final binary MAY be compiled with GCC even when the analysis frontend is Clang, provided C& can faithfully parse/model the source under the project's language/extension profile.

If the project depends on GCC-only semantics that the active C& frontend cannot represent, C& MUST report the file/profile as unsupported rather than issue a false safety claim.

## Versioning and toolchain compatibility

Clang's internal AST/tooling API is not a stable long-term ABI. Therefore:

- C& releases must publish supported analysis-frontend versions;
- frontend-specific code must be isolated behind adapters;
- the semantic C& IR/contracts/diagnostic IDs must be versioned independently;
- CI must test at least the supported minimum and current frontend versions;
- a frontend upgrade must not silently change ownership semantics without SPEC review.

## Alternatives considered

### A. Fork Clang

**Rejected.**

Benefits:
- easiest path to new keywords and type-system integration;
- full parser/type-checker control.

Costs:
- permanent LLVM rebase burden;
- effectively a new compiler distribution;
- poor GCC compatibility;
- pressure toward language divergence;
- adoption friction inconsistent with the project purpose.

Checked C demonstrates that compiler-fork approaches can implement powerful C extensions, but C& deliberately chooses a different operational trade-off.

### B. New C compiler

**Rejected.**

This would turn C& into a language/compiler project and recreate decades of backend, target, debug-info, extension, optimizer, linker, and platform work unrelated to the ownership problem.

### C. Preprocessor-only checker

**Rejected as the safety engine.**

The preprocessor does not provide sufficient semantic information for aliasing, control flow, aggregate fields, interprocedural effects, or lifetime relationships.

Macros remain useful as portable source annotations only.

### D. Source-to-source transformation

**Rejected as canonical enforcement.**

A transformation pass risks semantic drift and creates a generated-source authority problem. C& may eventually offer refactoring/fix-it tooling, but validation operates on the original program and must not depend on rewriting it into a different dialect.

### E. Runtime-only instrumentation

**Rejected as the primary model.**

Runtime instrumentation catches executed violations but cannot provide the desired compile-time ownership contract and may impose unacceptable overhead in systems code. Runtime instrumentation may be added as an optional debug validation mode.

### F. Static analyzer only, with no annotations/contracts

**Rejected.**

Pure inference cannot reliably recover all ownership intent from arbitrary C. C& prioritizes low annotation burden, not zero annotations at all costs.

## Consequences

### Positive

- existing compilers remain authoritative for code generation;
- existing C ABI/library ecosystem remains usable;
- incremental adoption is practical;
- analyzer releases can move independently from production compiler choice;
- C& can be introduced as CI before being made mandatory;
- safety semantics remain reviewable in SPEC/contracts rather than hidden in a compiler fork.

### Negative

- the checker cannot rely on custom grammar/type-system support;
- macro/annotation ergonomics are less elegant than native keywords;
- Clang tooling APIs require maintenance across versions;
- parsing GCC-specific code through Clang can be incomplete;
- accurate interprocedural alias/lifetime analysis is difficult;
- explicit contracts are unavoidable for some external APIs;
- some soundness properties may require rejecting valid C rather than guessing.

## Invariants

1. **No codegen:** C& never needs to generate machine code to provide its core guarantee.
2. **No compiler fork:** upstream compiler binaries/libraries only.
3. **No ABI change at C&1:** annotations do not alter layout/calling convention.
4. **Fail closed:** unsupported semantics cannot be advertised as checked safety.
5. **Original-source authority:** generated rewrites are never the safety source of truth.
6. **Explicit safety level:** every safety claim names the exact C& level.
7. **Unsafe is visible:** inability to prove is represented explicitly, not silently ignored.
8. **Contracts are versioned:** external ownership semantics are machine-readable and reviewable.
9. **Frontend independence:** Clang is the first parser/backend, not the definition of C& semantics.
10. **Incremental adoption:** legacy and checked C can coexist through the normal C ABI.

## Acceptance criteria

ADR-0001 is implemented when a prototype can:

1. consume a normal C project's compilation database;
2. analyze at least allocation/free, move, borrow and basic interprocedural transfer semantics;
3. reject known temporal violations before compilation;
4. allow the same source to compile with unmodified upstream Clang;
5. allow a supported source subset to compile with unmodified GCC;
6. emit no C&-specific object/binary format;
7. validate external contracts against a schema;
8. produce stable machine-readable diagnostic IDs;
9. clearly report unsupported source/toolchain semantics;
10. demonstrate one existing C library adopting C& incrementally without a compiler replacement.
