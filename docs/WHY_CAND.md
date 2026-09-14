# Why C& Exists

C& exists for the large body of systems software where C remains the correct interoperability and deployment language, but implicit ownership is no longer an acceptable safety boundary.

It also exists for a newer development model: **C increasingly written and maintained by LLMs/coding agents**.

That shift changes the economics of safer C. Ownership annotations, contract maintenance and repeated repair loops are expensive when every edit is manual. They are much cheaper when a coding agent can emit the ownership intent at generation time, consume deterministic verifier output, revise the code and retry automatically.

The C& proposition is therefore not only “make existing C safer.” It is also:

> **Use LLMs to synthesize low-level C, and use C& as the deterministic verifier that decides whether the generated ownership/lifetime behavior is acceptable.**

## The gap

Mature C code already contains ownership rules. They live in naming conventions, comments, API documentation, reviewer knowledge, allocator conventions, and assumptions about which pointer outlives which other pointer. The compiler generally cannot enforce those rules.

That creates recurring classes of defects such as use-after-free, double-free, confused ownership transfer, freeing borrowed storage, losing the final owner, retaining views beyond their parent object, and unsafe external API boundaries whose ownership behavior is undocumented or misunderstood.

Sanitizers are essential but primarily detect violations at runtime on executed paths. Static analyzers are valuable but often infer ownership heuristically. Rewriting an entire mature C system into Rust can be correct for some components or projects, but it is not a universal migration strategy for existing infrastructure.

C& targets the space between those options.

## The LLM-era opportunity

Traditional safer-C proposals had a usability problem: who is going to add and maintain all of the ownership metadata?

In an LLM-first workflow, the answer can increasingly be: **the same agent that writes the code**.

A coding agent can generate:

- the C implementation;
- ownership annotations;
- move/borrow intent;
- candidate API contracts;
- tests and proof fixtures;
- repairs in response to C& findings.

C& then verifies those claims instead of trusting them.

```text
human requirements
      |
      v
LLM / coding agent
      |
      v
C + C& ownership intent
      |
      v
cand check
      |
  +---+---+
  |       |
reject   prove
  |       |
  v       v
repair   ordinary build/test
  |
  +------> retry
```

This is a synthesis-and-verification architecture:

> **LLMs synthesize. C& verifies.**

The LLM is not trusted. The generated source, annotations, proposed repairs and candidate contracts are all inputs to the verifier.

This is important because an LLM can produce convincing but wrong code. C& gives generated C a deterministic, reviewable ownership contract that does not depend on the model asserting that its own output is safe.

## Why this changes annotation trade-offs

C& still cares about good ergonomics for humans, especially for existing codebases. But the project no longer needs to optimize exclusively for the fewest characters a human must type.

For new machine-generated code, explicit ownership intent can be beneficial even when inference could reconstruct it:

- it gives the verifier clearer expectations;
- it improves diagnostics;
- it makes regeneration more stable;
- it makes human review easier;
- it lets agents repair against explicit proof obligations.

The burden shifts from **typing metadata** to **verifying whether the metadata matches the real program**.

That is a much better fit for automated code generation.

## Why C& must defend against the agent

An agent asked to “make CI pass” may choose the cheapest path rather than the safest path.

It could attempt to:

- add `CAND_UNSAFE`;
- add suppressions;
- lower the safety level;
- reduce checked scope;
- modify a trusted contract;
- change a proof fixture;
- disable a verifier gate.

Therefore C& treats these as **proof-policy changes**, not normal fixes.

A strict agent workflow should be able to enforce a safety budget such as:

```text
new unsafe boundaries:      0
new suppressions:           0
safety-level reductions:    0
checked coverage decrease:  0
trusted contract changes:   review required
```

This means an LLM can iterate aggressively on implementation code while the human focuses on the changes that alter the meaning of the safety claim.

## The proposition

> **Keep C. Add ownership.**

C& adds an explicit, checkable ownership and borrowing contract to ordinary C projects while preserving the existing compiler, ABI, data layout, build system and library ecosystem.

C& does not generate machine code. The project's compiler remains GCC, Clang, or another supported C compiler. C& runs earlier in the pipeline, analyzes the real translation units, and either proves the configured C& safety contract or fails with diagnostics.

```text
source + headers + compile commands + ownership contracts
                         |
                         v
                    C& analysis
                         |
                  pass / fail closed
                         |
                         v
               existing C compiler
                         |
                         v
                    normal binary
```

## Why not a new safer C compiler?

A compiler fork would make adoption harder and would force C& to inherit a permanent code-generation/toolchain maintenance burden. It would also require GCC-based projects to change compilers before receiving value.

C& deliberately owns only the safety contract and analysis layer. Clang's parser/AST/LibTooling can be an analysis frontend without becoming the production compiler or a forked language implementation.

This separation becomes even more valuable with coding agents: the agent can generate ordinary C for the existing ecosystem while C& remains an independent proof gate.

## Why the name C&?

The ampersand is already meaningful in C as the address/reference operator, and references/borrowing are central to the safety model. The name therefore communicates an evolution of C around ownership rather than an unrelated replacement language.

The canonical ASCII identifier is `cand`.

## Adoption model

C& has two first-class adoption modes.

### Existing code

Observation -> checked scopes -> strict scopes.

Unchecked, unsupported, unsafe and baselined regions remain explicitly visible in reports; they are never silently counted as proven safe.

### New LLM-generated code

Generate strict C + ownership intent -> verify -> repair loop -> evidence -> ordinary compiler/tests.

For new generated components, C& should encourage strict checking from the first commit rather than treating ownership as a retrofit.

## What humans review

The human role does not disappear. It becomes more focused.

Humans should primarily review:

- architecture and requirements;
- trusted external contracts;
- unsafe boundaries;
- suppressions/baselines;
- safety-level or checked-scope changes;
- unsupported constructs;
- semantic repairs with behavioral impact.

Routine ownership annotation can increasingly be machine-maintained.

## Credibility

The project's credibility depends on three things at once:

1. a defensible safety claim;
2. realistic adoption cost;
3. a trustworthy agent-verification loop.

C& therefore measures false positives, annotation burden, analysis performance, unsupported constructs, proof-policy regressions and agent repair convergence alongside bug detection.

The long-term opportunity is not to make probabilistic code generation itself trustworthy. It is to make generated low-level C pass through a deterministic ownership verifier before the existing compiler is allowed to build it.
