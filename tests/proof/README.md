# C& differential proof corpus

This directory contains paired ordinary-C fixtures used to demonstrate the difference between compiler acceptance, runtime detection, and future C& enforcement.

## Current baseline

| Case | Ordinary GCC/Clang | Runtime oracle | Future C& expectation | Corrected fixture |
|---|---|---|---|---|
| `use_after_free_unsafe.c` | accepted | ASan must report a temporal heap access error | pending implementation: `CAND-T002` | `use_after_free_fixed.c` |
| `double_free_unsafe.c` | accepted | ASan must report repeated deallocation | pending implementation: `CAND-T003` | `double_free_fixed.c` |

The C& expectations are deliberately marked **pending**. These fixtures prove today that ordinary C compilation can accept these temporal defects. They do not claim that the C& analyzer already rejects them.

When the analyzer is implemented, CI will add the missing enforcement leg:

```text
unsafe fixture -> cand check -> expected stable diagnostic -> build gate closed
fixed fixture  -> cand check -> pass -> ordinary build/test
```

The proof model is normative in `docs/adr/ADR-0002-differential-safety-evidence.md`.
