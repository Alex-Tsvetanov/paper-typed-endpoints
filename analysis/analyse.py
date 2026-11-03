# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""P5's campaign analysis: hypotheses.md (frozen at 704061a; CODE_FREEZE 9a3dc92), sections 2 to 5.

Reads the unpacked `out` folders of Laptop requests 22 to 25 (bench/requests/compile-campaign.md,
e2e-campaign.md), and optionally request 24's test_allocs output and request 26's constexpr-ops.txt;
writes results/summary.json, results/macros.tex, results/tables/compile.tex and growth.tex (table
bodies of macros) and results/figures/compile-ratio.pdf (drawn from summary.json; tables, figure and
phase macros added 2026-11-03), and prints a report. Standard library only.

  python -I analysis/analyse.py --compile-linux D --compile-windows D --e2e-linux D --e2e-windows D
                                [--allocs FILE] [--constexpr-ops FILE] [--out results]

The hypotheses and directions are those of hypotheses.md section 7, as frozen; this script holds
them as tables (DIRECTION, E2E_HYPOTHESES) and changes none.

Choices hypotheses.md leaves open, fixed here before any campaign archive is opened:
- Compile pairs. A pair is the R and the P line of one cell and replicate in campaign.jsonl. If
  either build does not count (section 2, Validity), the pair from retry.jsonl is used if both of
  its builds count; otherwise the pair is void twice and its cell is descriptive.
- Growth intervals: clustered by replicate. A resample draws replicates, each with that
  generator's builds at N and at 2N (from the cells' pairs).
- End-to-end replicate value: the mean of the 4 windows' requests per second: h2load's "req/s"
  (succeeded calls per second of its main phase), ghz's "Requests/sec" (Count over Total).
- Validity of a server run, as requests 14 and 15 check it (e2e-campaign.md): every window file
  present and clean, the generator's exit status 0 (`rc` in shares.jsonl; request 15 checked
  ghz's), and the server log's count in its bound. A run whose server never started, or whose
  generator wrote no file, is void like any other.
- A void server run is replaced by its rerun when the rerun is valid. If the rerun is void too,
  the cells it touches are descriptive: section 3 defines a cell as a payload at a load, so both
  loads of that payload on that OS, every contrast in them, whatever the run's arm.
- The server's CPU share: tep_server's `mid` share (20% to 80% of the window), in percent of one
  CPU, the mean over a cell's windows; the generator's likewise. A null share (no such process)
  is left out and counted.
- The Linux ghz cross-check is reported; a missing or failed window is listed and stops nothing.
- Linux latency at c1 m1: per replicate, the median and the 99th percentile (inverted_cdf) of
  column 3 (microseconds until the end of the response) over the requests of its 4 latency logs;
  reported, the median over replicates.
- Windows mean call cycle at c1 m1: 1e6 over the mean, over replicates, of requests per second, in
  microseconds.
- Peak memory: the median over a cell's server runs, in MiB (KiB / 1024).
- Bootstrap seeds: seed_of("P5 bootstrap <experiment> <os> <cell> <contrast>") (stats.py).
- Validity is recomputed from the raw files; the analysis stops when its void lists differ from a
  job's validate.txt, or when a file is missing or unexpected.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import statistics
import sys
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import figure  # noqa: E402
import macros  # noqa: E402
import stats as S  # noqa: E402

OSES = ("linux", "windows")
SIZES = (16, 32, 64, 128, 256, 512, 1024, 2048, 4096)
CELLS = [(shape, n) for shape in ("wide", "many") for n in SIZES]
REPLICATES = range(1, 11)
FIVE = {"tep/include/tep/" + f for f in ("fixed_string.hpp", "grpc.hpp", "reflect.hpp", "schema.hpp", "wire.hpp")}

