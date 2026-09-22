#!/usr/bin/env python3
"""Generate a measurement-only amalgamation of a set of C translation
units (physical concatenation, no content modification) plus a JSON
line-offset table for mapping locations back to the original files.

Used by scripts/pilots/unity_counterfactual.py (milestone #42 Gate A
evidence).  The amalgam is a build product: never commit it, never ship
it.  Place it in the same directory as the sources it concatenates so
that quoted #include resolution matches the original per-TU builds.
"""

from __future__ import annotations

import argparse
import json
import os
import re


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="amalgam .c path to write")
    ap.add_argument("--file", action="append", required=True,
                    help="source file, relative to the amalgam's directory")
    ap.add_argument("--order", choices=["sorted", "preserve"], default="sorted",
                    help="segment order: sorted by path (default) or preserve "
                         "the --file order (use to place macro-heavy files last)")
    args = ap.parse_args()

    out = os.path.realpath(args.out)
    base = os.path.dirname(out)
    files = []
    for f in args.file:
        path = os.path.realpath(os.path.join(base, f))
        files.append((f, path))
    if args.order == "sorted":
        files.sort(key=lambda kv: kv[1])

    lines = ['/* amalgamated measurement file - generated, do not distribute */\n']
    offsets = []
    current_line = 1  # lines emitted so far
    for rel, path in files:
        text = open(path, errors='replace').read()
        if not text.endswith('\n'):
            text += '\n'
        marker = f'/* ==== BEGIN {rel} ==== */\n'
        offsets.append({'file': rel, 'start': current_line + 2,
                        'lines': text.count('\n')})
        lines.append(marker)
        lines.append(text)
        current_line += 1 + text.count('\n')
    with open(out, 'w') as handle:
        handle.write(''.join(lines))
    offsets_path = re.sub(r'\.c$', '.offsets.json', out)
    with open(offsets_path, 'w') as handle:
        json.dump(offsets, handle, indent=1)
    total = sum(l.count('\n') for l in lines)
    print(f"wrote {out} ({total} lines, {len(files)} files) and {offsets_path}")


if __name__ == "__main__":
    main()
