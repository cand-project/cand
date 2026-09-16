# C& — C with Ownership

![C& logo](docs/assets/cand-logo.svg)

> **Keep C. Add ownership.**

C& (pronounced **“C and”**) is a compile-time ownership and borrowing safety layer for ordinary C projects. It is designed for systems software that wants stronger temporal memory-safety guarantees **without replacing C, creating a new compiler, forking Clang/GCC, changing the C ABI, or forcing a whole-codebase rewrite**.

C& is deliberately narrower than Rust. It does not redesign C into a new general-purpose language. It makes ownership rules that mature C projects already maintain informally—who owns an allocation, who borrows it, who consumes it, what outlives what, and where responsibility crosses an external API—explicit and machine-checkable.

**Current version:** `0.1.0` — architecture and compatibility baseline. The analyzer is not yet complete, and this version does **not** claim that C&1 temporal ownership safety has been implemented or proven sound.

## Core thesis: LLMs synthesize. C& verifies.

C& is designed for a software-development model in which much new C code is generated and iterated by LLMs/coding agents rather than typed line-by-line by humans.

That changes the ownership-safety trade-off significantly. Explicit ownership metadata used to have a human annotation cost. A coding agent can emit and maintain that metadata at generation time, consume deterministic checker diagnostics, repair the implementation, and repeat the loop cheaply.

The intended architecture is therefore:

```text
human intent / architecture
          |
          v
   LLM / coding agent
    synthesis engine
          |
          v
 C + C& ownership intent
          |
          v
       cand check
 deterministic verifier
          |
     +----+----+
     |         |
   reject    prove
     |         |
     v         v
structured    ordinary C build
obligations   tests / sanitizers
     |
     +-------> agent repairs and retries
```

The LLM is **not** part of the trusted computing base. Generated source, annotations, fixes and candidate contracts are all treated as untrusted input to the verifier.

This lets the human role move upward. Instead of manually maintaining every ownership annotation, reviewers can focus on the high-consequence boundaries that actually change the meaning of a safety claim:

- new or widened `unsafe` regions;
- trusted external API contracts;
- suppressions and baselines;
- safety-level reductions;
- unsupported constructs;
- semantic repairs that change behavior;
- architecture and requirements.

C& is also designed to prevent a coding agent from “making CI green” by weakening the proof. The P0.5 `--agent` mode compares an exact generated-code policy with `origin/main`, pins checked source and frontend arguments, rejects unsafe/suppression markers and untrusted contract substitution, and separates semantic from policy results. A trusted runner must provide `CAND_TRUSTED_BASE_SHA` from outside the agent-controlled change; local agent output is not a CI attestation.
Changing that policy is a proof-policy change, not an implementation repair.

The core loop is model-neutral:

```text
generate -> cand check -> structured finding -> repair -> cand check -> evidence
```

See [ADR-0008 — LLM-first synthesis and verification](docs/adr/ADR-0008-llm-first-synthesis-and-verification.md), [ADR-0009 — Agent proof policy](docs/adr/ADR-0009-agent-proof-policy.md), and [SPEC-0004 — Machine-Agent Verification Protocol](docs/spec/SPEC-0004-machine-agent-protocol.md).

## Why C&

![Why C& exists](docs/assets/cand-purpose.svg)

Large operating systems, hypervisors, storage engines, databases, networking stacks, firmware, and embedded projects often contain years or decades of C plus platform-specific behavior and C ABI dependencies. Rewriting all of that into another language can be sensible for selected components, but it is often not a realistic universal migration strategy.

Well-written C already has implicit ownership rules such as:

- this pointer owns the allocation;
- this API consumes ownership;
- this return value borrows from argument 0;
- this callback retains the context until unregister;
- this view must not outlive its parent object;
- this destructor ends the object's lifetime.

C& turns those rules into contracts that tooling and CI can check.

```c
Packet *packet CAND_OWN = packet_new();
packet_send(CAND_MOVE(packet));

/* C&1 should reject any later owner use of packet. */
```

