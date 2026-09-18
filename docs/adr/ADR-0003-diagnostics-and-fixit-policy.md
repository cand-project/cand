# ADR-0003: Diagnostics, Inline Remediation, and Safe Fix-It Policy

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** diagnostics, IDE integration, CLI remediation, source edits

## Context

A memory-safety checker that merely says “unsafe” is not enough for practical migration of large C systems. Developers need to know:

- which object is considered the owner;
- where a borrow began;
- where ownership moved or was destroyed;
- which later use makes the path invalid;
- which external contract caused the ownership effect;
- what edit would make the ownership state valid.

At the same time, automatic rewriting is dangerous. Moving a `free()`, changing an API to copy instead of borrow, introducing reference counting, or transferring ownership at a different point can alter program semantics even if it eliminates a memory error. C& is a safety gate, not an autonomous code-rewriting system.

## Decision

C& SHALL provide **inline diagnostics and remediation guidance**, but SHALL distinguish sharply between:

1. detection;
2. explanation;
3. mechanically safe fix-its;
4. semantic repair suggestions;
5. developer-owned design changes.

C& SHALL NOT silently modify source code to make a safety check pass.

The default behavior of `cand check` is read-only.

## Diagnostic structure

Every ownership diagnostic SHOULD provide:

- stable diagnostic ID;
- primary source location;
- ownership object identity/name when available;
- current ownership state;
- state-transition history relevant to the error;
- secondary source locations for owner creation, borrow creation, move/destruction, and invalid use;
- external contract source when a contract caused the transition;
- safety level and checked scope;
- remediation class;
- optional fix-it edits if the remediation is proven mechanical.

Example:

```text
error[CAND-B001]: owner `packet` is destroyed while borrow `header` is still live
  --> src/request.c:91:5
   |
72 | Packet *packet CAND_OWN = packet_new();
   |         ------ owner created here
76 | Header *header CAND_BORROW = packet_header(packet);
   |         ------ borrow created here, depends on `packet`
91 | packet_free(packet);
   | ^^^^^^^^^^^^^^^^^^^ destroys owner here
96 | inspect(header);
   |         ------ borrow remains live through this use

help: end the borrow before destroying `packet`, or move destruction after the final
      use if that ordering preserves the program's intended semantics
remediation: semantic-review-required
```

## Remediation classes

C& SHALL classify proposed repairs into three categories.

### Class A — mechanically safe metadata/refactoring edits

These MAY be applied automatically by `cand fix --safe` when all preconditions are proven.

Examples include:

- adding a C& ownership annotation that has no production-code semantic effect;
- adding `CAND_MOVE(x)` at a call whose trusted contract already states that the parameter consumes ownership, if this only makes existing transfer intent explicit;
- adding a checked-scope marker;
- generating a candidate external ownership contract in a separate review file;
- replacing an equivalent C& annotation spelling with the canonical spelling;
- adding suppression metadata only when the command explicitly requests suppression, never as a safety fix.

Class A edits MUST preserve ordinary runtime semantics and ABI.

### Class B — semantic repair suggestions

These MUST NOT be auto-applied by default.

Examples include:

- moving `free()` after the last use of a borrow;
- removing a duplicate destructor call;
- changing which object owns an allocation;
- copying data instead of borrowing it;
- shortening a borrow scope;
- changing callback retention behavior;
- changing `realloc()` handling;
- introducing cleanup on an error path.

C& MAY render these as IDE code actions or CLI patch previews, but they require explicit developer approval because they may alter observable behavior.

### Class C — architectural ownership changes

C& provides explanation, not an automatic patch.

Examples include:

- introducing reference counting/shared ownership;
- redesigning an API from retained pointer to copied value;
- replacing global aliasing with object lifetime management;
- changing thread ownership/lifetime architecture;
- redesigning intrusive containers or callback registries;
- changing allocator/deallocator families across subsystem boundaries.

## `cand fix` behavior

The initial CLI contract SHOULD be:

```text
cand check                 # read-only diagnostics
cand explain <diagnostic>  # ownership/lifetime trace
cand fix --safe            # apply only Class A edits
cand fix --preview         # show Class A/B candidate patches without writing
```

A future interactive mode MAY allow users to accept individual Class B patches, but those changes remain ordinary source edits reviewed like any other code change.

