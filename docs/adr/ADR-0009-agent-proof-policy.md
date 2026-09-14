# ADR-0009: Agents May Repair Code but Must Not Silently Weaken Proof Policy

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** agent workflows, CI policy, unsafe/suppression governance
- **Depends on:** ADR-0005, ADR-0006, ADR-0008

## Context

An autonomous coding agent is goal-seeking. If its task is “make CI green,” it may discover that changing the safety policy is easier than repairing the implementation.

Examples include:

- adding `CAND_UNSAFE` around a failing region;
- adding or broadening suppressions;
- changing a trusted contract to claim an API does not retain/free memory;
- lowering `cand1` to `cand0`;
- excluding a file/function from checked scope;
- marking an unsupported construct as accepted;
- modifying the proof corpus or expected diagnostics;
- disabling a C& check in CI;
- changing the base policy against which proof coverage is measured.

These can make a build green without making the code safer.

This risk exists with humans too, but machine-generated changes can happen rapidly and at large scale. C& therefore needs a distinction between **implementation repair** and **proof-policy weakening**.

## Decision

C& SHALL classify changes that can weaken, bypass or redefine a safety claim as **proof-policy changes**.

A coding agent MAY propose such changes, but strict agent workflows MUST NOT treat them as ordinary automatic repair.

By default, a generated-code enforcement profile SHALL fail when an unapproved change weakens proof policy relative to the configured base revision/policy.

## Proof-policy surface

The following are proof-policy-sensitive at minimum:

- new or widened unsafe boundaries;
- new or widened suppressions/baselines;
- lower requested safety level;
- reduced checked scope;
- changed unsupported/allowed construct policy;
- modifications to trusted external contracts;
- contract trust-class promotion;
- changes to mandatory proof fixtures/expected diagnostic mappings;
- disabling/removing C& CI gates;
- changes to C& policy configuration;
- changes to evidence-generation requirements;
- changes that convert an error into a warning/ignored diagnostic.

The exact file paths are repository-specific; classification is semantic, not merely path based.

## Safety budget

C& SHOULD support a policy concept informally called a **safety budget**.

A strict policy may state, for example:

```text
new unsafe boundaries:        0
new suppressions:             0
new unsupported functions:    0
safety-level reductions:      0
trusted-contract changes:     require review
checked coverage decrease:    0
```

A project may intentionally choose a different budget, but the budget must be explicit and machine-readable.

## Base-aware policy diff

Agent-mode checking SHOULD compare the proposed tree against a trusted base revision or policy snapshot.

Conceptual interface:

```bash
cand check --agent --base origin/main --level cand1
cand policy diff --base origin/main --format json
```

The output SHOULD separate:

```text
implementation findings
proof-policy changes
coverage changes
contract trust changes
unsupported-scope changes
```

This lets an LLM freely iterate on implementation while a human/reviewer can focus on changes that alter the meaning of “safe.”

## Repair permissions

### Automatically permitted

Subject to normal review/project policy, an agent may automatically:

- edit implementation code;
- add code-generation-neutral ownership annotations;
- add explicit moves/borrows consistent with existing trusted semantics;
- apply semantics-preserving C& fix-its;
- restructure implementation to satisfy existing ownership rules;
- regenerate deterministic derived metadata.

### Requires explicit policy allowance or approval

An agent must not silently:

- add `unsafe` to avoid a diagnostic;
- add a suppression/baseline entry;
- lower safety level;
- shrink checked scope;
- promote an AI-generated contract to trusted;
- weaken an existing trusted contract;
- change expected proof-test results;
- remove evidence/CI gates.

## “Unsafe” is not a fix

When an agent encounters code C& cannot prove, `CAND_UNSAFE` is a valid architectural escape hatch only when the project deliberately accepts that boundary.

It is not an automatic repair for a failing check.

Diagnostics that could be silenced by an unsafe boundary SHOULD report that possibility separately from a true semantic repair.

For example:

```json
{
  "diagnostic": "CAND-T005",
  "repair_class": "SEMANTIC_REPAIR",
  "policy_escape_available": "unsafe-boundary",
  "policy_escape_requires_approval": true
}
```

## Contract changes

An agent MAY propose external ownership contracts because LLMs are useful at reading headers, documentation and call sites.

However:

```text
LLM proposed contract
        |
        v
 candidate / untrusted
        |
   +----+----+
   |         |
verification review
   |         |
   +----+----+
        v
 trusted contract
```

An agent cannot elevate its own proposal into the proof base simply to make its generated code pass.

## Evidence

The C& evidence artifact SHOULD include a proof-policy delta relative to a configured base when operating in agent/review mode.

A clean agent-generated change should be able to state:

```text
C&1 checked scope: unchanged/increased
new unsafe boundaries: 0
new suppressions: 0
trusted contract modifications: 0
unsupported scope: unchanged/decreased
implementation diagnostics: 0
```

This is more meaningful than “the LLM says it fixed the issue.”

## Consequences

### Positive

- prevents the easiest agent shortcut from becoming the dominant behavior;
- makes AI-generated safety regressions easy to review;
- keeps humans focused on high-trust decisions rather than routine code;
- makes C& suitable as a guardrail in autonomous coding loops;
- supports measurable safety improvement rather than merely green CI.

### Negative

- requires base-aware policy analysis;
- contract/policy classification adds implementation complexity;
- some legitimate large migrations will need explicit policy updates;
- projects must decide who/what is authorized to approve policy weakening.

## Invariants

1. Passing by weakening the proof policy is not equivalent to repairing code.
2. Agent-mode output distinguishes implementation repair from proof-policy change.
3. New unsafe/suppression/trust changes are visible.
4. The agent cannot self-approve promotion of its own untrusted contract facts.
5. A release claim records the policy/scope under which it passed.
6. Projects can configure policy, but policy changes are explicit artifacts.

## Acceptance criteria

ADR-0009 is implemented when:

1. C& can classify the minimum proof-policy surface above;
2. agent mode can compare against a base revision/policy;
3. a configured zero-regression safety budget can block new unsafe/suppressions/coverage loss;
4. trusted-contract changes are separately reported;
5. evidence reports include proof-policy delta;
6. test fixtures prove an agent cannot make a failing case pass merely by adding an unapproved unsafe boundary or suppression.