# hypotheses.md section 7, items 1 and 2 (704061a): the direction of R against P in every compile
# cell, the same on both OSes. "faster" means R's whole build takes less time than P's.
DIRECTION = {("wide", n): "faster" for n in SIZES} | {("many", n): "faster" if n <= 256 else "slower" for n in SIZES}
# Section 2: R many 4096 builds with -fconstexpr-ops-limit=67108864 and is labelled the secondary
# curve wherever it appears (its cell, and the growth rows that end at it).
SECONDARY = ("many", 4096)

PAYLOADS = ("small", "medium")
ARMS = ("R", "P", "Q", "S")
LOADS = {"light": (1, 1), "heavy": (12, 128)}  # c1 m1 and c12 m128 (section 3)
# hypotheses.md section 3 and section 7, item 3 (704061a): the end-to-end contrasts that carry a
# hypothesis, R serving more requests per second than the other arm. Every other is descriptive.
E2E_HYPOTHESES = {
    ("linux", "small", "heavy", "P"), ("linux", "small", "heavy", "Q"),
    ("linux", "medium", "heavy", "P"), ("linux", "medium", "heavy", "Q"),
    ("linux", "medium", "light", "P"), ("linux", "medium", "light", "Q"),
    ("windows", "medium", "light", "P"),
}

ORDER_SHA256 = {  # bench/campaign/, the orders the jobs follow (bench/requests/*-campaign.md)
    ("compile", "linux"): "11414e03b79e7761a154321abf7c33cf94e01f4b16c8c384abba426be6d13840",
    ("compile", "windows"): "7f40b65babbd021ec40a751d268638f8e3b9228f89aa457d1327224194da2a1c",
    ("e2e", "linux"): "fe2023bf88627596ad18ae13e0ba6daa76a26fc22fa8bcd98b701f0fc8395031",
    ("e2e", "windows"): "9daa56f9cb02db4a846653d9bf6cd6d085ff6491b77bc002dace20fe585c7129",
}
# validate.txt's named lines (the requests fix their form).
VALIDATE_LINES = {
    "compile": re.compile(r"^(void|retried) ((?:wide|many) \d+ r\d+)$|^(descriptive) ((?:wide|many) \d+)$"),
    "e2e": re.compile(r"^(void) ([RPQS] (?:small|medium) r\d+x?)$"),
}


class AnalysisError(Exception):
    """A campaign folder that does not match its request: the analysis stops."""


def fail(message: str):
    raise AnalysisError(message)


def text_of(path: Path) -> str:
    if not path.is_file():
        fail(f"{path}: missing")
    return path.read_text(encoding="utf-8", errors="replace")


def read_order(folder: Path, experiment: str, os_name: str) -> list[dict]:
    path = folder / f"{experiment}-order-{os_name}.tsv"
    if not path.is_file():
        fail(f"{path}: the order file is missing")
    if hashlib.sha256(path.read_bytes()).hexdigest() != ORDER_SHA256[(experiment, os_name)]:
        fail(f"{path}: sha256 differs from bench/campaign/")
    lines = [ln for ln in path.read_text(encoding="ascii").splitlines() if ln and not ln.startswith("#")]
    head = lines[0].split("\t")
    return [dict(zip(head, ln.split("\t"))) for ln in lines[1:]]


def read_validate(folder: Path, experiment: str) -> dict[str, set[str]]:
    """validate.txt's named lines, by kind: void, retried, descriptive."""
    out: dict[str, set[str]] = {"void": set(), "retried": set(), "descriptive": set()}
    for line in text_of(folder / "validate.txt").splitlines():
        if m := VALIDATE_LINES[experiment].match(line.strip()):
            kind, rest = (m[1], m[2]) if m[1] else (m[3], m[4])
            out[kind].add(rest)
    return out


def read_jsonl(path: Path) -> list[dict]:
    return [json.loads(ln) for ln in text_of(path).splitlines() if ln.strip()]


# ---------------------------------------------------------------- compile (section 2)


def counts(build: dict, os_name: str) -> bool:
    """Section 2, Validity: built and checked; on Windows every phase on CPU 14 and none timed out."""
    if not (build.get("built") is True and build.get("checked") is True):
        return False
    if os_name == "windows":
        return all(p.get("affinity") == "0x4000" and not p.get("timed_out") for p in build["phases"])
    return True


