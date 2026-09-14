# C& — C with Ownership

> **Keep C. Add ownership.**

C& (pronounced **“C and”**) is a compile-time ownership and borrowing safety layer for ordinary C projects. It is designed for systems software that wants stronger temporal memory-safety guarantees **without replacing C, creating a new compiler, forking Clang/GCC, changing the C ABI, or forcing a whole-codebase rewrite**.

C& is deliberately narrower than Rust. It does not attempt to redesign C into a new general-purpose language. It makes the ownership rules that mature C projects already maintain informally—who owns an allocation, who borrows it, who consumes it, what outlives what, and where responsibility crosses an external API—explicit and machine-checkable.

## Why C&

Large operating systems, hypervisors, storage engines, databases, networking stacks, firmware and embedded projects often contain years or decades of C plus platform-specific behavior and C ABI dependencies. Rewriting all of that into a different language may be sensible for some components, but it is often not a realistic universal migration strategy.

At the same time, well-written C already has an implicit ownership model:

- this pointer owns the allocation;
- this API consumes ownership;
- this return value borrows from argument 0;
- this callback retains the context until unregister;
- this view must not outlive its parent object;
- this destructor ends the object's lifetime.

C& turns that discipline into a contract the build pipeline can enforce.

```c
Packet *packet CAND_OWN = packet_new();
packet_send(CAND_MOVE(packet));

/* C&1 should reject a later owner use after the move. */
```

## Pipeline, not compiler

C& has a non-negotiable architecture rule: **no new compiler**.

C& MUST NOT become a compiler fork. It owns analysis and proof, not machine-code generation.

```text
              existing C source + headers
                         |
                         v
                compile_commands.json
                         |
                         v
             +-----------------------+
             |      C& analysis      |
             |-----------------------|
             | frontend adapter      |
             | semantic C IR         |
             | ownership graph       |
             | borrow/lifetime model |
             | function summaries    |
             | API contract resolver |
             +-----------+-----------+
                         |
                  pass --+-- fail
                    |          |
                    |          +----> diagnostics / CI failure
                    v
               ordinary build
              +------+------+
              |             |
            Clang          GCC
              |             |
              +------+------+
                     |
                     v
                normal C ABI
```

The first frontend uses upstream Clang tooling APIs to parse and understand C. That does **not** make Clang a required production compiler. A GCC-built project can still use C& where the analysis frontend can faithfully model the effective C semantics.

C& annotations are analysis metadata. They MUST NOT alter runtime behavior, calling convention, data layout or generated code. The same checked source remains ordinary C for supported unmodified compilers.

## Initial safety boundary

C& does not use the phrase “memory-safe C” as an unqualified promise.

The project defines explicit safety levels:

| Level | Meaning | Claim |
|---|---|---|
| **C&0** | Observe/report | No safety claim |
| **C&1** | Temporal ownership safety | Ownership, moves, destruction, borrow lifetime for the checked/modelled scope |
| **C&2** | Spatial safety | Reserved until a dedicated SPEC and implementation exist |
| **C&3** | Concurrency safety | Reserved until a dedicated SPEC and implementation exist |

C&1 is intended to reject, for the strict checked scope, errors such as:

- use after move;
- use after destruction/free;
- double destruction;
- destroying through a non-owner;
- destroying/moving an owner while an invalidated borrow is live;
- losing the last required owner;
- returning/storing a borrow beyond its backing object's proven lifetime;
- conflicting mutable/shared borrow use;
- unknown ownership-affecting external calls without an explicit checked contract or unsafe boundary.

C&1 does **not** by itself claim general array-bounds safety, arbitrary pointer-arithmetic safety, data-race freedom, integer safety, null-dereference freedom, arbitrary pointer/integer provenance, inline-assembly correctness, or correctness inside explicit unsupported/unsafe regions.

## Source model

C& avoids new C grammar. The source vocabulary is provided by `include/cand/cand.h`:

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

Outside an analysis invocation, annotations reduce to code-generation-neutral no-ops and `CAND_MOVE(x)` is simply `(x)`.

## External API contracts

C& cannot infer every external library's ownership semantics from a C declaration. Trusted machine-readable contracts model those boundaries without changing the library itself.

Example:

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

Contracts are security-sensitive proof inputs. Strict checking fails closed on unknown or contradictory ownership effects. Suggested or AI-generated contracts are not trusted until explicitly reviewed and accepted.

## Incremental adoption

C& is designed to enter existing repositories progressively:

```text
legacy/unclassified C
        |
        +------ observed by C&0
        |
        +------ checked functions/modules
        |            |
        |            +-- safe under named level
        |            +-- explicit unsafe boundaries
        |            +-- unsupported remains visible
        |
        +------ normal C ABI to the rest of the program
```

A baseline or suppression never becomes proof. C& reports must distinguish checked-safe, explicit unsafe, unsupported/unanalysed and baselined findings.

## Repository authority

The initial project architecture is defined by:

- [ADR-0001 — Pipeline safety layer, not a C compiler](docs/adr/ADR-0001-pipeline-safety-layer.md)
- [SPEC-0001 — Ownership and borrowing semantics](docs/spec/SPEC-0001-ownership-and-borrowing.md)
- [SPEC-0002 — Analysis pipeline and toolchain interoperability](docs/spec/SPEC-0002-analysis-pipeline-and-interoperability.md)
- [SPEC-0003 — External API contract format](docs/spec/SPEC-0003-contract-format.md)
- [Why C& exists](docs/WHY_CAND.md)
- [Implementation roadmap](docs/ROADMAP.md)

Machine-readable project contracts live under `contracts/`.

## Repository layout

```text
include/cand/cand.h       portable source annotations
contracts/                safety/diagnostic/API contract definitions
docs/adr/                 architecture decisions
docs/spec/                normative semantic specifications
docs/ROADMAP.md           evidence-first implementation phases
examples/                  compatibility and semantic fixtures
scripts/check.sh           repository baseline validation
.github/workflows/         CI enforcement
```

## Current status

C& is at the architecture/early implementation baseline. The repository does **not** currently claim that C&1 is implemented or sound. A safety level becomes a product claim only after its SPEC acceptance suite and supported-toolchain evidence are complete.

The first engineering target is therefore intentionally modest: load a real compilation database, build the semantic ownership IR, model allocator/destructor effects, emit stable diagnostics, and prove that the same annotated source continues to compile with unmodified GCC and Clang.

## Development

Run the current repository checks with:

```bash
bash scripts/check.sh
```

The baseline validates contract syntax and proves that the annotation surface is accepted by ordinary GCC/Clang C11 plus the Clang analysis-annotation profile.

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