For legacy code, C& can infer and progressively classify these rules. For newly generated code, the agent should emit ownership intent from the start and operate under a strict generation profile wherever practical.

## Pipeline, not compiler

![How C& works](docs/assets/cand-pipeline.svg)

C& has a non-negotiable architecture rule: **no new compiler**. C& **MUST NOT become a compiler fork**. It owns analysis and proof, not machine-code generation.

The canonical pipeline is:

```text
existing/generated C + headers
        |
        v
compile_commands.json
        |
        v
C& analysis / contracts
        |
   pass + fail ----> diagnostics / JSON / SARIF / CI failure
        |
        v
ordinary project build
        |
   clang / gcc / existing toolchain
        |
        v
normal C ABI binary
```

The first frontend uses upstream Clang tooling APIs to parse and understand C. That does **not** make Clang the required production compiler. GCC-built projects can use C& where the analysis frontend can faithfully model the effective C semantics.

C& annotations are analysis metadata. They MUST NOT alter runtime behavior, calling convention, data layout, or generated code.

## Source model

C& avoids new C grammar. The portable vocabulary is provided by `include/cand/cand.h`:

```c
#include <cand/cand.h>

Packet *packet_new(void) CAND_RETURNS_OWN;
void packet_send(Packet *packet CAND_TAKES);
void run(void) CAND_SAFE;

void run(void)
{
    Packet *packet CAND_OWN = packet_new();
    packet_send(CAND_MOVE(packet));
}
```

Outside an analysis invocation, annotations reduce to code-generation-neutral no-ops and `CAND_MOVE(x)` remains the ordinary expression `(x)`.

In an LLM-first workflow these annotations are not primarily manual ceremony. They are machine-maintained ownership intent that the verifier checks against actual control/data flow.

## Safety levels

C& does not use “memory-safe C” as an unqualified promise. Safety claims are explicit and scoped:

| Level | Meaning | Status in 0.1.0 |
|---|---|---|
| **C&0** | Observe/report only | baseline vocabulary defined |
| **C&1** | Temporal ownership safety | specified, **not yet implemented as a soundness claim** |
| **C&2** | Spatial safety | reserved |
| **C&3** | Concurrency safety | reserved |

C&1 is intended to cover ownership, moves, destruction, and borrow lifetimes in a strict checked scope. It is intended to reject use-after-move, use-after-free, double destruction, destruction through non-owners, invalid borrow lifetimes, conflicting shared/mutable borrow use, and unknown ownership-affecting external calls that lack a trusted contract or explicit unsafe boundary.

C&1 alone does **not** claim general array-bounds safety, arbitrary pointer-arithmetic safety, data-race freedom, integer safety, null-dereference freedom, arbitrary pointer/integer provenance correctness, inline-assembly correctness, or correctness inside explicit unsafe/unsupported regions.

## Result semantics: trustworthy PASS

C& results follow [ADR-0010 — Trustworthy PASS](docs/adr/ADR-0010-trustworthy-pass.md). The rule is:

> **False INCOMPLETE is temporarily acceptable. False PASS is not.**

| Result | Exit | Meaning |
|---|---|---|
| `PASS` | 0 | no known violation **and** no unresolved ownership/lifetime operation in the P0 checked scope |
| `FAIL` | 1 | C& found a known ownership/lifetime violation (e.g. `CAND-T002`, `CAND-T003`) |
| ERROR | 2 | tool/frontend/input failure; never a C& verdict |
| `INCOMPLETE` | 3 | C& encountered ownership/lifetime semantics it cannot currently model (`CAND-U001`: unknown calls, aliases, struct members, array elements, unknown pointer-return ownership, unsupported control flow, …) |

PASS therefore never means merely "no T002/T003 was emitted". Any heap-relevant operation the analyzer encounters but does not model — an untracked `free()` argument, an allocation stored into a struct member or array element, a pointer returned by an unmodelled function — is reported as an explicit unsupported obligation, and the result is INCOMPLETE.

### Flow-sensitive analysis