def seconds(build: dict) -> float:
    return sum(p["wall_ns"] for p in build["phases"]) / 1e9


def peak_kib(build: dict, os_name: str) -> int:
    key = "maxrss_kib" if os_name == "linux" else "peak_commit_kib"
    return max(p[key] for p in build["phases"])


def check_inputs(build: dict, where: str) -> None:
    """The request's added checks on a built line: deps_exit 0, no deps_error, the five headers on R, none on P."""
    if build.get("built") is not True:
        return
    if build.get("deps_exit") != 0 or "deps_error" in build:
        fail(f"{where}: deps_exit not 0 or a deps_error")
    if set(build.get("first_party_inputs") or {}) != (FIVE if build["gen"] == "R" else set()):
        fail(f"{where}: first_party_inputs are not as the request says")


def check_line(b: dict, want: tuple, where: str) -> None:
    """A campaign or retry line is the build its order row names (gen, shape, n, rep, ops_limit), timed, without -ftime-report."""
    got = (b.get("gen"), b.get("shape"), b.get("n"), b.get("rep"), b.get("ops_limit"))
    if got != want or b.get("timed") is not True or b.get("time_report") is not False:
        fail(f"{where} is {got}, the order says {want}")
    if b.get("built") is True and not all("wall_ns" in p for p in b.get("phases", [])):
        fail(f"{where}: a built line without wall_ns on every phase")
    check_inputs(b, where)


def load_compile(folder: Path, os_name: str) -> dict:
    """The pairs of every cell, after void and retry, checked against validate.txt."""
    order = read_order(folder, "compile", os_name)
    lines = read_jsonl(folder / "campaign.jsonl")
    if len(lines) != 360 or len(order) != 360:
        fail(f"{folder}: campaign.jsonl has {len(lines)} lines, want 360")
    builds: dict[tuple, dict] = {}
    wants: dict[tuple, tuple] = {}
    for k, (row, b) in enumerate(zip(order, lines), 1):
        want = (row["gen"], row["shape"], int(row["n"]), int(row["rep"]), int(row["ops_limit"]))
        check_line(b, want, f"{folder}: campaign.jsonl line {k}")
        builds[(b["shape"], b["n"], b["rep"], b["gen"])] = b
        wants[(b["shape"], b["n"], b["rep"], b["gen"])] = want
    retry_path = folder / "retry.jsonl"
    retry: dict[tuple, dict] = {}
    for k, b in enumerate(read_jsonl(retry_path) if retry_path.exists() else [], 1):
        key = (b.get("shape"), b.get("n"), b.get("rep"), b.get("gen"))
        if key in retry or key not in builds:
            fail(f"retry.jsonl line {k}: {key} repeated or not in the order")
        check_line(b, wants[key], f"{folder}: retry.jsonl line {k}")
        retry[key] = b
    pairs: dict[tuple, dict[int, tuple[dict, dict]]] = {cell: {} for cell in CELLS}
    void, retried, descriptive = set(), set(), set()
    for (shape, n) in CELLS:
        for rep in REPLICATES:
            r, p = builds[(shape, n, rep, "R")], builds[(shape, n, rep, "P")]
            if counts(r, os_name) and counts(p, os_name):
                pairs[(shape, n)][rep] = (r, p)
                continue
            void.add(f"{shape} {n} r{rep}")
            rr, rp = retry.get((shape, n, rep, "R")), retry.get((shape, n, rep, "P"))
            if rr is None or rp is None:
                fail(f"{folder}: void pair {shape} {n} r{rep} was not built again")
            retried.add(f"{shape} {n} r{rep}")
            if counts(rr, os_name) and counts(rp, os_name):
                pairs[(shape, n)][rep] = (rr, rp)
            else:
                descriptive.add(f"{shape} {n}")
    stray = {k for k in retry if f"{k[0]} {k[1]} r{k[2]}" not in void}
    if stray:
        fail(f"{folder}: retry.jsonl holds pairs that were not void: {sorted(stray)}")
    said = read_validate(folder, "compile")
    if (said["void"], said["retried"], said["descriptive"]) != (void, retried, descriptive):
        fail(f"{folder}: validate.txt's void, retried or descriptive lines differ from the analysis's")
    return {"pairs": pairs, "void": sorted(void), "descriptive": sorted(descriptive)}


