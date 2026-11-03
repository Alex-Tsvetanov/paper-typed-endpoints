#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""The seeded run orders of the P5 campaigns (hypotheses.md sections 2 and 3, design D9), written
before the first build or window. Standard library only; deterministic.

Each order's seed is the first 8 hex digits of sha256("<prefix> <experiment> <os>"), read as an
integer, and drives one random.Random. The prefix is "P5 campaign" for the first campaign
(bench/campaign/) and "P5 replication" for the replication of hypotheses-r2.md (bench/campaign-r2/),
whose new labels give new seeds:
  compile  10 replicates; in each, the 18 cells (wide and many at 16 to 4096 fields) in a shuffled
           order, and the two builds of each pair (R and P) in a shuffled order. 360 builds.
           R many 4096 carries ops_limit 67108864 (the secondary curve), every other build 0.
  e2e      10 replicates; in each, the two payloads in a shuffled order, and per payload the four
           arms (R, P, Q, S) in a shuffled order. 80 server runs.
  mixed    (replication only, Linux; hypotheses-r2.md section 4; label "P5 mixed e2e linux")
           10 replicates; in each, the four arms in a shuffled order. 40 server runs.
  oha      (hypotheses-m3.md, Linux; label "P5 oha e2e linux") as e2e: 80 server runs.
  grpc     (hypotheses-m2.md, Linux; label "P5 grpc e2e linux") as e2e with a fifth arm, G: 100 runs.

The files under bench/campaign/ are the orders the jobs follow; this script only made them.
Usage: campaign_order.py OUT_DIR        writes compile-order-{linux,windows}.tsv, e2e-order-{linux,windows}.tsv
       campaign_order.py --r2 OUT_DIR   writes the replication's four orders and mixed-order-linux.tsv
       campaign_order.py --m3 OUT_DIR   writes oha-order-linux.tsv
       campaign_order.py --m2 OUT_DIR   writes g-order-linux.tsv
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


def compile_order(os_name: str, prefix: str = "P5 campaign") -> tuple[str, list[str]]:
    label = f"{prefix} compile {os_name}"
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


def e2e_order(os_name: str, prefix: str = "P5 campaign", arm_set: tuple[str, ...] = ARMS) -> tuple[str, list[str]]:
    label = f"{prefix} e2e {os_name}"
    rng = random.Random(seed_of(label))
    lines = ["index\trep\tpayload\tarm"]
    index = 0
    for rep in range(1, REPLICATES + 1):
        payloads = list(PAYLOADS)
        rng.shuffle(payloads)
        for payload in payloads:
            arms = list(arm_set)
            rng.shuffle(arms)
            for arm in arms:
                index += 1
                lines.append(f"{index}\t{rep}\t{payload}\t{arm}")
    return label, lines


def mixed_order() -> tuple[str, list[str]]:
    label = "P5 mixed e2e linux"
    rng = random.Random(seed_of(label))
    lines = ["index\trep\tarm"]
    index = 0
    for rep in range(1, REPLICATES + 1):
        arms = list(ARMS)
        rng.shuffle(arms)
        for arm in arms:
            index += 1
            lines.append(f"{index}\t{rep}\t{arm}")
    return label, lines


def write(path: pathlib.Path, label: str, lines: list[str]) -> None:
    head = f"# {label}; seed {seed_of(label)} (sha256 of the label, first 8 hex digits); bench/campaign_order.py"
    path.write_text("\n".join([head] + lines) + "\n", encoding="ascii", newline="\n")
    print(f"{path.name}: {len(lines) - 1} lines, seed {seed_of(label)}")


def main() -> int:
    args = sys.argv[1:]
    mode = args[0] if args[:1] in (["--r2"], ["--m3"], ["--m2"]) else None
    if mode:
        args = args[1:]
    r2 = mode == "--r2"
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    out = pathlib.Path(args[0])
    out.mkdir(parents=True, exist_ok=True)
    if mode == "--m3":
        write(out / "oha-order-linux.tsv", *e2e_order("linux", "P5 oha"))
        return 0
    if mode == "--m2":
        write(out / "g-order-linux.tsv", *e2e_order("linux", "P5 grpc", ARMS + ("G",)))
        return 0
    prefix = "P5 replication" if r2 else "P5 campaign"
    for name, make in (("compile", compile_order), ("e2e", e2e_order)):
        for os_name in ("linux", "windows"):
            write(out / f"{name}-order-{os_name}.tsv", *make(os_name, prefix))
    if r2:
        write(out / "mixed-order-linux.tsv", *mixed_order())
    return 0


if __name__ == "__main__":
    sys.exit(main())