Since P0.2, ownership state is attached to program points and propagated over the function's control-flow graph (`clang::CFG`) instead of being interpreted in source order ([ADR-0011](docs/adr/ADR-0011-cfg-flow-sensitive-ownership.md)). Ordinary `if`/`else`, early returns, cleanup `goto`, `switch`, `break`/`continue`, `?:`, `&&`/`||` and simple loops are analyzed rather than rejected, and judgments are classified:

```json
{
  "id": "CAND-T002",
  "rule_id": "cand1.no-use-after-death",
  "certainty": "possible",
  "state_before_access": "MaybeDead",
  "state_trace": [
    {"event": "allocation", "state": "Owned"},
    {"event": "conditional_destruction", "state": "MaybeDead"},
    {"event": "access", "state": "MaybeDead"}
  ]
}
```

`certainty: definite` means every represented path reaches the point with the object destroyed; `possible` means at least one does. Aliases, struct members, array elements and supported same-translation-unit effects are tracked. Unknown external effects, unsupported pointer shapes, mixed provenance, and unresolved alias targets remain INCOMPLETE; CFG reachability never converts unknown behavior into PASS.

The analyzer is a deliberately narrow P0 temporal-lifecycle subset. A differential ASan corpus (`tests/differential/`) runs in CI and fails on any `ASan violation + cand PASS` pair. This is P0.1 evidence hygiene, **not** a C&1 soundness claim.

## External API contracts

Existing libraries do not need to become C& projects. Machine-readable contracts describe ownership effects at API boundaries:

P0.4 derives same-translation-unit allocator, destructor, borrow/view, and
read-only parameter summaries. Trusted SPEC-0003 v1 bundles load with
`--contracts=PATH`; visible bodies take precedence and conflicting contracts
fail closed. A `consumes` effect transfers the caller's tracked object to an
unknown state, so subsequent caller use is INCOMPLETE rather than assumed live
or destroyed. Unknown external calls and candidate/LLM proposals remain
INCOMPLETE.

```yaml
- symbol: malloc
  kind: function
  returns:
    ownership: owned
    allocation_family: c-heap
    nullable: true

- symbol: free
  kind: function
  params:
    - index: 0
      effect: destroys
      allocation_family: c-heap
```

Contracts are security-sensitive proof inputs. Strict checking must fail closed on unknown or contradictory ownership effects.

LLMs are expected to be useful at proposing contracts from headers, implementations, documentation and call sites. Those proposals remain **candidate/untrusted** until promoted through an approved trust path. A model cannot make its own generated code pass by inventing a trusted contract.

For P0.4, trust is granted by the verifier operator explicitly supplying a
bundle with `--contracts=PATH`; YAML trust/provenance fields do not grant
authority. Any deployment that lets an agent control this verifier option or
its configuration must protect that authority outside the agent-editable
worktree.

## Incremental adoption

C& supports two complementary worlds.

For existing projects:

```text
legacy / unclassified C
        |
        +-- observed by C&0
        |
        +-- checked functions / files / modules
        |      +-- named safety level
        |      +-- explicit unsafe boundaries
        |      +-- unsupported code remains visible
        |
        +-- normal C ABI to the rest of the program
```

For new LLM-generated code:

```text
requirements
    |
    v
agent generates strict C + ownership intent
    |
    v
cand check
    |
    +-- repair loop until proof obligations resolve
    |
    v
ordinary C build/tests
```

A baseline or suppression is never proof.

## Proof-policy protection for agents

A generated-code workflow must distinguish a **repair** from a **weaker claim**.

C& `--agent` currently enforces the repository's generated policy:

```text
new unsafe boundaries:      0
new suppressions:           0
safety-level reductions:    0
checked coverage decrease:  0
trusted contract changes:   review required
```

An agent may rewrite implementation code to satisfy the existing policy. Policy or trusted-contract changes are surfaced as weakening or `REVIEW_REQUIRED`, never silently counted as a successful repair. Intentional changes require a human-reviewed base-policy update.

