# Reviewing C&

Thank you for reviewing C&. This project is intentionally being developed in public and should be treated as a security-sensitive research prototype.

The most valuable review is adversarial: try to make C& say `PASS` for C code that has a temporal ownership/lifetime defect, or show that a documented guarantee is broader than the implementation actually supports.

## Read this first

C& is **not** a new C compiler, and the current project does **not** claim that C is generally memory-safe or that C&1 has been proven sound.

The current release is an early implementation of a fail-closed temporal-lifetime verifier. A `PASS` is scoped to the operations the current analyzer claims to model. Unsupported ownership-relevant semantics must produce `INCOMPLETE`, not `PASS`.

Before reviewing code, read:

1. [`README.md`](README.md)
2. [`docs/SAFETY_CLAIMS.md`](docs/SAFETY_CLAIMS.md)
3. [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md)
4. [`docs/adr/ADR-0001-pipeline-safety-layer.md`](docs/adr/ADR-0001-pipeline-safety-layer.md)
5. [`docs/adr/ADR-0010-trustworthy-pass.md`](docs/adr/ADR-0010-trustworthy-pass.md)
6. [`docs/adr/ADR-0011-cfg-flow-sensitive-ownership.md`](docs/adr/ADR-0011-cfg-flow-sensitive-ownership.md)
7. [`docs/spec/SPEC-0004-machine-agent-protocol.md`](docs/spec/SPEC-0004-machine-agent-protocol.md)
8. [`SECURITY.md`](SECURITY.md)

Open pull requests may contain experimental work that is intentionally not yet part of `main`. Review `main` and each open PR as separate claims.

## What counts as a serious finding

Use this order of severity when reviewing analyzer semantics:

1. **False PASS** — C& reports `PASS` for a program containing an ownership/lifetime violation within semantics that C& encountered but failed to model. This is the highest-priority class.
2. **Proof-boundary bypass** — checked scope, trusted contracts, suppressions, unsafe boundaries, or agent policy can be weakened without being surfaced as a policy change.
3. **Non-deterministic or misleading evidence** — the same input/configuration produces materially different machine verdicts, or evidence omits facts needed to interpret the claim.
4. **False FAIL** — valid code is rejected as a known violation rather than reported `INCOMPLETE`.
5. **Unnecessary INCOMPLETE** — safe/common C cannot yet be modeled. This matters for usability, but is preferable to a false PASS during the founding phases.

A suspected false safety claim may be security-relevant. Follow [`SECURITY.md`](SECURITY.md) rather than opening a public issue if disclosure could expose a soundness flaw before a fix is available.

## Build and baseline checks

A typical Ubuntu/Debian environment needs Clang/LLVM development packages, CMake, Ninja, GCC, Python and Ruby.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
bash scripts/check.sh
```

Where available, also run the differential proof and showcase scripts documented under `tests/` and `examples/`.

Do not infer correctness only from a green CI run. CI demonstrates tested behavior, not general soundness.

## Recommended adversarial methodology

For each candidate program, record three independent observations where practical:

| Oracle | Purpose |
|---|---|
| ordinary GCC/Clang | show that the program remains ordinary C |
| ASan/UBSan or another dynamic oracle | independently expose executed runtime defects |
| `cand` | classify the verifier result as PASS / FAIL / INCOMPLETE |

Classify every case as one of:

- `CORRECT PASS`
- `CORRECT FAIL`
- `CORRECT INCOMPLETE`
- `FALSE PASS`
- `FALSE FAIL`
- `ANALYZER BUG`

A sanitizer is an independent regression oracle, **not** the formal proof engine. Lack of a sanitizer report is not evidence of safety.

## C mechanisms worth attacking

Do not limit tests to `p = malloc(); free(p); *p`.

Useful review targets include:

- aliases created through declarations and assignments;
- branches, joins, loops, early returns, `goto`, `switch`, `break` and `continue`;
- struct/nested-struct fields;
- arrays and multidimensional arrays;
- aggregate copy/initialization/return;
- unions and overlapping storage;
- `memcpy`/`memmove` transport;
- pointer arithmetic and pointer mutation (`p++`, `p += n`);
- pointer/integer casts and provenance changes;
- globals and static locals;
- function parameters, returns and out-parameters;
- unknown/external calls;
- callbacks and retention;
- `realloc`;
- atomics;
- varargs;
- `setjmp`/`longjmp` and other non-local control flow;
- compiler extensions, inline assembly and statement expressions;
- macros and effective build flags.

If C& cannot soundly represent one of these, the desired founding-phase behavior is normally `INCOMPLETE` rather than a guessed `PASS`.

## Annotation non-interference

C& annotations are intended to be analysis metadata. Reviewers should verify that adding C& metadata does not silently change:

- runtime behavior;
- C ABI;
- calling convention;
- data layout;
- ordinary GCC/Clang code generation semantics.

Bypassing `cand check` should not magically repair unsafe C. C& is an acceptance/verifier layer, not a hidden runtime or source-rewriting compiler.

## LLM/agent review

C& assumes generated C is untrusted. An LLM is a synthesis engine, not part of the trusted computing base.

When evaluating agent workflows, explicitly test whether an agent can make CI green by:

- adding or widening unsafe regions;
- adding suppressions/baselines;
- lowering the requested safety level;
- shrinking checked scope;
- modifying trusted contracts;
- modifying expected test outputs;
- deleting failing behavior.

Those are proof-policy changes, not ordinary repairs.

## Submitting review results

For public, non-security findings, open a focused GitHub issue with:

- exact commit SHA;
- compiler/LLVM versions;
- minimal reproducer;
- exact `cand` command and result/exit code;
- ordinary compiler result;
- sanitizer/independent-oracle result if applicable;
- expected classification and why;
- whether the issue is a false PASS, false FAIL, INCOMPLETE gap, tool failure, documentation problem, or policy bypass.

For broader evaluations, the reusable prompts under [`prompts/`](prompts/) provide a common structure. Prompt output is evidence for review, never architectural authority; ADRs, SPECs, contracts and executable tests remain authoritative.

## Review principle

> **Increase the amount of real C that C& can understand without decreasing the meaning of PASS.**

During the founding phases, conservative `INCOMPLETE` results are acceptable. A false `PASS` is not.