def phase_medians(builds: list[dict]) -> dict[str, float]:
    names = [p["phase"] for p in builds[0]["phases"]]
    return {name: statistics.median(next(p for p in b["phases"] if p["phase"] == name)["wall_ns"] / 1e9 for b in builds)
            for name in names}


def interval(values: list, stat, label: str) -> dict:
    """The statistic and its BCa interval, or None values when fewer than two clusters are left."""
    if len(values) < 2:
        return {"ratio": None, "low": None, "high": None, "seed": None}
    iv = S.bca(values, stat, label, resamples=S.B_RESAMPLES)  # read at call time, so a test may lower it
    return {"ratio": iv.theta, "low": iv.low, "high": iv.high, "seed": iv.seed}


def compile_cell(os_name: str, cell: tuple[str, int], reps: dict[int, tuple[dict, dict]], descriptive: bool) -> dict:
    shape, n = cell
    values = [(seconds(r), seconds(p)) for _, (r, p) in sorted(reps.items())]
    out = {"os": os_name, "shape": shape, "n": n, "pairs": len(values), "secondary": cell == SECONDARY,
           "direction": None if descriptive or len(values) != 10 else DIRECTION[cell]}
    out |= interval(values, S.ratio_of_medians, f"P5 bootstrap compile {os_name} {shape} {n} R/P")
    if values:
        rs, ps = [r for r, _ in reps.values()], [p for _, p in reps.values()]
        out |= {"median_r_s": statistics.median(v[0] for v in values), "median_p_s": statistics.median(v[1] for v in values),
                "peak_r_mib": statistics.median(peak_kib(b, os_name) for b in rs) / 1024,
                "peak_p_mib": statistics.median(peak_kib(b, os_name) for b in ps) / 1024,
                "phases_r_s": phase_medians(rs), "phases_p_s": phase_medians(ps)}
    if out["direction"]:
        faster = out["direction"] == "faster"
        k = sum((r < p) if faster else (r > p) for r, p in values)  # a tie counts against
        out["in_direction"], out["p"] = k, S.sign_p(k, len(values))
    return out


def growth(os_name: str, pairs: dict, gen_index: int, gen: str) -> list[dict]:
    """Section 2, Growth: per generator and shape, median at 2N over median at N, clustered by replicate."""
    out = []
    for shape in ("wide", "many"):
        for n in SIZES[:-1]:
            lo, hi = pairs[(shape, n)], pairs[(shape, 2 * n)]
            reps = sorted(set(lo) & set(hi))
            values = [(seconds(hi[r][gen_index]), seconds(lo[r][gen_index])) for r in reps]
            row = {"os": os_name, "gen": gen, "shape": shape, "n": n, "replicates": len(reps),
                   "secondary": (shape, 2 * n) == SECONDARY}
            out.append(row | interval(values, S.ratio_of_medians, f"P5 bootstrap growth {os_name} {gen} {shape} {n}"))
    return out


def analyse_compile(folders: dict[str, Path]) -> dict:
    cells, grow, void, descriptive = [], [], {}, {}
    for os_name in OSES:
        data = load_compile(folders[os_name], os_name)
        void[os_name], descriptive[os_name] = data["void"], data["descriptive"]
        for cell in CELLS:
            cells.append(compile_cell(os_name, cell, data["pairs"][cell], f"{cell[0]} {cell[1]}" in data["descriptive"]))
        for gen_index, gen in ((0, "R"), (1, "P")):
            grow += growth(os_name, data["pairs"], gen_index, gen)
    tests = [c for c in cells if c["direction"]]
    pvals = [c["p"] for c in tests]
    for c, adj, ok in zip(tests, S.holm_adjust(pvals), S.holm_pass(pvals)):
        c["holm_adjusted"], c["holm_pass"] = adj, ok
    return {"cells": cells, "growth": grow, "m": len(tests), "void": void, "descriptive": descriptive}


