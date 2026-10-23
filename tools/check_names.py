#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""Fails if a file of the repository contains a name of the thesis's integration library.

The banned names are stored only as SHA-256 digests of their normalized form (lowercase ASCII
letters and digits) in tools/banned_names.txt, so the repository never spells them. Each line of
each tracked or untracked (not ignored) text file is normalized the same way, and every substring
of a banned length is hashed and compared. A match prints the file and line, never the match.

Usage:
  check_names.py <repo root>      scan the repository; exit 1 on a match
  check_names.py --self-test      plant a synthetic name in a temporary file; exit 0 only if found
"""
from __future__ import annotations

import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile

NON_ALNUM = re.compile(r"[^a-z0-9]+")
SKIP_SUFFIXES = {".bin"}


def load_digests(path: pathlib.Path) -> dict[int, set[str]]:
    digests: dict[int, set[str]] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        length, digest = line.split()
        digests.setdefault(int(length), set()).add(digest.lower())
    if not digests:
        raise SystemExit(f"{path}: no digests")
    return digests


def line_hits(text: str, digests: dict[int, set[str]]) -> bool:
    norm = NON_ALNUM.sub("", text.lower())
    for length, wanted in digests.items():
        for i in range(len(norm) - length + 1):
            if hashlib.sha256(norm[i : i + length].encode("ascii")).hexdigest() in wanted:
                return True
    return False


def scan(files: list[pathlib.Path], root: pathlib.Path, digests: dict[int, set[str]]) -> list[str]:
    hits: list[str] = []
    for path in files:
        if path.suffix in SKIP_SUFFIXES or not path.is_file():
            continue
        text = path.read_bytes().decode("utf-8", errors="replace")
        found = False
        for number, line in enumerate(text.splitlines(), start=1):
            if line_hits(line, digests):
                hits.append(f"{path.relative_to(root)}:{number}")
                found = True
        # A name split across lines (or by punctuation spanning lines) shows in the whole text.
        if not found and line_hits(text, digests):
            hits.append(f"{path.relative_to(root)} (across lines)")
    return hits


def repo_files(root: pathlib.Path) -> list[pathlib.Path]:
    out = subprocess.run(
        ["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        check=True,
        capture_output=True,
    ).stdout
    return [root / name for name in out.decode("utf-8").split("\0") if name]


def self_test() -> int:
    planted = "plantedcanary"
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "digests.txt").write_text(
            f"{len(planted)} {hashlib.sha256(planted.encode()).hexdigest()}\n", encoding="utf-8"
        )
        (root / "clean.txt").write_text("nothing here\n", encoding="utf-8")
        (root / "dirty.txt").write_text("line one\nuses Planted_Canary::thing\n", encoding="utf-8")
        (root / "split.txt").write_text("planted\ncanary\n", encoding="utf-8")
        (root / "nul.dat").write_bytes(b"\0\0PLANTEDCANARY\0")
        digests = load_digests(root / "digests.txt")
        names = ["clean.txt", "dirty.txt", "split.txt", "nul.dat"]
        hits = scan([root / n for n in names], root, digests)
    if hits == ["dirty.txt:2", "split.txt (across lines)", "nul.dat:1"]:
        print("name guard self-test: the planted name was found")
        return 0
    print(f"name guard self-test FAILED: hits {hits}")
    return 1


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) != 2:
        print(__doc__)
        return 2
    root = pathlib.Path(argv[1]).resolve()
    digests = load_digests(root / "tools" / "banned_names.txt")
    files = repo_files(root)
    hits = scan(files, root, digests)
    if hits:
        print("the integration library is named in:")
        for h in hits:
            print("  " + h)
        return 1
    print(f"name guard: {len(files)} files clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
