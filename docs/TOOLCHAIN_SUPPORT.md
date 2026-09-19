# C&1/v1 toolchain support

Status: C&1-D qualification target; the public C&1 claim remains disabled.

The first release-qualified profile is deliberately narrow:

| Field | Supported value |
| --- | --- |
| Host | Ubuntu 24.04 reference container |
| Architecture | x86_64 |
| C language | C11, extensions disabled |
| Analyzer frontend | Clang 18.1.3 |
| LLVM | 18.1.3 |
| Production Clang | 18.1.3 |
| Production GCC | 13.3.0 |
| Target | `x86_64-pc-linux-gnu` |
| Sysroot | Ubuntu 24.04 default system headers, identity `ubuntu-24.04-default` |
| Build | CMake 3.28.3, Ninja 1.11.1, `clang++-18` |

The pinned package versions, container digest, and sysroot identity are the
normative contents of
[`toolchains/cand1-v1-linux-x86_64.json`](../toolchains/cand1-v1-linux-x86_64.json).
The Dockerfile uses the same immutable container digest and package versions.

Reference-environment identity from the qualification run:

```yaml
base_manifest_digest: sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4
locally_built_image_id: sha256:b86a0e38dd34290e0281b9bc452636849b669f60958c9eb02c97f4c945db13c3
published_oci_digest: none
```

The base value is the pulled Ubuntu OCI manifest digest. The built value is a
local Docker image identity from this qualification run; this repository does
not publish the derived reference image, so it is not a portable published
OCI digest.

`SUPPORTED` means the exact profile is build- and test-qualified. Other Linux
hosts, architectures, target triples, C standards, LLVM/Clang versions,
GCC versions, sysroots, compiler extensions, modules, plugins, response files,
and explicit external include/sysroot paths are `UNSUPPORTED` for a release
qualified `cand1 PASS`. A future matrix may classify a combination as `TESTED
BUT NOT GUARANTEED`, but none is advertised by this profile.

The verifier embeds the source commit, binary digest, C++ build compiler,
Clang/LLVM identity, target, language mode, sysroot identity, and reference
environment digest in evidence. Evidence replay compares these values with the
running verifier and rejects drift as stale. A cand1 agent check on an
unsupported build environment is fail-closed and cannot produce PASS.

Production compiler compatibility is limited to annotation erasure and ABI
neutrality. The compatibility fixture is compiled and executed with both
qualified GCC and Clang; C& annotations add no runtime dependency and do not
alter layout, alignment, symbols, or program results.

Clean-room reproduction:

```sh
docker build --provenance=false -f toolchains/Dockerfile -t cand1-v1-toolchain .
docker run --rm -v "$PWD":/src/cand -w /src/cand cand1-v1-toolchain \
  bash -c 'git config --global --add safe.directory /src/cand && \
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++-18 && cmake --build build && \
    ctest --test-dir build --output-on-failure && \
    bash tests/toolchain/run.sh build/cand && \
    bash tests/toolchain/reproducible.sh'
```

The container digest and all package versions are pinned; an ordinary host
build is useful for development but is not release qualification evidence.
