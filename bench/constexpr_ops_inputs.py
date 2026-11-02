#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""The D5 inputs of the constant-evaluation counts (Laptop request 26; added after A59, 2026-11-02).

bench/constexpr_ops.sh compiles, per cell, a translation unit that includes tep/reflect.hpp and
embeds a generated schema (stage `tables`, -fsyntax-only). Its builds are not CMake builds, so this
lists them as compile_curve.py lists its own: `g++ -MM` of the same translation unit with the same
flags, and every listed file under include/tep/ with its sha256 (compile_curve.first_party_inputs,
CRLF read as LF). One JSON line per cell, in the form bench/check_compile_inputs.py reads (gen R,
rep 0), so the same gate checks them against the GCC sanitizer records' lists. Untimed.

Usage (inside papers-dev, from a tree at the commit the counts ran at):
  constexpr_ops_inputs.py OUT.jsonl
"""
from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import compile_curve  # noqa: E402

SHAPES = ("wide", "many")
SIZES = (16, 32, 64, 128, 256, 512, 1024, 2048, 4096)
# bench/constexpr_ops.sh, stage tables.
TU = """#include <string_view>
#include "tep/reflect.hpp"
struct schema { static constexpr char data[] = {
#embed "schema.proto"
, '\\0'}; static constexpr std::string_view text{data, sizeof data - 1}; };
static_assert(!tep::tables<schema>.fields.empty());
"""


def cell_inputs(cxx: str, shape: str, n: int) -> dict:
    with tempfile.TemporaryDirectory() as tmp:
        cell = pathlib.Path(tmp)
        schema = subprocess.run([sys.executable, str(HERE / "gen_schema.py"), "schema", "--shape", shape, "--n", str(n)],
                                capture_output=True, text=True, check=True).stdout
        (cell / "schema.proto").write_text(schema, encoding="utf-8", newline="\n")
        (cell / "tu.cpp").write_text(TU, encoding="utf-8", newline="\n")
        # constexpr_ops.sh's flags and the request's CXXFLAGS; -fsyntax-only is dropped, since -MM stops after preprocessing.
        cmd = [cxx, "-std=c++26", "-I", str(compile_curve.REPO / "include"), "-O2", "-DNDEBUG", "-freflection",
               "-c", "tu.cpp"]
        status, files, errors = compile_curve.first_party_inputs([("compile_tables", cmd)], cell, None)
    if errors:
        print(errors, file=sys.stderr)
    return {"gen": "R", "shape": shape, "n": n, "rep": 0, "built": True, "deps_exit": status,
            "first_party_inputs": files}


def main(argv: list[str]) -> int:
    cxx = os.environ.get("CXX", "g++")
    version = subprocess.run([cxx, "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    lines = [dict(cell_inputs(cxx, shape, n), compiler=version) for shape in SHAPES for n in SIZES]
    pathlib.Path(argv[1]).write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8", newline="\n")
    print(f"{len(lines)} cells; {version}; deps_exit {sorted({line['deps_exit'] for line in lines})}")
    return 0 if all(line["deps_exit"] == 0 for line in lines) else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
