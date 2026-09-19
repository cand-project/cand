#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import tempfile
import time


def run(cand, functions):
    source = "#include <stdlib.h>\n#include <cand/cand.h>\n"
    for i in range(functions):
        source += (
            f"static void f{i}(void) {{ int *p CAND_OWN = malloc(sizeof *p); "
            "if (p != NULL) free(p); }\n"
        )
    with tempfile.TemporaryDirectory(prefix="cand1-heap-") as directory:
        path = pathlib.Path(directory) / "workload.c"
        path.write_text(source)
        start = time.perf_counter()
        result = subprocess.run(
            [cand, "check", "--level=cand1", "--format=json", str(path), "--",
             "-std=c11", "-Iinclude"],
            capture_output=True, text=True, cwd=pathlib.Path(__file__).parents[3]
        )
        elapsed = time.perf_counter() - start
        report = json.loads(result.stdout)
        if result.returncode != 3 or report.get("result") != "incomplete":
            raise SystemExit(f"{functions}: unexpected result {result.returncode}: {report}")
        if report.get("findings"):
            raise SystemExit(f"{functions}: unexpected findings: {report['findings']}")
        return elapsed, report["coverage"].get("functions_analyzed", 0)


if __name__ == "__main__":
    cand = sys.argv[1]
    for count in (1000, 10000):
        seconds, analyzed = run(cand, count)
        print(json.dumps({"functions": count, "analyzed": analyzed, "wall_seconds": round(seconds, 3)}))
