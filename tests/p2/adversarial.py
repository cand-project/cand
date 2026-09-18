#!/usr/bin/env python3
"""Small generated negative corpus for the P2 borrow boundary."""

from pathlib import Path
import json
import subprocess
import sys
import tempfile


def program(body: str, compat: Path) -> str:
    return f'''#include <stdlib.h>
#include "{compat}"
typedef struct Header {{ int type; }} Header;
typedef Header Packet;
CAND_RETURNS_OWN Packet *make_packet(void) {{ return malloc(sizeof(Packet)); }}
CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *p) {{ return p; }}
extern void unknown(Header *);
{body}
'''


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: adversarial.py path/to/cand")
    cand = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[2]
    compat = Path(__file__).resolve().parent / "p2_compat.h"
    cases = []

    for i in range(15):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    free(p);
    return h->type + {i};
}}''')
    for i in range(15):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    if ({i} & 1) free(p); else free(p);
    return h->type;
}}''')
    for i in range(10):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    for (int n = 0; n < {i + 1}; ++n) free(p);
    return h->type;
}}''')
    for i in range(10):
        cases.append(f'''struct View {{ Header *h; }};
int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    struct View v = {{ h }};
    free(p);
    return v.h->type + {i};
}}''')
    for i in range(10):
        cases.append(f'''static Header *saved;
int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    saved = h;
    free(p);
    return saved->type + {i};
}}''')
    for i in range(10):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *a CAND_BORROW_MUT = packet_header(p);
    Header *b CAND_BORROW_MUT = packet_header(p);
    return a->type + b->type + {i};
}}''')
    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    Packet *q CAND_OWN = CAND_MOVE(p);
    free(q);
    return h->type + {i};
}}''')
    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    realloc(p, sizeof(Packet) + {i});
    return h->type;
}}''')

    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    free(p);
    goto released;
released:
    return h->type + {i};
}}''')
    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    switch ({i}) {{
    case 0: free(p); break;
    default: free(p); break;
    }}
    return h->type;
}}''')
    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    for (int n = 0; n < 1; ++n) {{
        free(p);
        if ({i} & 1) continue;
        break;
    }}
    return h->type;
}}''')
    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    if ({i} & 1) {{
        if ({i} & 2) free(p);
        else free(p);
    }} else free(p);
    return h->type;
}}''')
    for i in range(5):
        cases.append(f'''int main(void) {{
    Packet *p CAND_OWN = make_packet();
    Header *h CAND_BORROW = packet_header(p);
    void (*callback)(Header *) = unknown;
    free(p);
    callback(h);
    return h->type + {i};
}}''')

    assert len(cases) == 105
    false_pass = []
    with tempfile.TemporaryDirectory(prefix="cand-p2-adversarial-") as directory:
        directory = Path(directory)
        for index, body in enumerate(cases):
            source = directory / f"case_{index:03d}.c"
            source.write_text(program(body, compat), encoding="utf-8")
            result = subprocess.run(
                [str(cand), "check", "--format=json", str(source), "--", "-std=c11", f"-I{root / 'include'}"],
                capture_output=True,
                text=True,
                check=False,
            )
            try:
                report = json.loads(result.stdout)
            except json.JSONDecodeError:
                false_pass.append((index, "invalid-json"))
                continue
            if report.get("result") == "pass":
                false_pass.append((index, report))
    if false_pass:
        print(f"P2 adversarial false PASS: {false_pass[:3]}", file=sys.stderr)
        return 1
    print(f"P2 adversarial corpus: {len(cases)} cases, false PASS: 0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