# ---------------------------------------------------------------- end to end (section 3)

H2LOAD_RATE = re.compile(r"finished in ([\d.]+)s, ([\d.]+) req/s")
H2LOAD_OK = re.compile(r"requests: \d+ total, \d+ started, \d+ done, (\d+) succeeded, 0 failed, 0 errored, 0 timeout")
H2LOAD_2XX = re.compile(r"status codes: \d+ 2xx, 0 3xx, 0 4xx, 0 5xx")
GHZ_COUNT = re.compile(r"^\s*Count:\s*(\d+)", re.M)
GHZ_RATE = re.compile(r"Requests/sec:\s+([\d.]+)")
GHZ_STATUS = re.compile(r"\[(\w+)\]\s+\d+ responses")
SERVER_LAST = re.compile(r"^tep_server: calls ok (\d+), failed 0$")
FILE_PATTERN = re.compile(r"^(h2load|ghz|latency|server)-[RPQS]-(small|medium)-")


def tool_of(os_name: str) -> str:
    return "h2load" if os_name == "linux" else "ghz"


def run_windows(os_name: str, arm: str, payload: str, tag: str) -> list[tuple[str, int, int, str]]:
    """The 9 windows of one server run, in order: (file, c, m, k)."""
    spec = [(12, 128, "warmup")] + [(*LOADS[load], str(k)) for load in ("light", "heavy") for k in range(1, 5)]
    return [(f"{tool_of(os_name)}-{arm}-{payload}-c{c}-m{m}-{tag}-{k}.txt", c, m, k) for c, m, k in spec]


def parse_window(path: Path, os_name: str) -> tuple[bool, int, float]:
    """(valid, count, requests per second) of one window file."""
    text = text_of(path)
    if os_name == "linux":
        ok, rate = H2LOAD_OK.search(text), H2LOAD_RATE.search(text)
        valid = bool(ok and rate and H2LOAD_2XX.search(text))
        return valid, int(ok.group(1)) if ok else 0, float(rate.group(2)) if rate else 0.0
    count, rate = GHZ_COUNT.search(text), GHZ_RATE.search(text)
    valid = bool(count and rate and set(GHZ_STATUS.findall(text)) == {"OK"} and "Error distribution:" not in text)
    return valid, int(count.group(1)) if count else 0, float(rate.group(1)) if rate else 0.0


def check_run(folder: Path, os_name: str, arm: str, payload: str, tag: str, rcs: dict) -> tuple[bool, dict]:
    """Validity of one server run (as requests 14 and 15 check it, e2e-campaign.md) and its loads' window rates.
    A missing window file or server log makes the run void (a server that never started, a generator that wrote nothing)."""
    valid, total, slack, rates = True, 0, 0, {"light": [], "heavy": []}
    for name, c, m, k in run_windows(os_name, arm, payload, tag):
        path = folder / name
        if not path.is_file():
            valid = False
            continue
        if name not in rcs:
            fail(f"{folder}: shares.jsonl has no line for {name}")
        ok, count, rate = parse_window(path, os_name)
        valid = valid and ok and rcs[name] == 0
        total += count
        slack += 2 * c * m
        if k != "warmup":
            rates["light" if (c, m) == LOADS["light"] else "heavy"].append(rate)
    log = folder / f"server-{arm}-{payload}-{tag}.log"
    lines = log.read_text(encoding="utf-8", errors="replace").strip().splitlines() if log.is_file() else []
    last = SERVER_LAST.match(lines[-1].strip()) if lines else None
    valid = valid and bool(last) and total <= int(last.group(1)) <= total + slack
    return valid, rates


