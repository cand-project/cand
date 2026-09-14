# C& visual identity and project diagrams

The visual identity is intentionally simple: **C& — C with Ownership**.

## Logo

![C& logo](assets/cand-logo.svg)

The connector mark inside the `C` represents references, borrowing and ownership relationships. The ampersand keeps the project identity tied to the idea of **C and ownership**, rather than presenting C& as a replacement language.

## Why C& exists

![Why C& exists](assets/cand-purpose.svg)

C& addresses a specific migration problem: mature C systems already have ownership rules, but those rules usually live in developer knowledge and documentation rather than in machine-checkable contracts. C& adds ownership, borrowing, move/consume semantics, external API contracts and explicit unsafe boundaries while preserving C source files, existing compilers, the C ABI and the existing library ecosystem.

## How C& fits into the build pipeline

![How C& works](assets/cand-pipeline.svg)

C& is an analysis and enforcement stage, not a compiler. It consumes the existing build description, analyzes ownership and borrowing semantics, and either allows the normal build to continue through clang/GCC or fails with diagnostics. C& does not own machine-code generation and does not define a new ABI.

## Canonical assets

- `docs/assets/cand-logo.svg` — primary project logo
- `docs/assets/cand-purpose.svg` — project-purpose diagram
- `docs/assets/cand-pipeline.svg` — build-pipeline architecture diagram

These SVGs are the canonical repository assets because they are scalable, editable, small, and suitable for README, documentation and website reuse.
