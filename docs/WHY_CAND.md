# Why C& Exists

C& exists for the large body of systems software where C remains the correct interoperability and deployment language, but implicit ownership is no longer an acceptable safety boundary.

## The gap

Mature C code already contains ownership rules. They live in naming conventions, comments, API documentation, reviewer knowledge, allocator conventions, and assumptions about which pointer outlives which other pointer. The compiler generally cannot enforce those rules.

That creates recurring classes of defects such as use-after-free, double-free, confused ownership transfer, freeing borrowed storage, losing the final owner, retaining views beyond their parent object, and unsafe external API boundaries whose ownership behavior is undocumented or misunderstood.

Sanitizers are essential but primarily detect violations at runtime on executed paths. Static analyzers are valuable but often infer ownership heuristically. Rewriting an entire mature C system into Rust can be correct for some components or projects, but it is not a universal migration strategy for existing infrastructure.

C& targets the space between those options.

## The proposition

> **Keep C. Add ownership.**

C& adds an explicit, checkable ownership and borrowing contract to ordinary C projects while preserving the existing compiler, ABI, data layout, build system and library ecosystem.

C& does not generate machine code. The project's compiler remains GCC, Clang, or another supported C compiler. C& runs earlier in the pipeline, analyzes the real translation units, and either proves the configured C& safety contract or fails with diagnostics.

```text
source + headers + compile commands + ownership contracts
                         |
                         v
                    C& analysis
                         |
                  pass / fail closed
                         |
                         v
               existing C compiler
                         |
                         v
                    normal binary
```

## Why not a new safer C compiler?

A compiler fork would make adoption harder and would force C& to inherit a permanent code-generation/toolchain maintenance burden. It would also require GCC-based projects to change compilers before receiving value.

C& deliberately owns only the safety contract and analysis layer. Clang's parser/AST/LibTooling can be an analysis frontend without becoming the production compiler or a forked language implementation.

## Why the name C&?

The ampersand is already meaningful in C as the address/reference operator, and references/borrowing are central to the safety model. The name therefore communicates an evolution of C around ownership rather than an unrelated replacement language.

The canonical ASCII identifier is `cand`.

## Adoption model

C& must be useful before an entire repository is converted. Adoption proceeds from observation to checked scopes to strict scopes. Unchecked, unsupported, unsafe and baselined regions remain explicitly visible in reports; they are never silently counted as proven safe.

The project's credibility depends on two things at once: a defensible safety claim and realistic adoption cost. C& therefore measures false positives, annotation burden, analysis performance and unsupported constructs alongside bug detection.
