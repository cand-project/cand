#!/usr/bin/env python3
import json
import pathlib
import subprocess
import tempfile
import time
import sys

ROOT = pathlib.Path(__file__).parents[3]


def run(cand, count):
    source = "#include <cand/cand.h>\n#include <stdlib.h>\ntypedef struct H { int *p; } H;\n"
    source += "typedef struct N { H h[2]; } N;\n"
    for i in range(count):
        source += (
            f"static int f{i}(void) {{ N n = {{0}}; n.h[0].p = malloc(sizeof(int)); "
            "free(n.h[0].p); return *n.h[0].p; }\n"
        )
    with tempfile.TemporaryDirectory(prefix="cand1-transport-bench-") as directory:
        path = pathlib.Path(directory) / "aggregate.c"
        path.write_text(source)
        start = time.perf_counter()
        proc = subprocess.run(
            [cand, "check", "--level=cand1", "--format=json", str(path), "--",
             "-std=c11", "-Iinclude"], cwd=ROOT, capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        report = json.loads(proc.stdout)
        if report.get("result") == "pass":
            raise SystemExit(f"aggregate benchmark produced PASS: {report}")
        return {"functions": count, "wall_seconds": round(elapsed, 3),
                "result": report.get("result"),
                "transport_events": report.get("coverage", {}).get("unsupported_transport_operations", 0)}


if __name__ == "__main__":
    cand = sys.argv[1]
    for count in (1000, 10000):
        print(json.dumps(run(cand, count), sort_keys=True))