def latency(folder: Path, arm: str, payload: str, tag: str) -> tuple[float, float]:
    """Median and p99 (microseconds) over the requests of one run's 4 c1 m1 latency logs (Linux)."""
    values: list[int] = []
    for k in range(1, 5):
        values += [int(ln.split("\t")[2]) for ln in text_of(folder / f"latency-{arm}-{payload}-{tag}-{k}.tsv").splitlines() if ln]
    values.sort()
    return float(statistics.median(values)), float(S.inverted_cdf(values, 0.99))


def possible_files(os_name: str, runs: list[tuple[str, str, str]]) -> set[str]:
    """Every file name a run that was executed may leave; a file outside this set stops the analysis."""
    names = set()
    for arm, payload, tag in runs:
        names |= {w[0] for w in run_windows(os_name, arm, payload, tag)}
        names.add(f"server-{arm}-{payload}-{tag}.log")
        if os_name == "linux":
            names |= {f"latency-{arm}-{payload}-{tag}-{k}.tsv" for k in range(1, 5)}
    return names


def read_keyed(path: Path, keys: tuple[str, ...], key_of) -> dict:
    """A JSON lines file as a map by key_of(line); every line holds the keys, and no key repeats."""
    out = {}
    for k, line in enumerate(read_jsonl(path), 1):
        missing = [x for x in keys if x not in line]
        if missing:
            fail(f"{path} line {k}: no {missing}")
        key = key_of(line)
        if key in out:
            fail(f"{path} line {k}: {key} repeated")
        out[key] = line
    return out


def run_entry(folder: Path, os_name: str, arm: str, payload: str, rep: int, tag: str, rates: dict,
              shares: dict, peaks: dict) -> dict:
    entry = {load: statistics.fmean(rates[load]) for load in LOADS}
    key = (arm, payload, rep, tag.endswith("x"))
    if key not in peaks:
        fail(f"{folder}: peak.jsonl has no line for {key}")
    kib = peaks[key]["kib"]
    entry["peak_mib"] = None if kib is None else kib / 1024
    windows = run_windows(os_name, arm, payload, tag)[1:]
    entry["share"] = {load: [shares[w[0]]["mid"] for w in windows if (w[1], w[2]) == LOADS[load]] for load in LOADS}
    if os_name == "linux":
        entry["latency"] = latency(folder, arm, payload, tag)
    return entry


def load_e2e(folder: Path, os_name: str) -> dict:
    order = read_order(folder, "e2e", os_name)
    if len(order) != 80:
        fail(f"{folder}: the order has {len(order)} rows, want 80")
    shares = read_keyed(folder / "shares.jsonl", ("file", "rc", "mid"), lambda s: s["file"])
    rcs = {name: s["rc"] for name, s in shares.items()}
    peaks = read_keyed(folder / "peak.jsonl", ("arm", "payload", "rep", "rerun", "kib"),
                       lambda p: (p["arm"], p["payload"], p["rep"], bool(p["rerun"])))
    runs, void, twice, values = [], set(), set(), {}
    for row in order:
        arm, payload, rep = row["arm"], row["payload"], int(row["rep"])
        tag = f"r{rep}"
        runs.append((arm, payload, tag))
        valid, rates = check_run(folder, os_name, arm, payload, tag, rcs)
        if not valid:
            void.add(f"{arm} {payload} {tag}")
            tag = f"r{rep}x"
            runs.append((arm, payload, tag))
            valid, rates = check_run(folder, os_name, arm, payload, tag, rcs)
            if not valid:
                void.add(f"{arm} {payload} {tag}")
                twice.add(payload)
        if valid:
            values[(arm, payload, rep)] = run_entry(folder, os_name, arm, payload, rep, tag, rates, shares, peaks)
    present = {p.name for p in folder.iterdir() if FILE_PATTERN.match(p.name)}
    stray = present - possible_files(os_name, runs)
    if stray:
        fail(f"{folder}: unexpected files {sorted(stray)[:5]}")
    if read_validate(folder, "e2e")["void"] != void:
        fail(f"{folder}: validate.txt's void lines differ from the analysis's")
    return {"values": values, "void": sorted(void), "touched": sorted(twice)}


