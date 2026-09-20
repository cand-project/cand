#!/usr/bin/env python3
"""Fresh ASan differential campaign for independent temporal defects."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from independent import PREFIX, init_workspace


def sources() -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for i in range(8):
        out.append((f"uaf-{i}", f'''
int main(void) {{
    int *owner CAND_OWN = malloc(sizeof(int));
    *owner = {i} + 1;
    int *stale = owner;
    free(owner);
    volatile int sink = *stale;
    return sink;
}}
'''))
        out.append((f"double-free-{i}", f'''
int main(void) {{
    int *owner CAND_OWN = malloc(sizeof(int));
    *owner = {i} + 3;
    int *alias = owner;
    free(owner);
    free(alias);
    return 0;
}}
'''))
        out.append((f"aggregate-stale-{i}", f'''
struct Pair {{ int *value; }};
int main(void) {{
    int *owner CAND_OWN = malloc(sizeof(int));
    *owner = {i} + 7;
    struct Pair pair = {{ owner }};
    free(owner);
    volatile int sink = *pair.value;
    return sink;
}}
'''))
        out.append((f"loop-generation-{i}", f'''
int main(void) {{
    int *stale = 0;
    for (int round = 0; round < 2; ++round) {{
        int *owner CAND_OWN = malloc(sizeof(int));
        *owner = round + {i};
        if (round == 0) stale = owner;
        free(owner);
    }}
    volatile int sink = *stale;
    return sink;
}}
'''))
        out.append((f"wrapper-lifetime-{i}", f'''
static void destroy(int *p) CAND_TAKES {{ free(p); }}
int main(void) {{
    int *owner CAND_OWN = malloc(sizeof(int));
    *owner = {i} + 11;
    int *stale = owner;
    destroy(owner);
    volatile int sink = *stale;
    return sink;
}}
'''))
    return out


def cand_result(cand: Path, work: Path, env: dict[str, str]) -> tuple[str, dict]:
    proc = subprocess.run(
        [str(cand.resolve()), "check", "--agent", "--level=cand1", "--format=json",
         "--base", "origin/main", "--policy", "cand-policy.json", "case.c", "--", "-std=c11"],
        cwd=work, env=env, capture_output=True, text=True, timeout=30, check=False,
    )
    try:
        report = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return "tool-error", {"stderr": proc.stderr[-500:], "returncode": proc.returncode}
    return str(report.get("result", "missing")), report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cand", type=Path)
    args = parser.parse_args()
    defects = sources()
    sanitizer_results: dict[str, int] = {}
    false_passes: list[str] = []
    unconfirmed: list[str] = []
    with tempfile.TemporaryDirectory(prefix="cand1-e-asan-") as name:
        root = Path(name)
        base = init_workspace(root)
        env = os.environ.copy()
        env["CAND_TRUSTED_BASE_SHA"] = base
        for ident, body in defects:
            source = PREFIX + "\n" + body
            (root / "case.c").write_text(source)
            binary = root / "asan"
            compile_proc = subprocess.run(
                ["clang", "-std=c11", "-O0", "-g", "-fsanitize=address,undefined",
                 "-fno-omit-frame-pointer", "case.c", "-o", str(binary)],
                cwd=root, capture_output=True, text=True, check=False,
            )
            if compile_proc.returncode != 0:
                unconfirmed.append(ident + ": compile")
                continue
            run = subprocess.run(
                [str(binary)], cwd=root, env={**env, "ASAN_OPTIONS": "detect_leaks=0"},
                capture_output=True, text=True, timeout=15, check=False,
            )
            stderr = run.stderr.lower()
            if "addresssanitizer" in stderr or "undefinedbehaviorsanitizer" in stderr:
                kind = "confirmed"
            else:
                kind = "unconfirmed"
                unconfirmed.append(ident + ": runtime")
            sanitizer_results[kind] = sanitizer_results.get(kind, 0) + 1
            verdict, report = cand_result(args.cand, root, env)
            if verdict == "pass":
                false_passes.append(ident)
    print(json.dumps({
        "schema": "cand1-e.sanitizer-differential/v1", "cases": len(defects),
        "sanitizer": sanitizer_results, "unconfirmed": unconfirmed,
        "false_passes": false_passes,
    }, sort_keys=True))
    return int(bool(false_passes or unconfirmed))


if __name__ == "__main__":
    raise SystemExit(main())
