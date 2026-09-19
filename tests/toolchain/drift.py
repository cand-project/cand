#!/usr/bin/env python3
"""Evidence replay must reject every qualified toolchain identity mutation."""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    cand, cwd, evidence_path = map(Path, sys.argv[1:4])
    original = json.loads(evidence_path.read_text(encoding="utf-8"))
    mutations = {
        ("toolchain", "clang_version"): "17.0.0",
        ("toolchain", "llvm_version"): "17.0.0",
        ("toolchain", "target"): "aarch64-linux-gnu",
        ("toolchain", "sysroot"): "foreign-sysroot-v1",
        ("toolchain", "environment_digest"): "0" * 64,
        ("toolchain", "build_compiler_path"): "/tmp/fake-clang++",
        ("toolchain", "cmake_version"): "3.29.0",
        ("toolchain", "ninja_version"): "1.12.0",
        ("frontend", "standard"): "c17",
        ("frontend", "target"): "aarch64-linux-gnu",
    }
    with tempfile.TemporaryDirectory(prefix="cand1-toolchain-drift-") as directory:
        for (section, key), value in mutations.items():
            mutated = copy.deepcopy(original)
            mutated[section][key] = value
            mutated.pop("integrity_sha256")
            payload = json.dumps(mutated, ensure_ascii=False, sort_keys=True, indent=2)
            mutated["integrity_sha256"] = hashlib.sha256(payload.encode()).hexdigest()
            path = Path(directory) / f"{section}-{key}.json"
            path.write_text(json.dumps(mutated, ensure_ascii=False, sort_keys=True, indent=2) + "\n")
            result = subprocess.run(
                [str(cand), "evidence", "verify", str(path)],
                cwd=cwd, capture_output=True, text=True, check=False,
            )
            report = json.loads(result.stdout)
            if result.returncode != 1 or report.get("result") != "stale":
                raise SystemExit(f"toolchain drift was accepted: {section}.{key}: {report}")
    print("toolchain drift replay: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