## Machine-facing verifier API

Structured output is a core C& interface, not a convenience feature. An agent should not have to scrape human prose.

The implemented P0.5 interface is:

```bash
cand check --agent --base origin/main --policy cand-policy.json \
  --emit-evidence evidence.json examples/ownership.c -- -std=c11 -Iinclude
cand policy diff --base origin/main --format json
cand evidence verify evidence.json
```

The current generated profile supports only `p0-temporal-lifecycle` and the exact compiler arguments pinned by policy. It refuses unsupported safety levels and argument changes. Policy differences are classified as `NO_CHANGE`, `PROOF_STRENGTHENING`, `PROOF_WEAKENING`, or `REVIEW_REQUIRED`.

Agent output keeps `semantic_result` separate from `policy_result`; policy weakening cannot become a verified success. Evidence binds source and project-local included-file contents, executable `cand` binary digest, frontend identity/arguments, trusted base commit, policy digest, checked scope, and used trusted-contract hashes. `cand evidence verify` validates hashes and replays the recorded check. Its SHA-256 is an integrity/reproducibility mechanism, not a signature: CI must run a trusted verifier against a runner-supplied base SHA. Evidence is not a mathematical proof or a universal statement about C memory safety.

Schemas: [policy](contracts/schema/cand-policy.schema.json), [agent-check](contracts/schema/cand-agent-check.schema.json), [evidence](contracts/schema/cand-evidence.schema.json). The executable policy is [cand-policy.json](cand-policy.json).

## Architecture authority

The initial design is defined by:

- [ADR index](docs/adr/README.md)
- [ADR-0001 — Pipeline safety layer, not a C compiler](docs/adr/ADR-0001-pipeline-safety-layer.md)
- [ADR-0008 — LLM-first synthesis and verification](docs/adr/ADR-0008-llm-first-synthesis-and-verification.md)
- [ADR-0009 — Agent proof policy](docs/adr/ADR-0009-agent-proof-policy.md)
- [ADR-0014 — Agent verification evidence and policy authority](docs/adr/ADR-0014-agent-verification-evidence.md)
- [SPEC-0001 — Ownership and borrowing semantics](docs/spec/SPEC-0001-ownership-and-borrowing.md)
- [SPEC-0002 — Analysis pipeline and toolchain interoperability](docs/spec/SPEC-0002-analysis-pipeline-and-interoperability.md)
- [SPEC-0003 — External API contract format](docs/spec/SPEC-0003-contract-format.md)
- [SPEC-0004 — Machine-Agent Verification Protocol](docs/spec/SPEC-0004-machine-agent-protocol.md)
- [Before and after C&](docs/BEFORE_AFTER.md)
- [Why C& exists](docs/WHY_CAND.md)
- [Visual identity and diagrams](docs/VISUALS.md)
- [Implementation roadmap](docs/ROADMAP.md)

Machine-readable project contracts live under `contracts/`.

## Repository layout

```text
include/cand/cand.h       portable source annotations
contracts/                safety, diagnostic, and API contracts
docs/adr/                 architecture decisions
docs/spec/                normative semantic specifications
docs/assets/              canonical project visuals
tests/proof/               differential ordinary-C/C& proof corpus
examples/                  compatibility and semantic fixtures
scripts/check.sh           baseline validation
.github/workflows/         CI enforcement
```

## Development

Run the repository checks with:

```bash
bash scripts/check.sh
```

The 0.1.0 baseline validates contract syntax, SVG syntax, required architecture invariants, version metadata, and compatibility of the annotation surface with ordinary GCC/Clang C11 plus the Clang analysis-annotation profile.

## Project identity

- **Display name:** C&
- **Pronunciation:** C and
- **ASCII identifier:** `cand`
- **Repository:** `cand-project/cand`
- **Header namespace:** `cand/`
- **Contract namespace:** `cand.*`

C& is an independent open-source systems project intended for any C codebase, whether maintained by humans, coding agents, or both.

## License

Apache License 2.0.
