# C& — C with Ownership

![C& logo](docs/assets/cand-logo.svg)

> **Keep C. Add ownership.**

C& (pronounced **“C and”**) is a compile-time ownership and borrowing safety layer for ordinary C projects. It is designed for systems software that wants stronger temporal memory-safety guarantees **without replacing C, creating a new compiler, forking Clang/GCC, changing the C ABI, or forcing a whole-codebase rewrite**.

C& is deliberately narrower than Rust. It does not redesign C into a new general-purpose language. It makes ownership rules that mature C projects already maintain informally—who owns an allocation, who borrows it, who consumes it, what outlives what, and where responsibility crosses an external API—explicit and machine-checkable.

**Current version:** `0.1.0` — architecture and compatibility baseline. The analyzer is not yet complete, and this version does **not** claim that C&1 temporal ownership safety has been implemented or proven sound.

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

## Pipeline, not compiler

![How C& works](docs/assets/cand-pipeline.svg)

C& has a non-negotiable architecture rule: **no new compiler**. C& **MUST NOT become a compiler fork**. It owns analysis and proof, not machine-code generation.

The canonical pipeline is:

```text
existing C + headers
        |
        v
compile_commands.json
        |
        v
C& analysis / contracts
        |
   pass + fail ----> diagnostics / SARIF / CI failure
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

## External API contracts

Existing libraries do not need to become C& projects. Machine-readable contracts describe ownership effects at API boundaries:

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

Contracts are security-sensitive proof inputs. Strict checking must fail closed on unknown or contradictory ownership effects. Generated or AI-suggested contracts are untrusted until reviewed and accepted.

## Incremental adoption

C& is designed for progressive migration:

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

A baseline or suppression is never proof.

## Architecture authority

The initial design is defined by:

- [ADR-0001 — Pipeline safety layer, not a C compiler](docs/adr/ADR-0001-pipeline-safety-layer.md)
- [SPEC-0001 — Ownership and borrowing semantics](docs/spec/SPEC-0001-ownership-and-borrowing.md)
- [SPEC-0002 — Analysis pipeline and toolchain interoperability](docs/spec/SPEC-0002-analysis-pipeline-and-interoperability.md)
- [SPEC-0003 — External API contract format](docs/spec/SPEC-0003-contract-format.md)
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

C& is an independent open-source systems project intended for any C codebase.

## License

Apache License 2.0.
