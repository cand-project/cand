# C&1 False-PASS Response Policy

This policy applies to the C&1/v1 qualification boundary. A false PASS is a
soundness incident, not an ordinary test failure.

## Incident definition

A false PASS is a confirmed ownership, lifetime, borrow, generation, transport,
policy, evidence, or toolchain case in the documented C&1/v1 checked scope for
which `cand1` emits authoritative PASS while an applicable rule is violated or
the evidence authority is bypassed.

## Required response

1. Mark the finding BLOCKER and stop claim activation.
2. Suspend the public C&1 claim and invalidate affected evidence/release
   identities.
3. Preserve the exact source, policy, toolchain, verifier binary, evidence, and
   reproducer used to confirm the incident.
4. Minimize the case and identify the root cause and affected profile/release
   range.
5. Add a permanent regression, including a known-safe control where practical.
6. Fix the implementation or narrow the normative claim; never weaken the test
   or turn the case into unsupported merely to restore PASS.
7. Rerun the affected family and the complete exact-head C&1-E gate.
8. Restore the claim only after independent review, required CI, fresh evidence,
   and exact-head approval succeed.

The incident record must state the reproducer, root cause, fix, regression,
affected evidence, and post-fix qualification result. GitHub review is merge
authority; it is not semantic proof authority.
