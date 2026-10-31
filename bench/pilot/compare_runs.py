"""Compile runs side by side: median seconds per cell, ratios to the first run, R/P per run, phase split.

Usage: python -I compare_runs.py NAME=jsonl[,timereport_dir] ...
The first run is the reference for the ratios.
"""
import json
import os
import re
import statistics
import sys

runs = []
for arg in sys.argv[1:]:
    name, rest = arg.split("=", 1)
    path, _, trdir = rest.partition(",")
    runs.append((name, path, trdir))

data = {}
for name, path, _ in runs:
    for line in open(path, encoding="utf-8"):
        r = json.loads(line)
        if not r.get("timed") or r.get("time_report"):
            continue
        ph = {p["phase"]: p for p in r["phases"]}
        mem = lambda p: p.get("maxrss_kib", p.get("peak_commit_kib"))  # Linux RSS, Windows commit
        if r["gen"] == "R":
            secs = ph["compile"]["wall_ns"] / 1e9
            rss = mem(ph["compile"]) / 1024
        else:
            secs = sum(ph[k]["wall_ns"] for k in ("protoc", "compile_pb", "compile_user")) / 1e9
            rss = max(mem(ph[k]) for k in ("protoc", "compile_pb", "compile_user")) / 1024
        data.setdefault((r["gen"], r["shape"], r["n"]), {}).setdefault(name, []).append((secs, rss))

med = statistics.median
ref = runs[0][0]
print(f"per cell: median [min..max] s (n); ratio of medians to {ref}; RSS MiB medians")
for key in sorted(data):
    row = data[key]
    parts, rss = [], []
    t0 = med([t for t, _ in row[ref]]) if ref in row else None
    for name, _, _ in runs:
        v = row.get(name)
        if not v:
            parts.append(f"{name} -")
            rss.append("-")
            continue
        ts = [t for t, _ in v]
        rr = f" x{med(ts) / t0:.3f}" if t0 and name != ref else ""
        parts.append(f"{name} {med(ts):7.2f} [{min(ts):.2f}..{max(ts):.2f}]({len(ts)}){rr}")
        rss.append(f"{med([m for _, m in v]):.0f}")
    print(f"{key[0]} {key[1]:4} {key[2]:5} " + " | ".join(parts) + " | MiB " + ", ".join(rss))
print()
print("R/P per run (medians) and separation of builds")
for shape in ("wide", "many"):
    for n in sorted({k[2] for k in data}):
        out = []
        for name, _, _ in runs:
            r = data.get(("R", shape, n), {}).get(name)
            p = data.get(("P", shape, n), {}).get(name)
            if r and p:
                rt, pt = [t for t, _ in r], [t for t, _ in p]
                sep = "R<P all" if max(rt) < min(pt) else ("R>P all" if min(rt) > max(pt) else "overlap")
                out.append(f"{name} {med(rt) / med(pt):.3f} ({sep})")
        if out:
            print(f"  {shape} {n}: " + "; ".join(out))
print()
print("phase split of the R time-report builds: seconds, share of TOTAL wall (phases >= 5%)")
pat = re.compile(r"^\s*(phase [^:]+?|TOTAL)\s*:.*?(\d+\.\d+)\s*\(\s*\d+%\)\s*wall")
for name, _, d in runs:
    if not d:
        continue
    for cell in ("many_1024", "many_2048", "wide_1024"):
        path = os.path.join(d, f"R_{cell}_0.time-report.log")
        if not os.path.exists(path):
            continue
        vals = {}
        for line in open(path, encoding="utf-8", errors="replace"):
            m = pat.match(line)
            if m:
                vals[m.group(1).strip()] = float(m.group(2))
        tot = vals.get("TOTAL")
        if not tot:
            print(f"  {name} {cell}: no TOTAL")
            continue
        parts = ", ".join(f"{k.replace('phase ', '')} {v:.1f} s ({100 * v / tot:.0f}%)"
                          for k, v in vals.items() if k != "TOTAL" and v >= 0.05 * tot)
        print(f"  {name} {cell}: TOTAL {tot:.1f} s; {parts}")
