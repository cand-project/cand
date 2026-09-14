# Security Policy

C& is security tooling, so analyzer unsoundness can be security-relevant even when it does not execute attacker-controlled code.

Please do **not** file a public issue for a suspected soundness flaw that could cause C& to report unsafe code as satisfying a published safety level. Use GitHub's private vulnerability reporting for this repository when available.

Public issues are appropriate for false positives, unsupported constructs, usability problems, documentation errors, and bugs that do not create a false safety claim.

## Current security posture

The project is in design/early implementation status. No current release should be treated as proof of memory safety unless a release explicitly states the supported safety level, scope, frontend/toolchain versions, trusted contract digests, and evidence status.
