#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""One compile-curve build (design note section 4): from .proto to a linked, checked executable.

Generators:
  R  the reflection generator: one translation unit #embeds the schema; phases compile, link.
  P  the protoc generator: phases protoc, compile_pb (schema.pb.cc), compile_user, link; linked
     with the prebuilt libprotobuf found through pkg-config under --protobuf.

Phases run one at a time. With timing on, each phase records its wall time (monotonic clock) and
the peak resident set size of its process tree (wait4). The schema and the user program are
written before the first phase and are not timed. After the last phase the program runs once,
untimed, and must print 0 with and without an argument.

Timing is for the laptop only; desktop runs pass --no-time (functional check).

Usage:
  compile_curve.py --gen R|P --shape wide|many --n N --rep K --work DIR --out FILE.jsonl
                   [--cxx G++] [--protobuf PREFIX] [--ops-limit L] [--time-report] [--no-time]
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
STD = ["-std=c++26", "-O2"]
STATIC_RUNTIME = ["-static-libstdc++", "-static-libgcc"]


def load_generator():
    spec = importlib.util.spec_from_file_location("gen_schema", HERE / "gen_schema.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_phase(name: str, cmd: list[str], cwd: pathlib.Path, timed: bool, log: pathlib.Path) -> dict:
    with open(log, "ab") as out:
        out.write(f"--- {name}\n$ {shlex.join(cmd)}\n".encode())
        out.flush()
        start = time.perf_counter_ns()
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
        _, status, usage = os.wait4(proc.pid, 0)
        end = time.perf_counter_ns()
        proc.returncode = os.waitstatus_to_exitcode(status)
    record = {"phase": name, "exit": proc.returncode}
    if timed:
        record["wall_ns"] = end - start
        record["maxrss_kib"] = usage.ru_maxrss
    return record


def plan(args) -> list[tuple[str, list[str]]]:
    cxx = args.cxx
    report = ["-ftime-report"] if args.time_report else []
    if args.gen == "R":
        limit = [f"-fconstexpr-ops-limit={args.ops_limit}"] if args.ops_limit else []
        return [
            ("compile", [cxx, *STD, "-freflection", *limit, *report, "-I", str(REPO / "include"), "-I", ".",
                         "-c", "user.cpp", "-o", "user.o"]),
            ("link", [cxx, "user.o", "-o", "user", *STATIC_RUNTIME]),
        ]
    prefix = pathlib.Path(args.protobuf)
    env = dict(os.environ, PKG_CONFIG_PATH=str(prefix / "lib" / "pkgconfig"))
    libs = subprocess.run(["pkg-config", "--static", "--libs", "protobuf"], env=env, check=True,
                          capture_output=True, text=True).stdout.split()
    include = ["-I", ".", "-I", str(prefix / "include")]
    return [
        ("protoc", [str(prefix / "protoc" / "bin" / "protoc"), "--cpp_out=.", "schema.proto"]),
        ("compile_pb", [cxx, *STD, *report, *include, "-c", "schema.pb.cc", "-o", "schema.pb.o"]),
        ("compile_user", [cxx, *STD, *report, *include, "-c", "user.cpp", "-o", "user.o"]),
        ("link", [cxx, "user.o", "schema.pb.o", "-o", "user", *STATIC_RUNTIME, *libs]),
    ]


def check(cell: pathlib.Path) -> bool:
    for argv in (["./user"], ["./user", "x"]):
        out = subprocess.run(argv, cwd=cell, capture_output=True, text=True)
        if out.returncode != 0 or out.stdout.strip() != "0":
            return False
    return True


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", choices=["R", "P"], required=True)
    ap.add_argument("--shape", choices=["wide", "many"], required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--rep", type=int, required=True)
    ap.add_argument("--work", required=True, help="a fresh build folder per build is made under it")
    ap.add_argument("--out", required=True, help="JSON lines file; one line per build is appended")
    ap.add_argument("--cxx", default=str(pathlib.Path.home() / "opt" / "gcc-16.2.0" / "bin" / "g++"))
    ap.add_argument("--protobuf", default=str(pathlib.Path.home() / "opt" / "protobuf-36.2"))
    ap.add_argument("--ops-limit", type=int, default=0, help="raise -fconstexpr-ops-limit (secondary curve)")
    ap.add_argument("--time-report", action="store_true", help="add -ftime-report to the compiles")
    ap.add_argument("--no-time", action="store_true", help="functional run: record no times")
    ap.add_argument("--keep", action="store_true", help="keep the build folder")
    args = ap.parse_args(argv[1:])

    gen = load_generator()
    cell = pathlib.Path(args.work) / f"{args.gen}_{args.shape}_{args.n}_{args.rep}"
    if cell.exists():
        shutil.rmtree(cell)
    cell.mkdir(parents=True)
    (cell / "schema.proto").write_text(gen.schema(args.shape, args.n), encoding="utf-8")
    user = gen.reflect_user(args.shape, args.n, "schema.proto") if args.gen == "R" else gen.protoc_user(args.shape, args.n)
    (cell / "user.cpp").write_text(user, encoding="utf-8")

    log = cell / "build.log"
    steps = plan(args)
    records = []
    for name, cmd in steps:
        records.append(run_phase(name, cmd, cell, not args.no_time, log))
        if records[-1]["exit"] != 0:
            break
    built = len(records) == len(steps) and all(r["exit"] == 0 for r in records)
    result = {
        "gen": args.gen, "shape": args.shape, "n": args.n, "rep": args.rep, "ops_limit": args.ops_limit,
        "time_report": args.time_report, "timed": not args.no_time, "phases": records,
        "built": built, "checked": built and check(cell),
    }
    with open(args.out, "a", encoding="utf-8") as out:
        out.write(json.dumps(result) + "\n")
    if args.time_report or not result["checked"]:
        suffix = ".time-report.log" if args.time_report else ".log"
        shutil.copy(log, pathlib.Path(args.out).with_name(cell.name + suffix))
    if not args.keep:
        shutil.rmtree(cell)
    return 0 if result["checked"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
