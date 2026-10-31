#!/usr/bin/env python3
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""One compile-curve build (design note section 4): from .proto to a linked, checked executable.

Generators:
  R  the reflection generator: one translation unit #embeds the schema; phases compile, link.
  P  the protoc generator: phases protoc, compile_pb (schema.pb.cc), compile_user, link; linked
     with the prebuilt static libprotobuf under --protobuf, its libraries named by pkg-config on
     Linux and by its .pc files, read here, on Windows (which has no pkg-config).

Phases run one at a time. With timing on, each phase records its wall time (monotonic clock) and
the peak memory of its process tree: on Linux the peak resident set size of the largest process
(wait4, maxrss_kib); on Windows the peak committed memory of the largest process (a job object,
peak_commit_kib). The two are different measures, compared only within one OS. The schema and the
user program are written before the first phase and are not timed. After the last phase the
program runs once, untimed, and must print 0 with and without an argument.

Windows (MinGW-w64 GCC 16.2.0; Alex's Q4 of 2026-10-31): each phase runs in its own job object,
started suspended, so its whole process tree is in the job. --cpu pins the job to one CPU, as
taskset -c pins the build on Linux; --timeout ends the job's process tree. Both generators link
the C++ runtime dynamically (finding F4), so the compiler's bin folder is put first on PATH.

Timing is for the laptop only; desktop runs pass --no-time (functional check).

Usage:
  compile_curve.py --gen R|P --shape wide|many --n N --rep K --work DIR --out FILE.jsonl
                   [--cxx G++] [--protobuf PREFIX] [--ops-limit L] [--time-report] [--no-time]
                   [--cpu C] [--timeout S]      (the last two on Windows only)
  compile_curve.py --self-test [--cpu C]        (Windows only: checks the job object, not timed)
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
STD = ["-std=c++26", "-O2"]
STATIC_RUNTIME = ["-static-libstdc++", "-static-libgcc"]
WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""

if WINDOWS:
    import ctypes
    from ctypes import wintypes

    CREATE_SUSPENDED = 0x4
    LIMIT_AFFINITY, LIMIT_KILL_ON_JOB_CLOSE = 0x10, 0x2000
    EXTENDED_LIMIT_INFORMATION = 9
    # No crash or missing-DLL dialog: a failing child exits with its status (children inherit it).
    QUIET_ERRORS = 0x1 | 0x2 | 0x8000  # SEM_FAILCRITICALERRORS, SEM_NOGPFAULTERRORBOX, SEM_NOOPENFILEERRORBOX

    class _IoCounters(ctypes.Structure):
        _fields_ = [(n, ctypes.c_ulonglong) for n in (
            "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
            "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

    class _BasicLimits(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong),
                    ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                    ("SchedulingClass", wintypes.DWORD)]

    class _ExtendedLimits(ctypes.Structure):
        _fields_ = [("BasicLimitInformation", _BasicLimits), ("IoInfo", _IoCounters),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _ntdll = ctypes.WinDLL("ntdll")
    _k32.CreateJobObjectW.restype = wintypes.HANDLE
    _k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    _k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    _k32.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD,
                                               ctypes.c_void_p]
    _k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    _k32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]
    _k32.SetErrorMode.argtypes = [wintypes.UINT]
    _ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]
    _ntdll.NtResumeProcess.restype = ctypes.c_long


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


def _job(cpu: int | None):
    """A new job object; closing its handle ends every process still in it."""
    job = _k32.CreateJobObjectW(None, None)
    if not job:
        raise OSError(f"CreateJobObjectW: error {ctypes.get_last_error()}")
    limits = _ExtendedLimits()
    limits.BasicLimitInformation.LimitFlags = LIMIT_KILL_ON_JOB_CLOSE
    if cpu is not None:
        limits.BasicLimitInformation.LimitFlags |= LIMIT_AFFINITY
        limits.BasicLimitInformation.Affinity = 1 << cpu
    if not _k32.SetInformationJobObject(job, EXTENDED_LIMIT_INFORMATION, ctypes.byref(limits), ctypes.sizeof(limits)):
        _k32.CloseHandle(job)
        raise OSError(f"SetInformationJobObject: error {ctypes.get_last_error()}")
    return job