`cand fix --safe` SHALL fail rather than guess when an edit depends on developer intent.

## IDE integration

C& SHOULD expose diagnostics through LSP and/or a companion language-server process, with SARIF for CI/code-scanning systems.

Inline developer experience SHOULD support:

- squiggle at invalid use/destruction;
- hover showing owner/borrow state;
- “show lifetime path” action;
- “show ownership transfer” action;
- safe code actions for Class A fixes;
- preview-only actions for Class B suggestions;
- link to the external contract that determined call semantics.

The IDE is a presentation layer. It SHALL use the same semantic analysis and stable diagnostic IDs as CLI/CI.

## How C& fixes a problem

C& uses the word **fix** in two different senses and SHALL document them clearly.

### Safety enforcement fix

For checked code, the primary safety mechanism is rejection:

```text
unsafe source
   |
   v
cand check
   |
   X  error: safety contract violated

ordinary production build is not admitted
```

The unsafe binary is prevented from becoming a passing checked build.

### Source repair

The source itself becomes safe only after the ownership violation is repaired:

```text
unsafe source
   |
   +--> C& diagnostic + lifetime trace
             |
             +--> developer / safe fix-it edits source
                          |
                          v
                     cand check PASS
                          |
                          v
                     normal compiler
```

C& does not claim that emitting a diagnostic changes the behavior of the C program.

## Required explanation graph

The analysis core SHOULD be able to emit a minimal causal graph for each temporal diagnostic:

```text
allocation/owner creation
          |
          +------> borrow creation ------> borrow uses
          |
          +------> move / destroy
                         |
                         +------> invalid later event
```

Diagnostics SHOULD report the shortest useful causal path rather than dumping the entire alias graph.

## Contract-generated fixes

External API contracts are proof inputs and therefore security-sensitive.

C& MAY infer or generate **candidate** contracts from headers, implementations, documentation, tests, or AI assistance, but generated contracts SHALL be untrusted until reviewed.

A generated contract MUST NOT become a Class A auto-fix merely because inference confidence is high.

## Suppressions are not repairs

Suppressing a diagnostic does not make code safe.

C& reports SHALL distinguish:

- fixed/passing;
- explicit unsafe boundary;
- suppressed/baselined finding;
- unsupported/unanalysed;
- checked-safe.

`cand fix` SHALL NEVER automatically suppress an error as a way to obtain a passing safety level.

## Alternatives considered

### Automatically rewrite unsafe C until checks pass

**Rejected.** This would make C& a semantic source transformation system and could produce behaviorally incorrect code that merely satisfies the checker.

### Diagnostics only, no remediation

**Rejected.** Adoption cost would be unnecessarily high, especially on large legacy codebases.

### Auto-apply all compiler-style fix-its

**Rejected.** Many ownership repairs encode product/domain intent and are not syntax repairs.

## Consequences

### Positive

- developers get actionable ownership explanations;
- C& remains conservative about semantic changes;
- safe metadata edits can still be automated aggressively;
- IDE and CI behavior remain consistent;
- remediation confidence becomes machine-readable.

### Negative

- some users will expect one-click repair and find C& conservative;
- high-quality causal diagnostics require additional IR/history infrastructure;
- Class B suggestion quality is a substantial engineering problem;
- IDE integration becomes an important part of usability.

## Invariants

1. **Detection is read-only by default.**
2. **No silent semantic rewrite.**
3. **Auto-fixes preserve ordinary runtime semantics.**
4. **Semantic repairs require explicit approval.**
5. **Suppressions are never fixes.**
6. **Every error is explainable through ownership/lifetime history.**
7. **CLI, CI, SARIF, and IDE share diagnostic identity.**
8. **Generated external contracts remain untrusted until reviewed.**

## Acceptance criteria

ADR-0003 is implemented when:

1. each implemented C&1 diagnostic carries a causal ownership/lifetime trace;
2. SARIF and human-readable output identify the same stable diagnostic;
3. `cand explain` can render the relevant owner/borrow state path;
4. at least one Class A fix-it can be safely applied and round-trip through ordinary GCC/Clang;
5. Class B edits are preview/approval-only;
6. CI proves `cand fix --safe` does not change generated production behavior for its supported fix classes;
7. no command silently converts an error into a suppression or unsafe boundary.
