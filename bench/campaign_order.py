#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""The seeded run orders of the P5 campaigns (hypotheses.md sections 2 and 3, design D9), written
before the first build or window. Standard library only; deterministic.

Each order's seed is the first 8 hex digits of sha256("P5 campaign <experiment> <os>"), read as an
integer, and drives one random.Random:
  compile  10 replicates; in each, the 18 cells (wide and many at 16 to 4096 fields) in a shuffled
           order, and the two builds of each pair (R and P) in a shuffled order. 360 builds.
           R many 4096 carries ops_limit 67108864 (the secondary curve), every other build 0.
  e2e      10 replicates; in each, the two payloads in a shuffled order, and per payload the four
           arms (R, P, Q, S) in a shuffled order. 80 server runs.

The files under bench/campaign/ are the orders the jobs follow; this script only made them.
Usage: campaign_order.py OUT_DIR   writes compile-order-{linux,windows}.tsv, e2e-order-{linux,windows}.tsv
"""
from __future__ import annotations

import hashlib
import pathlib
import random
import sys

SIZES = (16, 32, 64, 128, 256, 512, 1024, 2048, 4096)
CELLS = [(shape, n) for shape in ("wide", "many") for n in SIZES]
REPLICATES = 10
OPS_LIMIT = 67108864  # R many 4096 only (revision log 2026-10-30)
PAYLOADS = ("small", "medium")
ARMS = ("R", "P", "Q", "S")


def seed_of(label: str) -> int:
    return int(hashlib.sha256(label.encode("ascii")).hexdigest()[:8], 16)


def compile_order(os_name: str) -> tuple[str, list[str]]:
    label = f"P5 campaign compile {os_name}"
    rng = random.Random(seed_of(label))
    lines = ["index\trep\tpair\tshape\tn\tgen\tops_limit"]
    index = 0
    for rep in range(1, REPLICATES + 1):
        cells = CELLS[:]
        rng.shuffle(cells)
        for pair, (shape, n) in enumerate(cells, 1):
            gens = ["R", "P"]
            rng.shuffle(gens)
            for gen in gens:
                index += 1
                ops = OPS_LIMIT if (gen, shape, n) == ("R", "many", 4096) else 0
                lines.append(f"{index}\t{rep}\t{pair}\t{shape}\t{n}\t{gen}\t{ops}")
    return label, lines


def e2e_order(os_name: str) -> tuple[str, list[str]]:
    label = f"P5 campaign e2e {os_name}"
    rng = random.Random(seed_of(label))
    lines = ["index\trep\tpayload\tarm"]
    index = 0
    for rep in range(1, REPLICATES + 1):
        payloads = list(PAYLOADS)
        rng.shuffle(payloads)
        for payload in payloads:
            arms = list(ARMS)
            rng.shuffle(arms)
            for arm in arms:
                index += 1
                lines.append(f"{index}\t{rep}\t{payload}\t{arm}")
    return label, lines


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, make in (("compile", compile_order), ("e2e", e2e_order)):
        for os_name in ("linux", "windows"):
            label, lines = make(os_name)
            head = f"# {label}; seed {seed_of(label)} (sha256 of the label, first 8 hex digits); bench/campaign_order.py"
            path = out / f"{name}-order-{os_name}.tsv"
            path.write_text("\n".join([head] + lines) + "\n", encoding="ascii", newline="\n")
            print(f"{path.name}: {len(lines) - 1} lines, seed {seed_of(label)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