def run_phase_windows(name: str, cmd: list[str], cwd: pathlib.Path, timed: bool, log: pathlib.Path, cpu: int | None,
                      timeout: float | None, env: dict) -> dict:
    job = _job(cpu)
    limits = _ExtendedLimits()
    timed_out = False
    try:
        with open(log, "ab") as out:
            out.write(f"--- {name}\n$ {subprocess.list2cmdline(cmd)}\n".encode())
            out.flush()
            start = time.perf_counter_ns()
            proc = subprocess.Popen(cmd, cwd=cwd, stdout=out, stderr=subprocess.STDOUT, env=env,
                                    creationflags=CREATE_SUSPENDED)
            try:  # never leave the suspended process outside the job
                handle = wintypes.HANDLE(int(proc._handle))
                if not _k32.AssignProcessToJobObject(job, handle):
                    raise OSError(f"{name}: AssignProcessToJobObject: error {ctypes.get_last_error()}")
                status = _ntdll.NtResumeProcess(handle)
                if status != 0:
                    raise OSError(f"{name}: NtResumeProcess: NTSTATUS {status & 0xFFFFFFFF:#x}")
            except BaseException:
                proc.kill()
                proc.wait()
                raise
            try:
                proc.wait(timeout)
            except subprocess.TimeoutExpired:
                _k32.TerminateJobObject(job, 1)
                proc.wait()
                timed_out = True
            end = time.perf_counter_ns()
        if not _k32.QueryInformationJobObject(job, EXTENDED_LIMIT_INFORMATION, ctypes.byref(limits),
                                              ctypes.sizeof(limits), None):
            raise OSError(f"QueryInformationJobObject: error {ctypes.get_last_error()}")
    finally:
        _k32.CloseHandle(job)
    record = {"phase": name, "exit": proc.returncode if not timed_out else -1}
    if timed_out:
        record["timed_out"] = True
    if cpu is not None:
        record["affinity"] = hex(limits.BasicLimitInformation.Affinity)
    if timed:
        record["wall_ns"] = end - start
        record["peak_commit_kib"] = limits.PeakProcessMemoryUsed // 1024
    return record