def e2e_contrast(os_name: str, payload: str, load: str, other: str, values: dict, touched: bool) -> dict:
    reps = [r for r in REPLICATES if ("R", payload, r) in values and (other, payload, r) in values]
    pairs = [(values[("R", payload, r)][load], values[(other, payload, r)][load]) for r in reps]
    hypothesis = (os_name, payload, load, other) in E2E_HYPOTHESES and len(reps) == 10 and not touched
    out = {"os": os_name, "payload": payload, "load": load, "other": other, "replicates": len(reps),
           "hypothesis": hypothesis, "touched": touched}
    out |= interval(pairs, S.ratio_of_means, f"P5 bootstrap e2e {os_name} {payload} {load} R/{other}")
    if hypothesis:
        k = sum(r > o for r, o in pairs)  # a tie counts against
        out["in_direction"], out["p"] = k, S.sign_p(k, len(pairs))
    return out


def mean_present(values: list) -> tuple[float | None, int]:
    """The mean of the values that are not None, and how many were None."""
    got = [v for v in values if v is not None]
    return (statistics.fmean(got) if got else None), len(values) - len(got)


def e2e_arm_summary(os_name: str, values: dict) -> list[dict]:
    rows = []
    for payload in PAYLOADS:
        for arm in ARMS:
            got = [values[(arm, payload, r)] for r in REPLICATES if (arm, payload, r) in values]
            if not got:
                continue
            peaks = [g["peak_mib"] for g in got if g["peak_mib"] is not None]
            row = {"os": os_name, "payload": payload, "arm": arm, "replicates": len(got),
                   "peak_mib": statistics.median(peaks) if peaks else None, "peak_missing": len(got) - len(peaks),
                   "share_nulls": 0}
            for load in LOADS:
                mids = [m for g in got for m in g["share"][load]]
                row[f"{load}_rps"] = statistics.fmean(g[load] for g in got)
                row[f"{load}_server_share"], nulls_s = mean_present([m.get("tep_server") for m in mids])
                row[f"{load}_generator_share"], nulls_g = mean_present([m.get(tool_of(os_name)) for m in mids])
                row["share_nulls"] += nulls_s + nulls_g
            if os_name == "linux":
                row["light_latency_median_us"] = statistics.median(g["latency"][0] for g in got)
                row["light_latency_p99_us"] = statistics.median(g["latency"][1] for g in got)
            else:
                row["light_cycle_us"] = 1e6 / row["light_rps"]
            rows.append(row)
    return rows


def cross_check(folder: Path) -> dict:
    """Linux only: ghz against R and P, medium at c12 m128 (Alex's decision 4); a check, not a contrast.
    A missing or failed window is listed, the arm's mean is left out, and nothing stops."""
    out: dict = {"failed_windows": []}
    for arm in ("R", "P"):
        rates = []
        for k in range(1, 5):
            path = folder / f"cross-ghz-{arm}-medium-c12-m128-{k}.txt"
            valid, _, rate = parse_window(path, "windows") if path.is_file() else (False, 0, 0.0)
            if valid:
                rates.append(rate)
            else:
                out["failed_windows"].append(path.name)
        out[arm] = statistics.fmean(rates) if len(rates) == 4 else None
    out["ratio"] = out["R"] / out["P"] if out["R"] and out["P"] else None
    return out


def analyse_e2e(folders: dict[str, Path]) -> dict:
    contrasts, arms, void, touched = [], [], {}, {}
    for os_name in OSES:
        data = load_e2e(folders[os_name], os_name)
        void[os_name], touched[os_name] = data["void"], data["touched"]
        for payload in PAYLOADS:
            for load in LOADS:
                for other in ("P", "Q"):
                    contrasts.append(e2e_contrast(os_name, payload, load, other, data["values"], payload in data["touched"]))
        arms += e2e_arm_summary(os_name, data["values"])
    tests = [c for c in contrasts if c["hypothesis"]]
    pvals = [c["p"] for c in tests]
    for c, adj, ok in zip(tests, S.holm_adjust(pvals), S.holm_pass(pvals)):
        c["holm_adjusted"], c["holm_pass"] = adj, ok
    return {"contrasts": contrasts, "arms": arms, "m": len(tests), "void": void, "touched": touched,
            "cross_check": cross_check(folders["linux"])}


