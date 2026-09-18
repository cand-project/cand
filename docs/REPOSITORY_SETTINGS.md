# Repository Settings

This file records the intended GitHub governance for `cand-project/cand`. These settings are repository/organization controls, not C& runtime or analyzer semantics.

## Required `main` protection

Create a GitHub ruleset targeting the default branch `main` with:

- restrict deletion of `main`;
- block force pushes;
- require changes through pull requests after the initial repository bootstrap;
- require at least 1 approving review;
- dismiss stale approvals when new commits are pushed;
- require review from CODEOWNERS for paths covered by `.github/CODEOWNERS`;
- require conversation resolution before merge;
- require the `contract-and-compatibility` CI job to pass;
- require the `trusted-agent-attestation` CI job to pass;
- do not allow bypass except organization/repository administrators for emergency recovery;
- keep direct pushes disabled for normal contributors.

## Merge policy

Recommended:

- prefer squash merge for normal feature/fix PRs;
- allow rebase merge where preserving a deliberately structured commit series is useful;
- avoid merge commits unless a future governance decision requires them.

## Security settings

Enable where available:

- private vulnerability reporting;
- dependency graph;
- Dependabot security alerts;
- secret scanning and push protection;
- code scanning once implementation code is substantial enough to produce useful results.

## Repository metadata

Recommended description:

> C& — compile-time ownership and borrowing safety for C, without replacing the compiler.

Recommended topics:

`c`, `memory-safety`, `ownership`, `borrow-checker`, `static-analysis`, `systems-programming`, `clang`, `gcc`, `security`

## Claim discipline

Repository settings must never turn a green CI run into a stronger product claim than the accepted C& safety level and evidence support. Branch protection protects the development process; it is not memory-safety proof.
