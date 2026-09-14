# ADR-0005: Enforcement Modes and Safety-Claim Lifecycle

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** CLI modes, CI gates, migration, safety claims

## Context

C& must be useful before a large C repository is fully modeled. Requiring strict ownership proof on day one would make adoption impractical, while treating every warning-free scan as “safe” would make the project dishonest.

The project therefore needs explicit operational modes that separate observation, enforcement, and proof evidence.

## Decision

C& SHALL expose distinct analysis/enforcement modes with different claim semantics.

### Observe mode — C&0

Purpose: inventory and migration planning.

Typical command:

```text
cand scan -p build
```

Behavior:

- report inferred owners/borrows and candidate contracts;
- report likely temporal violations;
- report unsupported constructs and unknown boundaries;
- do not block the normal build unless explicitly requested;
- produce coverage metrics.

Claim: **none**. A clean C&0 scan does not mean the code is memory-safe or C&1-safe.

### Enforce mode — C&1 checked scope

Purpose: make temporal ownership rules a build/merge gate for selected scope.

Typical command:

```text
cand check -p build --level cand1 --strict
```

Behavior:

- fail on C&1 diagnostic errors;
- fail closed on unsupported semantics inside the declared checked scope;
- fail on unknown ownership-affecting external boundaries unless explicitly unsafe;
- require trusted contracts for modeled external ownership effects;
- emit checked/unsafe/unsupported/unclassified coverage.

Claim: only the named checked scope may satisfy C&1, and only if the implementation/release evidence for C&1 is itself accepted.

### Evidence mode

Purpose: produce reproducible proof artifacts for CI/releases.

Typical conceptual command:

```text
cand evidence --level cand1 --output evidence.json
```

Behavior:

- run normative positive/negative proof corpus;
- record expected diagnostic coverage;
- record frontend/toolchain/contract versions;
- generate machine-readable checked-scope statistics;
- fail on unexpected accepts in normative negative fixtures;
- fail on unsupported constructs that are required by the advertised profile.

Claim: evidence for a release/profile, not a replacement for project-specific checking.

## Coverage vocabulary

Every analysis report SHALL distinguish at least:

- `checked-safe`: analyzed under the active safety level and passed;
- `checked-error`: analyzed and violated the active safety level;
- `explicit-unsafe`: consciously outside the guarantee;
- `unsupported`: C& cannot model required semantics;
- `unclassified`: not enrolled into the checked scope;
- `suppressed`: diagnostic intentionally hidden/baselined but not proven safe.

Coverage percentages SHALL NOT merge these categories into one misleading “safe” number.

Example:

```text
C&1 scope summary
  checked-safe:      81.4%
  checked-error:      1.2%
  explicit-unsafe:    4.8%
  unsupported:        2.1%
  unclassified:      10.0%
  suppressed:         0.5%

C&1 project-wide claim: NOT ELIGIBLE
reason: unsupported/unclassified scope remains
```

## Incremental enrollment

Projects MAY enroll safety scope by:

- source directory/module;
- translation unit;
- function annotation;
- build target;
- manifest/configuration rule.

The enrollment mechanism must be explicit and version-controlled.

A repository may therefore progress:

```text
C&0 inventory
   -> selected functions C&1
   -> selected modules C&1
   -> subsystem C&1
   -> project profile C&1
```

without claiming the unclassified remainder is safe.

## Unsafe boundaries

An explicit unsafe boundary is a design statement, not a suppression.

Unsafe boundaries SHOULD carry:

- reason/category;
- owner/reviewer;
- external issue/reference where appropriate;
- optional expiry/review date;
- boundary scope;
- the safety properties delegated to the caller/programmer.

C& SHOULD make unsafe-boundary count and churn visible in evidence reports.

## Baselines

Baselines exist for migration convenience only.

A baseline MAY prevent known findings from blocking observe-mode adoption, but:

- it SHALL NOT count as checked-safe;
- strict C&1 scope SHALL NOT silently inherit baselined errors as passing proof;
- new occurrences of a baselined diagnostic SHOULD still be distinguishable from historical ones.

## Safety claim lifecycle

A safety-level claim SHALL progress through these states:

```text
specified
  -> implemented
  -> normative corpus green
  -> external/real-project evidence
  -> release profile accepted
  -> advertised
```

A regression can move the claim backward.

Documentation MAY describe future C&1 semantics before implementation, but MUST label the implementation/evidence status separately.

## Release profile

A release/profile claiming C&1 SHOULD name:

- C& version;
- C&1 SPEC revision;
- supported C language/extension profile;
- supported analysis frontend versions;
- supported annotation header version;
- trusted built-in contract set version/digest;
- unsupported feature list;
- proof corpus revision and result;
- soundness caveats.

## Alternatives considered

### One mode: warnings or errors configurable globally

**Rejected.** Severity knobs do not express whether a scope is actually eligible for a safety claim.

### Treat clean scan as safe

**Rejected.** Missing models/unsupported constructs can produce false confidence.

### Require entire repository to be C&1 before any use

**Rejected.** Incompatible with incremental migration of mature C codebases.

## Consequences

### Positive

- C& can enter large repositories gradually;
- users can distinguish discovery from proof;
- safety marketing is tied to evidence state;
- unsupported/unsafe debt remains measurable;
- migration progress becomes quantifiable without overstating coverage.

### Negative

- reports are more complex than a simple pass/fail tool;
- users must understand scope and evidence status;
- configuration/enrollment becomes a security-relevant input;
- release engineering must maintain profiles and evidence metadata.

## Invariants

1. **C&0 makes no safety claim.**
2. **Only enrolled checked scope can satisfy C&1.**
3. **Unsupported is never safe.**
4. **Suppressed is never safe.**
5. **Explicit unsafe is visible and measurable.**
6. **A safety claim names its release/profile evidence.**
7. **Migration convenience cannot silently weaken strict enforcement.**

## Acceptance criteria

ADR-0005 is implemented when:

1. CLI/reporting distinguishes observe and strict enforce behavior;
2. reports expose all required coverage categories;
3. scope enrollment is version-controlled;
4. unsupported semantics fail closed inside strict scope;
5. baselines cannot be counted as checked-safe;
6. evidence output names the active profile, frontend, contracts, and corpus revision;
7. README/release automation can derive advertised safety status from evidence rather than hand-written claims.