def pc_flags(pcdir: pathlib.Path, root: str) -> tuple[list[str], list[str]]:
    """The -l flags (each package before the packages it requires) and the -D flags of a package
    and of everything it requires, read from its .pc files: what `pkg-config --static --libs` and
    `--cflags` name. -L and -I come from --protobuf instead, since the files name the folder they
    were installed into on the machine that built them."""
    order: list[dict] = []
    seen: set[str] = set()

    def visit(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        fields = {}
        for line in (pcdir / f"{name}.pc").read_text(encoding="utf-8").splitlines():
            key, sep, value = line.partition(":")
            if sep and "=" not in key:
                fields[key.strip()] = value.strip()
        tokens = re.findall(r"[^\s,]+", fields.get("Requires", "") + " " + fields.get("Requires.private", ""))
        skip = False
        for token in tokens:
            if skip:
                skip = False
            elif token in ("=", "<", ">", "<=", ">=", "!="):
                skip = True  # the version after the operator
            else:
                visit(token)
        order.append(fields)  # after its requirements: reversed, a package precedes them

    visit(root)
    libs: list[str] = []
    defines: list[str] = []
    for fields in reversed(order):
        libs += [t for t in (fields.get("Libs", "") + " " + fields.get("Libs.private", "")).split() if t.startswith("-l")]
        defines += [t for t in fields.get("Cflags", "").split() if t.startswith("-D") and t not in defines]
    return libs, defines


def plan(args) -> list[tuple[str, list[str]]]:
    cxx = args.cxx
    report = ["-ftime-report"] if args.time_report else []
    runtime = [] if WINDOWS else STATIC_RUNTIME
    if args.gen == "R":
        limit = [f"-fconstexpr-ops-limit={args.ops_limit}"] if args.ops_limit else []
        return [
            ("compile", [cxx, *STD, "-freflection", *limit, *report, "-I", str(REPO / "include"), "-I", ".",
                         "-c", "user.cpp", "-o", "user.o"]),
            ("link", [cxx, "user.o", "-o", "user" + EXE, *runtime]),
        ]
    prefix = pathlib.Path(args.protobuf)
    if WINDOWS:
        names, defines = pc_flags(prefix / "lib" / "pkgconfig", "protobuf")
        libs = ["-L", str(prefix / "lib"), *names]
    else:
        env = dict(os.environ, PKG_CONFIG_PATH=str(prefix / "lib" / "pkgconfig"))
        libs = subprocess.run(["pkg-config", "--static", "--libs", "protobuf"], env=env, check=True,
                              capture_output=True, text=True).stdout.split()
        defines = []
    include = ["-I", ".", "-I", str(prefix / "include"), *defines]
    return [
        ("protoc", [str(prefix / "protoc" / "bin" / ("protoc" + EXE)), "--cpp_out=.", "schema.proto"]),
        ("compile_pb", [cxx, *STD, *report, *include, "-c", "schema.pb.cc", "-o", "schema.pb.o"]),
        ("compile_user", [cxx, *STD, *report, *include, "-c", "user.cpp", "-o", "user.o"]),
        ("link", [cxx, "user.o", "schema.pb.o", "-o", "user" + EXE, *runtime, *libs]),
    ]


def check(cell: pathlib.Path, env: dict | None, timeout: float | None) -> bool:
    exe = str(cell.resolve() / ("user" + EXE))
    for argv in ([exe], [exe, "x"]):
        try:
            out = subprocess.run(argv, cwd=cell, capture_output=True, text=True, env=env, timeout=timeout)
        except subprocess.TimeoutExpired:
            return False
        if out.returncode != 0 or out.stdout.strip() != "0":
            return False
    return True


SELF_TEST_GRANDCHILD = r"""
import ctypes, sys, time
k32 = ctypes.WinDLL("kernel32")
k32.GetCurrentProcess.restype = ctypes.c_void_p
k32.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
mask, system = ctypes.c_size_t(), ctypes.c_size_t()
k32.GetProcessAffinityMask(k32.GetCurrentProcess(), ctypes.byref(mask), ctypes.byref(system))
print(f"grandchild affinity {mask.value:#x}", flush=True)
block = bytearray(256 << 20)
if sys.argv[1:] == ["sleep"]:
    time.sleep(60)
"""
SELF_TEST_CHILD = ("import subprocess, sys; "
                   f"sys.exit(subprocess.call([sys.executable, '-I', '-c', {SELF_TEST_GRANDCHILD!r}] + sys.argv[1:]))")


def self_test(cpu: int | None) -> int:
    """Checks, untimed, that a phase's job holds its grandchildren (their committed memory counts and
    the CPU pin reaches them) and that --timeout ends the tree. Prints ok or FAIL per check."""
    with tempfile.TemporaryDirectory() as tmp:
        cwd = pathlib.Path(tmp)
        log = cwd / "self-test.log"
        env = dict(os.environ)
        tree = run_phase_windows("tree", [sys.executable, "-I", "-c", SELF_TEST_CHILD], cwd, True, log, cpu, 120, env)
        stopped = run_phase_windows("timeout", [sys.executable, "-I", "-c", SELF_TEST_CHILD, "sleep"], cwd, False, log,
                                    cpu, 5, env)
        text = log.read_text(encoding="utf-8", errors="replace")
    checks = {
        "grandchild memory counted": tree["exit"] == 0 and tree["peak_commit_kib"] >= 256 * 1024,
        "grandchild pinned": cpu is None or f"grandchild affinity {1 << cpu:#x}" in text,
        "timeout ends the tree": stopped.get("timed_out", False),
    }
    for what, ok in checks.items():
        print(f"self-test: {what}: {'ok' if ok else 'FAIL'}")
    if not all(checks.values()):
        print(f"self-test records: {tree} {stopped}\n{text}")
    return 0 if all(checks.values()) else 1


def default_tools() -> tuple[str, str]:
    opt = pathlib.Path.home() / "opt"
    if WINDOWS:
        return (str(opt / "winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3" / "mingw64" / "bin" / "g++.exe"),
                str(opt / "protobuf-36.2-mingw"))
    return str(opt / "gcc-16.2.0" / "bin" / "g++"), str(opt / "protobuf-36.2")


def main(argv: list[str]) -> int:
    cxx, protobuf = default_tools()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", choices=["R", "P"])
    ap.add_argument("--shape", choices=["wide", "many"])
    ap.add_argument("--n", type=int)
    ap.add_argument("--rep", type=int)
    ap.add_argument("--work", help="a fresh build folder per build is made under it")
    ap.add_argument("--out", help="JSON lines file; one line per build is appended")
    ap.add_argument("--cxx", default=cxx)
    ap.add_argument("--protobuf", default=protobuf)
    ap.add_argument("--ops-limit", type=int, default=0, help="raise -fconstexpr-ops-limit (secondary curve)")
    ap.add_argument("--time-report", action="store_true", help="add -ftime-report to the compiles")
    ap.add_argument("--no-time", action="store_true", help="functional run: record no times")
    ap.add_argument("--keep", action="store_true", help="keep the build folder")
    ap.add_argument("--cpu", type=int, help="Windows: pin each phase's process tree to this CPU")
    ap.add_argument("--timeout", type=float, help="Windows: end a phase's process tree after this many seconds")
    ap.add_argument("--self-test", action="store_true", help="Windows: check the job object and exit")
    args = ap.parse_args(argv[1:])
    if not WINDOWS and (args.cpu is not None or args.timeout is not None or args.self_test):
        ap.error("--cpu, --timeout and --self-test are for Windows; on Linux use taskset and timeout")
    if WINDOWS:
        _k32.SetErrorMode(QUIET_ERRORS)
    if args.self_test:
        return self_test(args.cpu)
    missing = [k for k in ("gen", "shape", "n", "rep", "work", "out") if getattr(args, k) is None]
    if missing:
        ap.error("required: " + ", ".join("--" + k for k in missing))

    gen = load_generator()
    cell = pathlib.Path(args.work) / f"{args.gen}_{args.shape}_{args.n}_{args.rep}"
    if cell.exists():
        shutil.rmtree(cell)
    cell.mkdir(parents=True)
    (cell / "schema.proto").write_text(gen.schema(args.shape, args.n), encoding="utf-8")
    user = gen.reflect_user(args.shape, args.n, "schema.proto") if args.gen == "R" else gen.protoc_user(args.shape, args.n)
    (cell / "user.cpp").write_text(user, encoding="utf-8")

    env = None
    if WINDOWS:
        env = dict(os.environ, PATH=str(pathlib.Path(args.cxx).parent) + os.pathsep + os.environ.get("PATH", ""))
    log = cell / "build.log"
    steps = plan(args)
    records = []
    for name, cmd in steps:
        if WINDOWS:
            records.append(run_phase_windows(name, cmd, cell, not args.no_time, log, args.cpu, args.timeout, env))
        else:
            records.append(run_phase(name, cmd, cell, not args.no_time, log))
        if records[-1]["exit"] != 0:
            break
    built = len(records) == len(steps) and all(r["exit"] == 0 for r in records)
    result = {
        "gen": args.gen, "shape": args.shape, "n": args.n, "rep": args.rep, "ops_limit": args.ops_limit,
        "time_report": args.time_report, "timed": not args.no_time, "phases": records,
        "built": built, "checked": built and check(cell, env, args.timeout),
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
