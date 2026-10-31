#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""The D5 gate of the compile-curve builds (Alex, 2026-10-31; bench/requests/sanitizer-records.md).

compile_curve.py builds are not CMake builds, so lab/bin/inputs_hash.py cannot hash them. Each
build records instead the files its compiles listed under include/tep/ with their sha256
(first_party_inputs). The rule: every such file must appear, with the same sha256, in the file
list (lab/bin/inputs_hash.py --list) of a green GCC sanitizer record. Whether the record is green
is read from the record itself; this script checks the match.

Usage:
  check_compile_inputs.py --list RECORD.tsv [--list RECORD.tsv ...] CURVE.jsonl [CURVE.jsonl ...]

Builds that did not build are not gated and are counted apart. Prints one line per failing
build and a summary per generator. Exit 0 if every gated build matches, 1 if any file is missing
or differs, 2 if a built build has no first_party_inputs or its dependency step failed.
"""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys


def record_files(lists: list[pathlib.Path]) -> dict[str, set[str]]:
    """{path, case folded: the sha256 values the records list for it}, from '<build>\\t<target>\\t<path>\\t<sha256>' lines."""
    files: dict[str, set[str]] = collections.defaultdict(set)
    for tsv in lists:
        for line in tsv.read_text(encoding="utf-8").splitlines():
            if line.strip():
                _, _, path, sha = line.split("\t")
                files[path.casefold()].add(sha)
    return files


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="append", required=True, type=pathlib.Path,
                    help="a green GCC sanitizer record's inputs_hash.py --list file")
    ap.add_argument("curves", nargs="+", type=pathlib.Path, help="compile_curve.py JSON lines files")
    args = ap.parse_args(argv[1:])
    covered = record_files(args.list)
    worst = 0
    tally: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    for curve in args.curves:
        for number, line in enumerate(curve.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            build = json.loads(line)
            where = f"{curve.name}:{number} {build['gen']} {build['shape']} {build['n']} rep {build['rep']}"
            count = tally[build["gen"]]
            if not build.get("built"):
                count["not built"] += 1
                continue
            inputs = build.get("first_party_inputs")
            if inputs is None or build.get("deps_exit") != 0:
                print(f"NO INPUTS {where}: deps_exit {build.get('deps_exit')}")
                count["no inputs"] += 1
                worst = 2
                continue
            bad = [path for path, sha in inputs.items() if sha not in covered.get(path.casefold(), set())]
            for path in bad:
                print(f"NOT COVERED {where}: {path} {inputs[path]}")
            count["not covered" if bad else "covered"] += 1
            count["files"] += len(inputs)
            if bad:
                worst = max(worst, 1)
    for gen, count in sorted(tally.items()):
        print(f"{gen}: covered {count['covered']}, not covered {count['not covered']}, no inputs {count['no inputs']}, "
              f"not built {count['not built']}; {count['files']} gated file entries")
    print("D5 compile-curve gate:", "pass" if worst == 0 else "FAIL")
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv))