# ---------------------------------------------------------------- section 4, read only

ALLOC_LINE = re.compile(r"^arm ([RPQS]) (small|medium): ([\d.]+) allocations, ([\d.]+) bytes per call$")
ALLOC_ALT = re.compile(r"^arm ([RPQS]) medium alternating with a smaller order: ([\d.]+) allocations per call$")
OPS_LINE = re.compile(r"^(wide|many) (\d+) tables: ops <= (\d+) \(within 0\.5%\)$")


def read_allocs(path: Path) -> dict:
    out = {}
    for line in text_of(path).splitlines():
        if m := ALLOC_LINE.match(line.strip()):
            out[f"{m[1]} {m[2]}"] = float(m[3])
        elif m := ALLOC_ALT.match(line.strip()):
            out[f"{m[1]} alternating"] = float(m[2])
    # server/test_allocs.cpp: every arm at small and medium, and R, P and Q alternating medium with a smaller order.
    want = {f"{a} {p}" for a in ARMS for p in PAYLOADS} | {f"{a} alternating" for a in ("R", "P", "Q")}
    if set(out) != want:
        fail(f"{path}: allocation counts for {sorted(out)}, want {sorted(want)}")
    return out


def read_ops(path: Path) -> dict:
    out = {f"{m[1]} {m[2]}": int(m[3]) for line in text_of(path).splitlines() if (m := OPS_LINE.match(line.strip()))}
    if len(out) != 18:
        fail(f"{path}: {len(out)} counts, want 18")
    return out


# ---------------------------------------------------------------- output


def jsonable(x):
    if isinstance(x, Fraction):
        return float(x)
    if isinstance(x, dict):
        return {str(k): jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [jsonable(v) for v in x]
    return x


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("compile-linux", "compile-windows", "e2e-linux", "e2e-windows"):
        ap.add_argument(f"--{name}", type=Path, required=True)
    ap.add_argument("--allocs", type=Path)
    ap.add_argument("--constexpr-ops", type=Path)
    ap.add_argument("--out", type=Path, default=Path("results"))
    a = ap.parse_args(argv)
    summary = {
        "python": platform.python_version(),
        "alpha": S.ALPHA, "resamples": S.B_RESAMPLES, "level": S.LEVEL,
        "compile": analyse_compile({"linux": a.compile_linux, "windows": a.compile_windows}),
        "e2e": analyse_e2e({"linux": a.e2e_linux, "windows": a.e2e_windows}),
        "allocs": read_allocs(a.allocs) if a.allocs else None,
        "constexpr_ops": read_ops(a.constexpr_ops) if a.constexpr_ops else None,
    }
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "summary.json").write_text(json.dumps(jsonable(summary), indent=1) + "\n", encoding="utf-8", newline="\n")
    (a.out / "macros.tex").write_text(macros.render(summary), encoding="utf-8", newline="\n")
    (a.out / "tables").mkdir(exist_ok=True)
    (a.out / "tables" / "compile.tex").write_text(macros.compile_table(summary), encoding="utf-8", newline="\n")
    (a.out / "tables" / "growth.tex").write_text(macros.growth_table(summary), encoding="utf-8", newline="\n")
    (a.out / "figures").mkdir(exist_ok=True)  # the figure reads summary.json as written, nothing else
    (a.out / "figures" / "compile-ratio.pdf").write_bytes(
        figure.render(json.loads((a.out / "summary.json").read_text(encoding="utf-8"))))
    print(macros.report(summary))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AnalysisError as e:
        print(f"analysis stopped: {e}", file=sys.stderr)
        sys.exit(2)
