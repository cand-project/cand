#!/usr/bin/env python3
"""Deterministic transport corpus: every tracked transport must be non-PASS."""
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).parents[3]

HEADER = """#include <cand/cand.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <setjmp.h>
typedef struct Packet { int value; } Packet;
typedef struct Holder { Packet *packet; } Holder;
typedef struct Nested { Holder holder; } Nested;
typedef union UnionPacket { Packet *packet; void *opaque; } UnionPacket;
static Packet *make_packet(void) { return malloc(sizeof(Packet)); }
static void unknown_pointer(Packet *p);
static void unknown_callback(void *p);
static int out_packet(Packet **p) { *p = make_packet(); return *p != NULL; }
"""

IMPL = [
    "Packet *p CAND_OWN = make_packet(); Packet *q = p; free(p); return q->value;",
    "Holder h = {0}; h.packet = make_packet(); free(h.packet); return h.packet->value;",
    "Nested n = {0}; n.holder.packet = make_packet(); free(n.holder.packet); return n.holder.packet->value;",
    "Packet *a[2] = {make_packet(), NULL}; free(a[0]); return a[0]->value;",
    "Packet *p = make_packet(); Holder h = {.packet = p}; free(p); return h.packet->value;",
    "UnionPacket u = {.packet = make_packet()}; free(u.packet); return u.packet->value;",
    "Packet *p = make_packet(), *q = NULL; memcpy(&q, &p, sizeof q); free(p); return q->value;",
    "Packet *p = make_packet(), *q = NULL; memmove(&q, &p, sizeof q); free(p); return q->value;",
    "Packet *p = make_packet(); void *v = p; free(p); return ((Packet *)v)->value;",
    "Packet *p = make_packet(); uintptr_t raw = (uintptr_t)p; free(p); return ((Packet *)raw)->value;",
    "Packet *p = make_packet(); char *v = (char *)p; free(p); return ((Packet *)v)->value;",
    "Packet *p = make_packet(); char *v = (char *)p + 1; free(p); return *v;",
    "static Packet *s; s = make_packet(); free(s); return s->value;",
    "Packet *p = make_packet(); unknown_pointer(p); free(p); return p->value;",
    "Packet *p = make_packet(); unknown_callback(p); free(p); return p->value;",
    "Packet *p = NULL; out_packet(&p); free(p); return p->value;",
    "Packet *p = make_packet(); (void)printf(\"%p\", (void *)p); free(p); return p->value;",
    "Packet *p = make_packet(); atomic_store((_Atomic(Packet **) *)&p, p); free(p); return p->value;",
    "Packet *p = make_packet(); if (setjmp(*(jmp_buf[1]){0}) == 0) free(p); return p->value;",
    "Packet *p = make_packet(); p = realloc(p, sizeof *p * 2); return p->value;",
]

INDEPENDENT = [
    "Packet *p = make_packet(); Holder h; h.packet = p; free(h.packet); return p->value;",
    "Packet *p = make_packet(); Packet *views[1]; views[0] = p; free(p); return views[0]->value;",
    "Packet *p = make_packet(); UnionPacket u; u.opaque = p; free(p); return ((Packet *)u.opaque)->value;",
    "Packet *p = make_packet(); memmove(&p, &p, sizeof p); free(p); return p->value;",
    "Packet *p = make_packet(); unknown_pointer((Packet *)((void *)p)); free(p); return p->value;",
]


def make_file(snippets, offset):
    body = []
    for i, snippet in enumerate(snippets):
        body.append(f"int case_{offset + i}(void) {{ {snippet} }}\n")
    return HEADER + "\n".join(body)


def run_file(cand, path):
    proc = subprocess.run(
        [cand, "check", "--level=cand1", "--format=json", str(path), "--",
         "-std=gnu11", "-Iinclude"], cwd=ROOT, text=True,
        capture_output=True, check=False)
    try:
        report = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"{path}: invalid JSON: {proc.stdout!r} stderr={proc.stderr!r}") from exc
    if report.get("result") == "pass":
        raise SystemExit(f"false PASS in {path}: {report}")
    if not report.get("findings") and not report.get("unsupported"):
        raise SystemExit(f"transport metadata disappeared in {path}: {report}")
    return report


def run_corpus(cand, snippets, cases, label, directory):
    reports = []
    width = len(snippets)
    for group, start in enumerate(range(0, cases, width)):
        group_snippets = [snippets[(start + i) % width] for i in range(min(width, cases - start))]
        path = directory / f"{label}_{group:03d}.c"
        path.write_text(make_file(group_snippets, start))
        reports.append(run_file(cand, path))
    return reports


def main():
    cand = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="cand1-transport-") as temp:
        directory = pathlib.Path(temp)
        impl = run_corpus(cand, IMPL, 250, "implementation", directory)
        independent = run_corpus(cand, INDEPENDENT, 75, "independent", directory)
        print(json.dumps({
            "implementation_cases": 250,
            "independent_cases": 75,
            "implementation_files": len(impl),
            "independent_files": len(independent),
            "false_pass": 0,
            "metadata_loss": 0,
        }, sort_keys=True))


if __name__ == "__main__":
    main()
