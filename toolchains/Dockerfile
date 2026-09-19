FROM ubuntu@sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-18=1:18.1.3-1ubuntu1 \
    llvm-18=1:18.1.3-1ubuntu1 \
    llvm-18-dev=1:18.1.3-1ubuntu1 \
    libclang-18-dev=1:18.1.3-1ubuntu1 \
    libclang-rt-18-dev=1:18.1.3-1ubuntu1 \
    gcc-13=13.3.0-6ubuntu2~24.04.1 \
    g++-13=13.3.0-6ubuntu2~24.04.1 \
    cmake=3.28.3-1build7 \
    ninja-build=1.11.1-2 \
    git=1:2.43.0-1ubuntu7.3 \
    python3=3.12.3-0ubuntu2.1 \
    python3-jsonschema=4.10.3-2ubuntu1 \
    jq=1.7.1-3ubuntu0.24.04.2 \
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/lib/llvm-18/bin/clang /usr/local/bin/clang \
    && ln -s /usr/lib/llvm-18/bin/clang++ /usr/local/bin/clang++ \
    && ln -s /usr/bin/gcc-13 /usr/local/bin/gcc \
    && ln -s /usr/bin/g++-13 /usr/local/bin/g++ \
    && ln -s /usr/bin/gcc-13 /usr/local/bin/cc

WORKDIR /src/cand
