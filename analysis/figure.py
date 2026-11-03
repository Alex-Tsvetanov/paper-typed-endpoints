# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""results/figures/compile-ratio.pdf (added 2026-11-03, design/revision-log.md): the ratio of median
whole-build times, reflection generator over protoc, against the schema size N on a log2 axis, one
line per shape and OS, each point with its 95% BCa interval as a whisker drawn under the marker (at
the campaign's values every interval is shorter than its marker, so the caption must say so). The
secondary curve's point is hollow; the legend calls it the raised-limit build, the paper's wording.
It reads summary.json only. The PDF is written by hand (PDF 1.4, vector paths, the standard
Helvetica font, not embedded, no compression, no dates), so it needs nothing outside the standard
library and the same summary gives the same bytes.

  python -I analysis/figure.py results/summary.json results/figures/compile-ratio.pdf
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

WIDTH, HEIGHT = 360.0, 250.0
LEFT, RIGHT, BOTTOM, TOP = 50.0, 12.0, 38.0, 10.0
PAD = 10.0
SIZES = (16, 32, 64, 128, 256, 512, 1024, 2048, 4096)
COLOUR = {"wide": (0.0, 0.447, 0.698), "many": (0.835, 0.369, 0.0)}  # Okabe and Ito's blue and vermillion
DASH = {"linux": "[] 0 d", "windows": "[4 2] 0 d"}
OS_WORD = {"linux": "Linux", "windows": "Windows"}
SHAPE_WORD = {"wide": "wide schema", "many": "many-message schema"}
# Helvetica's advance widths (thousandths of an em) for the characters the labels use; others count 556.
ADVANCE = dict(zip("0123456789.,/()- ", [556] * 10 + [278, 278, 278, 333, 333, 333, 278]))
ADVANCE |= dict(zip("abcdefghijklmnopqrstuvwxyz",
                    [556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556,
                     333, 500, 278, 556, 500, 722, 500, 500, 500]))
ADVANCE |= dict(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                    [667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778,
                     722, 667, 611, 722, 667, 944, 667, 667, 611]))


def width(text: str, size: float) -> float:
    return sum(ADVANCE.get(ch, 556) for ch in text) * size / 1000


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def text(x: float, y: float, s: str, size: float = 7.0) -> str:
    return f"0 g BT /F1 {size:g} Tf {x:.2f} {y:.2f} Td ({escape(s)}) Tj ET\n"


def marker(os_name: str, x: float, y: float, rgb: tuple, hollow: bool) -> str:
    """A filled circle for Linux and a filled square for Windows; hollow (white inside) for the
    raised-limit build. The caller sets the stroke colour."""
    fill = "1 1 1 rg" if hollow else "{:.3f} {:.3f} {:.3f} rg".format(*rgb)
    if os_name == "windows":
        path = f"{x - 2.2:.2f} {y - 2.2:.2f} 4.40 4.40 re"
    else:
        r, k = 2.4, 2.4 * 0.5523
        path = (f"{x + r:.2f} {y:.2f} m {x + r:.2f} {y + k:.2f} {x + k:.2f} {y + r:.2f} {x:.2f} {y + r:.2f} c "
                f"{x - k:.2f} {y + r:.2f} {x - r:.2f} {y + k:.2f} {x - r:.2f} {y:.2f} c "
                f"{x - r:.2f} {y - k:.2f} {x - k:.2f} {y - r:.2f} {x:.2f} {y - r:.2f} c "
                f"{x + k:.2f} {y - r:.2f} {x + r:.2f} {y - k:.2f} {x + r:.2f} {y:.2f} c h")
    return f"{fill} [] 0 d 0.6 w {path} B\n"


def series(summary: dict) -> dict:
    """{(os, shape): [(n, ratio, low, high, secondary)]} for every cell whose ratio exists."""
    out: dict = {}
    for c in summary["compile"]["cells"]:
        if c.get("ratio") is not None:
            out.setdefault((c["os"], c["shape"]), []).append(
                (c["n"], c["ratio"], c["low"], c["high"], bool(c.get("secondary"))))
    return {k: sorted(v) for k, v in out.items()}


def scales(data: dict):
    """The y axis's tick range (k0, k1, in steps of 0.2) and the maps from N and from a ratio to
    page coordinates."""
    lows = [p[2] for pts in data.values() for p in pts] + [1.0]
    highs = [p[3] for pts in data.values() for p in pts] + [1.0]
    k0, k1 = math.floor(min(lows) * 5), math.ceil(max(highs) * 5)
    y0, y1 = k0 / 5, k1 / 5
    pw, ph = WIDTH - LEFT - RIGHT, HEIGHT - BOTTOM - TOP

    def xa(n: float) -> float:  # 16 to 4096 on log2, inset from the frame so no marker sits on it
        return LEFT + PAD + (math.log2(n) - 4) / 8 * (pw - 2 * PAD)

    def ya(v: float) -> float:
        return BOTTOM + (v - y0) / (y1 - y0) * ph

    return k0, k1, xa, ya


