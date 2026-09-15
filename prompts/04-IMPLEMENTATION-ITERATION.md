# Prompt: Implement the Next C& Iteration Safely

You are implementing the next scoped phase of C& in:

https://github.com/cand-project/cand

Do not treat the task as ordinary feature delivery. C& is verifier infrastructure: widening support while weakening the meaning of `PASS` is a regression.

## 1. Establish authority and baseline

Before changing code:

1. record exact `main` SHA;
2. read `README.md`, `REVIEWING.md`, `docs/SAFETY_CLAIMS.md`, `docs/THREAT_MODEL.md`;
3. read all ADR/SPEC documents directly relevant to the requested phase;
4. inspect open issues/PRs for overlapping work;
5. build and run the full existing suite;
6. confirm the worktree is clean.

Do not begin implementation if the baseline is already failing without first classifying the failure.

## 2. Write the phase contract before implementation

State explicitly:

```text
GOAL
...

NEWLY MODELED SEMANTICS
...

STILL UNSUPPORTED / INCOMPLETE
...

SAFETY CLAIM BEFORE
...

SAFETY CLAIM AFTER
...

NON-GOALS
...

ACCEPTANCE CRITERIA
...
```

If the phase changes architecture or the meaning of PASS, create/update an ADR before implementation.

If it changes normative semantics, update/create the corresponding SPEC.

## 3. Preserve foundational invariants

Unless a reviewed architecture decision explicitly supersedes them:

- no new C compiler/code generator;
- no Clang/GCC fork requirement;
- annotations do not change ABI/runtime semantics;
- unsupported ownership-relevant constructs fail closed;
- false PASS is unacceptable;
- LLM output is untrusted;
- candidate contracts are not trusted automatically;
- proof-policy changes are not ordinary repairs;
- deterministic machine output is a core interface;
- P0/P0.x must not be marketed as proven C&1.

## 4. Implement with explicit semantic abstractions

Avoid accumulating syntax-specific `if` statements in a monolithic analyzer.

Prefer explicit internal concepts such as:

```text
ObjectId
StorageId
PointerRelation
FlowState
FunctionSummary
ContractTrust
ProofPolicy
```

Every modeled operation should have an explicit classification:

```text
SUPPORTED / KNOWN SAFE
KNOWN VIOLATION
UNSUPPORTED / INCOMPLETE
```

Never leave ownership-relevant behavior in an accidental fourth class: "ignored".

## 5. Add positive, negative and incomplete fixtures

For every new semantic capability add:

- safe fixture expected PASS;
- unsafe fixture expected FAIL where the rule can establish a violation;
- unsupported variant expected INCOMPLETE where the model intentionally stops.

Where executable, compare negative fixtures with ASan/UBSan or another independent oracle.

Do not treat sanitizer-clean execution as proof.

## 6. Regression taxonomy

Actively test the new feature through multiple C spellings:

- declaration vs assignment;
- nested expression vs top-level statement;
- branch/loop location;
- local vs field/array storage;
- direct vs alias access;
- macro-expanded form;
- casted form;
- aggregate/union transport where relevant;
- direct vs indirect call.

This is required to avoid AST-shape overfitting.

## 7. Adversarial implementation review

After the implementation tests pass, stop coding and switch roles.

Perform a fresh red-team review of the amended branch.

At minimum search for:

```text
ASan-confirmed temporal violation + cand PASS
unknown ownership semantics + cand PASS
safe program + incorrect cand FAIL
policy weakening reported as ordinary success
schema/output mismatch
nondeterministic result
```

Generate a new adversarial corpus based on mechanisms **not used by the implementation tests**.

When a defect is found:

1. minimize it;
2. classify severity;
3. add permanent regression fixture;
4. fix it;
5. rerun the entire suite;
6. start another review pass.

Do not stop at the first green pass.

## 8. Review-until-clean rule

Continue review/fix cycles until the newest independent/adversarial pass finds no remaining BLOCKER/HIGH issue within the phase's stated claim.

A clean pass does not mean the project is generally sound. It means no additional blocker/high issue was found in the reviewed scope.

## 9. Documentation consistency

Before opening the PR, compare:

- README;
- ADRs;
- SPECs;
- source comments;
- JSON schemas;
- diagnostic contracts;
- roadmap;
- implementation report;
- tests.

Fix stale counts, old SHAs, unsupported claims, and schema drift.

## 10. PR discipline

Create a focused branch and PR.

Do not merge automatically.

PR body must include:

```text
IMPLEMENTATION REPORT

Base main:
Exact HEAD:

GOAL
...

ARCHITECTURE
...

NEWLY SUPPORTED
...

STILL INCOMPLETE
...

TEST/EVIDENCE
Existing suites:
New fixtures:
Differential oracles:
Determinism:

ADVERSARIAL REVIEW
Programs tested:
Review rounds:
False PASS found/fixed:
False FAIL found/fixed:
Remaining BLOCKER:
Remaining HIGH:
Remaining MEDIUM:
Remaining LOW:

LLM NATURAL-APP IMPACT
...

PERFORMANCE
...

SAFETY CLAIM BOUNDARY
...

RECOMMENDATION
MERGE / DO NOT MERGE
```

## 11. Merge gate

Recommend merge only if:

- existing tests are green;
- new semantics have positive/negative/incomplete coverage;
- no known false PASS remains in the stated scope;
- no BLOCKER/HIGH review finding remains;
- machine schema and emitted output agree;
- docs and safety claims match implementation;
- ordinary GCC/Clang production semantics remain unaffected;
- known gaps are recorded honestly;
- CI is green on the exact final HEAD.

If any of those conditions is not met, leave the PR open and report exactly why.

## Final principle

> **Broader coverage is useful only if C& remains more trustworthy after the change than before it.**
