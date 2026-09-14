# C& differential proof corpus

This directory contains paired ordinary-C fixtures used to demonstrate the difference between compiler acceptance, runtime detection, C& metadata, and future C& enforcement.

## Current baseline

| Case | Ordinary GCC/Clang | Runtime oracle | Future C& expectation | Corrected fixture |
|---|---|---|---|---|
| `use_after_free_unsafe.c` | accepted | ASan must report a temporal heap access error | pending implementation: `CAND-T002` | `use_after_free_fixed.c` |
| `use_after_free_annotated_unsafe.c` | accepted with production-profile C& metadata | ASan must report the same temporal heap access error | pending implementation: `CAND-T002` | `use_after_free_fixed.c` |
| `double_free_unsafe.c` | accepted | ASan must report repeated deallocation | pending implementation: `CAND-T003` | `double_free_fixed.c` |

The C& expectations are deliberately marked **pending**. These fixtures prove today that ordinary C compilation can accept temporal defects. They do not claim that the C& analyzer already rejects them.

The annotated negative fixture proves a separate architectural property: **C& annotations are not runtime mitigation**. If the C& enforcement stage is bypassed, the annotated program still has the original defect. The intended safety effect comes from rejecting the invalid ownership state before the ordinary production build is admitted.

When the analyzer is implemented, CI will add the missing enforcement leg:

```text
unsafe fixture -> cand check -> expected stable diagnostic -> build gate closed
fixed fixture  -> cand check -> pass -> ordinary build/test
```

The proof model is normative in `docs/adr/ADR-0002-differential-safety-evidence.md` and the non-interference rule is defined by `docs/adr/ADR-0004-annotation-noninterference.md`.
