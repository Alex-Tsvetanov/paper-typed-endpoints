"""Applies the direction rule of hypotheses.md section 2 to compile pilots: per OS and cell, the
ratio of median whole-build seconds R/P (every phase, from .proto to the linked program) and
whether every R build is on one side of every P build. Pilots only; nothing here is a result.

Usage: python -I directions.py pilot.jsonl [pilot.jsonl ...]
Builds from all files are pooled per (OS, shape, n, ops_limit of R); time-report, untimed and
unchecked lines are skipped, and so are Windows builds that are not valid by section 2 (a phase
not pinned to CPU 14, or timed out). The OS is read from the memory field: maxrss_kib on Linux,
peak_commit_kib on Windows.
"""
import json
import statistics
import sys

builds: dict[tuple[str, str, int], dict[str, list[float]]] = {}
limits: dict[tuple[str, str, int], set[int]] = {}
invalid = 0
for path in sys.argv[1:]:
    with open(path, encoding="utf-8") as lines:
        for line in lines:
            if not line.strip():
                continue
            r = json.loads(line)
            if not r.get("timed") or r.get("time_report") or not r.get("checked"):
                continue
            os_name = "windows" if "peak_commit_kib" in r["phases"][0] else "linux"
            if os_name == "windows" and any(p.get("affinity") != "0x4000" or p.get("timed_out") for p in r["phases"]):
                invalid += 1  # hypotheses.md section 2: a Windows build counts only on CPU 14, none timed out
                continue
            cell = (os_name, r["shape"], r["n"])
            seconds = sum(p["wall_ns"] for p in r["phases"]) / 1e9
            builds.setdefault(cell, {}).setdefault(r["gen"], []).append(seconds)
            if r["gen"] == "R":
                limits.setdefault(cell, set()).add(r["ops_limit"])

print(f"Windows builds left out (affinity not 0x4000, or a phase timed out): {invalid}")
print("os      shape     n   R/P   builds R,P  direction")
for cell, by_gen in sorted(builds.items()):
    os_name, shape, n = cell
    rs, ps = by_gen.get("R", []), by_gen.get("P", [])
    if len(limits.get(cell, set())) > 1:
        print(f"{os_name:7} {shape:5} {n:5}   -     {len(rs)},{len(ps)}       none (R builds differ in ops_limit)")
        continue
    if not rs or not ps:
        print(f"{os_name:7} {shape:5} {n:5}   -     {len(rs)},{len(ps)}       none (a generator has no build)")
        continue
    ratio = statistics.median(rs) / statistics.median(ps)
    if max(rs) < min(ps):
        direction = "R faster (separated)"
    elif min(rs) > max(ps):
        direction = "R slower (separated)"
    else:
        direction = "none (overlap): descriptive"
    print(f"{os_name:7} {shape:5} {n:5} {ratio:5.3f}   {len(rs)},{len(ps)}       {direction}")