def content(summary: dict) -> str:
    data = series(summary)
    if not data:
        raise ValueError("summary has no compile ratios")
    k0, k1, xa, ya = scales(data)
    pw, ph = WIDTH - LEFT - RIGHT, HEIGHT - BOTTOM - TOP
    out =[f"0 G 0.6 w [] 0 d {LEFT:.2f} {BOTTOM:.2f} {pw:.2f} {ph:.2f} re S\n"]
    for k in range(k0, k1 + 1):
        y, label = ya(k / 5), f"{k / 5:.1f}"
        out.append(f"0 G 0.6 w {LEFT:.2f} {y:.2f} m {LEFT + 3:.2f} {y:.2f} l S\n")
        out.append(text(LEFT - 4 - width(label, 7), y - 2.4, label))
    for n in SIZES:
        x, label = xa(n), str(n)
        out.append(f"0 G 0.6 w {x:.2f} {BOTTOM:.2f} m {x:.2f} {BOTTOM + 3:.2f} l S\n")
        out.append(text(x - width(label, 7) / 2, BOTTOM - 10, label))
    xt = "Schema size N (fields, log scale)"
    out.append(text(LEFT + pw / 2 - width(xt, 8) / 2, 8, xt, 8))
    yt = "Build time ratio, reflection / protoc"
    out.append(f"0 g BT /F1 8 Tf 0 1 -1 0 14.00 {BOTTOM + ph / 2 - width(yt, 8) / 2:.2f} Tm ({escape(yt)}) Tj ET\n")
    out.append(f"0.55 G 0.5 w [1 2] 0 d {LEFT:.2f} {ya(1):.2f} m {LEFT + pw:.2f} {ya(1):.2f} l S [] 0 d\n")
    for (os_name, shape), pts in sorted(data.items()):
        rgb = COLOUR[shape]
        stroke = "{:.3f} {:.3f} {:.3f} RG".format(*rgb)
        for n, _, low, high, _ in pts:
            x = xa(n)
            out.append(f"{stroke} [] 0 d 0.6 w {x:.2f} {ya(low):.2f} m {x:.2f} {ya(high):.2f} l "
                       f"{x - 1.5:.2f} {ya(low):.2f} m {x + 1.5:.2f} {ya(low):.2f} l "
                       f"{x - 1.5:.2f} {ya(high):.2f} m {x + 1.5:.2f} {ya(high):.2f} l S\n")
        path = " ".join(f"{xa(n):.2f} {ya(r):.2f} {'m' if i == 0 else 'l'}" for i, (n, r, *_) in enumerate(pts))
        out.append(f"{stroke} {DASH[os_name]} 1 w {path} S\n")
        for n, r, _, _, secondary in pts:
            out.append(f"{stroke} " + marker(os_name, xa(n), ya(r), rgb, secondary))
    lx, ly = LEFT + 8, BOTTOM + ph - 12
    for i, (os_name, shape) in enumerate((o, s) for o in ("linux", "windows") for s in ("wide", "many")):
        y, rgb = ly - 10 * i, COLOUR[shape]
        stroke = "{:.3f} {:.3f} {:.3f} RG".format(*rgb)
        out.append(f"{stroke} {DASH[os_name]} 1 w {lx:.2f} {y:.2f} m {lx + 18:.2f} {y:.2f} l S\n")
        out.append(f"{stroke} " + marker(os_name, lx + 9, y, rgb, False))
        out.append(text(lx + 24, y - 2.4, f"{OS_WORD[os_name]}, {SHAPE_WORD[shape]}"))
    y = ly - 40
    out.append("0 G " + marker("linux", lx + 9, y, (0, 0, 0), True))
    out.append(text(lx + 24, y - 2.4, "hollow: raised-limit build"))
    return "".join(out)


def render(summary: dict) -> bytes:
    stream = content(summary).encode("latin-1")
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {WIDTH:g} {HEIGHT:g}] "
         f"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>").encode("ascii"),
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream",
    ]
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % number + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    out += b"".join(b"%010d 00000 n \n" % offset for offset in offsets)
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    summary = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    Path(argv[2]).parent.mkdir(parents=True, exist_ok=True)
    Path(argv[2]).write_bytes(render(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
