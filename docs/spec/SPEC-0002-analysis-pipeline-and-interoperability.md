# SPEC-0002: Analysis Pipeline and Toolchain Interoperability

- **Status:** Draft
- **Version:** 0.1.0
- **Depends on:** ADR-0001, SPEC-0001

## 1. Purpose

This specification defines how C& integrates into existing C builds without replacing the compiler.

## 2. Commands

The initial CLI SHOULD expose:

```text
cand check
cand contract validate
cand contract explain <symbol>
cand explain <diagnostic-id>
cand baseline create
cand report --format human|json|sarif
```

Exact CLI details may evolve, but `check` and contract validation are mandatory capabilities.

## 3. Pipeline modes

### 3.1 Standalone mode — canonical

```bash
cand check -p build
```

The tool discovers translation-unit compile commands from `compile_commands.json` or an approved build adapter. Standalone mode is the canonical semantic implementation.

### 3.2 Clang plugin mode — optional

For Clang builds, C& MAY expose a plugin loaded into upstream Clang. It invokes the same semantic analysis library and diagnostics. Plugin mode is for build convenience, not a different safety model.

### 3.3 CI mode

CI mode requires deterministic failure policy:

```bash
cand contract validate
cand check -p build --level cand1 --deny error
```

A checked safety claim is valid only for the exact source/configuration/contracts analyzed.

## 4. Inputs

A C& analysis run is defined by source identity, translation-unit commands, preprocessor/include state, target/data model, C standard mode, frontend/version, safety level, contract digests, C& configuration and any baseline/suppression file. Reports MUST capture enough of these inputs to distinguish materially different analyses.

## 5. Compilation database

The preferred source of build commands is `compile_commands.json`. Build adapters MAY produce equivalent exact command information. C& MUST NOT invent generic compile flags when the real translation unit depends on project-specific definitions or include order.

## 6. Frontend normalization

The initial frontend is Clang LibTooling. GCC command lines may require explicit normalization into equivalent analysis invocations. If GCC-specific preprocessing or language behavior cannot be represented faithfully, the unit is **unsupported**, not “passed.”

## 7. Analysis-only annotations

`include/cand/cand.h` can expose analysis attributes when `CAND_ANALYSIS=1` is defined. The final production build need not define it. These annotations introduce no runtime semantics, ABI change or data-layout change.

## 8. Macro event capture

Intent markers such as `CAND_MOVE(x)` may disappear after preprocessing. The frontend MAY use preprocessor callbacks/source ranges to capture C& macro events before they disappear from the AST. Canonical semantics come from SPEC-0001, not a macro implementation detail.

## 9. Semantic IR

Frontend AST nodes MUST be translated into an internal C& analysis representation before ownership solving. The IR must model function identity, object/storage identity, pointer/reference values, assignments, dereferences/field/index access, calls/returns, allocations/destructions, control flow, ownership capabilities, borrow origins, escapes, aggregate relationships, relevant casts and unsupported constructs.

This prevents Clang AST APIs from becoming the normative C& ownership semantics.

## 10. Function summaries

C& builds interprocedural summaries containing parameter borrow/consume/retain/destroy effects, owned/borrowed returns, out-parameter ownership, escape behavior and unsafe effects. Summaries must be invalidated when source, contract or configuration dependencies change.

## 11. Contract resolution order

Unless a later ADR changes it, ownership effects resolve in this order:

1. explicit project-local source annotation compatible with the SPEC;
2. project contract override;
3. selected vendor/platform contract bundle;
4. C& standard contract bundle;
5. inferred project-local function summary;
6. unknown.

Conflicting explicit sources MUST be an error rather than silently selecting one.

## 12. Incremental adoption modes

C& supports three modes:

- **Observe:** report only, no safety claim.
- **Checked subset:** configured safe functions/modules are checked; legacy neighbors have no claim.
- **Strict:** every function in scope is safe or explicitly unsafe; unknown ownership-affecting boundaries fail closed.

No repository may advertise C&1 coverage greater than its strict checked scope.

## 13. Baselines and suppressions

A baseline may ease adoption but MUST NOT convert findings into proof. Reports must distinguish clean proof, suppressed/baselined violations, unsafe boundaries and unsupported/unanalysed code. Safety badges/claims cannot count suppressed or unsupported code as verified.

## 14. Output formats

Mandatory outputs are human diagnostics and deterministic JSON. SARIF is recommended. Reports should include C& version, safety level, frontend/version, source identity, contract digests, analyzed/unsupported translation units, safe/unsafe functions, baselined findings, errors/warnings and coverage metrics.

## 15. Final compiler interaction

C& success authorizes continuation of the ordinary build; it does not compile the program.

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cand check -p build --level cand1
cmake --build build
```

If the final compiler rejects code that C& parsed, the build fails normally. C& does not emulate code generation.

## 16. GCC support definition

“GCC supported” means the same source is ultimately compiled by an upstream GCC release in the support matrix, C& can analyze the effective semantics sufficiently for the named claim, annotations require no modified GCC, and conformance fixtures prove the supported extension profile. It does not mean every GCC extension is automatically supported.

## 17. Clang support definition

C& can run as standalone analysis or optional plugin using supported upstream Clang releases. A frontend support matrix is mandatory because tooling APIs change across releases.

## 18. Build-system integrations

Priority order:

1. CMake compilation database;
2. Ninja/Make through compilation database generation;
3. Meson;
4. Bazel adapter where exact commands can be exported;
5. kernel/custom-build adapters based on demonstrated demand.

Build adapters discover commands only; they do not implement independent safety semantics.

## 19. Performance requirements

The implementation SHOULD cache unchanged summaries, parallelize translation units, avoid reparsing unchanged dependencies where practical, report wall time/cache effectiveness and support changed-file workflows without weakening full-release proof requirements.

## 20. Security of contracts

Contracts influence proof results and are security-sensitive inputs. They must validate against versioned schemas, malformed/unknown fields fail closed under strict validation, reports bind contract digests, third-party bundles carry provenance/version data, and generated/inferred contracts do not become trusted merely because a heuristic or LLM produced them.

## 21. Determinism

Given identical relevant source, commands, target model, C& version, configuration and contracts, analysis results should be deterministic. Solver nondeterminism must not change pass/fail semantics.

## 22. Compatibility testing

Every supported frontend/final-compiler profile must prove:

```text
source parses under C& frontend
-> C& result matches conformance expectation
-> source compiles under final compiler
-> ABI/layout fixtures remain unchanged by annotations
```

## 23. Non-goals

This SPEC does not define a new package manager, linker, C build system, binary instrumentation platform, automatic C-to-Rust translator, compiler replacement, or complete IDE.
