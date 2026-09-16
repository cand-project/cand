# ADR-0013: Interprocedural ownership summaries

- Status: Accepted (P0.4)
- Date: 2026-09-15

## Decision

C& models a call only through a semantic `FunctionSummary`. A summary is
derived from a supported visible body or supplied by an explicitly trusted
built-in/external contract. Candidate/LLM summaries are not proof input.
Missing, mixed, recursive-unknown, or conflicting behavior is `INCOMPLETE`.

The initial effect language is deliberately small: `returns_owned`,
`returns_borrow_from(argN)`, `destroys(argN)`, `borrows(argN)`,
`takes_ownership(argN)`, and `unknown`. P0.4 enforces owned returns,
borrowed-return ObjectId propagation, borrows, and destroys; unsupported
effects remain incomplete. A trusted `consumes` effect transfers a tracked
object to an unknown caller-visible state: later caller reads or destruction
remain `INCOMPLETE`, without claiming that transfer destroyed the object.

```
function behavior
        +-- verified body-derived summary
        +-- trusted external contract
        +-- built-in trusted model
        +-- unknown -> INCOMPLETE
```

Visible body and trusted contract facts must agree. A disagreement is reported
as `contract-body-conflict` and cannot produce PASS. Only same-translation-unit
bodies are analyzed; declarations outside the TU need a trusted contract.
Summary transfer operates on ObjectIds, not variable names. This is not a full
borrow checker and does not claim retention, concurrency, spatial safety, or
`realloc` semantics.

`--contracts=PATH` consumes the existing SPEC-0003 v1 YAML shape. Malformed or
unsupported contracts are tool errors. No candidate directory is implicitly
trusted. Trust comes from explicitly selecting a contract bundle through the
trusted input option, never from YAML `trust` metadata; self-declared trust and
missing required bundle metadata are rejected. The verifier operator must
protect the option/configuration that selects trusted bundles from an agent
that can edit the candidate worktree. Validated external summaries seed a
bounded body-summary convergence pass, so visible wrappers can compose trusted
external effects; contract/body conflicts are pinned and cannot be overwritten.
The `realloc` model remains unknown even when present in a bundle because its
conditional old/new-object semantics are outside this summary language. The
loader accepts the canonical block-structured ownership subset and rejects
unsupported YAML forms instead of guessing their meaning.
