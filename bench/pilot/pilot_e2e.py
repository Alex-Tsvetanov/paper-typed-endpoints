"""End-to-end pilot analysis (pilots 4 and 5): R and Q twice per payload in mirrored order, S and P once.

Windows 1 to 3 are an arm's first run, 4 to 6 its second (R and Q only). Prints per-run means,
pooled means, ratios, window separation, the comparison with the earlier pilot and c1 m1 latency.
Usage: python -I pilot_e2e.py <pilot out dir> <earlier pilot out dir>
"""
import os
import re
import statistics
import sys

OUT, OLD = sys.argv[1], sys.argv[2]
LOADS = [(1, 1), (12, 1), (12, 8), (12, 32), (12, 128), (48, 32), (48, 128)]
PAYLOADS = ["small", "medium"]
fin = re.compile(r"finished in ([\d.]+)s, ([\d.]+) req/s")


def rate(d, arm, payload, c, m, k):
    path = os.path.join(d, f"h2load-{arm}-{payload}-c{c}-m{m}-{k}.txt")
    t = open(path, encoding="utf-8", errors="replace").read()
    return float(fin.search(t).group(2))


def windows(d, arm, payload, c, m, ks):
    return [rate(d, arm, payload, c, m, k) for k in ks]


mean = statistics.fmean
print("per run means (req/s); spread over the run's 3 windows; R and Q: run 1 | run 2 | pooled")
for p in PAYLOADS:
    for c, m in LOADS:
        row = []
        for a in ("S", "R", "Q", "P"):
            r1 = windows(OUT, a, p, c, m, (1, 2, 3))
            if a in ("R", "Q"):
                r2 = windows(OUT, a, p, c, m, (4, 5, 6))
                row.append(f"{a} {mean(r1):8.0f} | {mean(r2):8.0f} ({100 * (mean(r2) / mean(r1) - 1):+.1f}%)")
            else:
                row.append(f"{a} {mean(r1):8.0f}")
        print(f"{p:6} c{c:<3}m{m:<4} " + "   ".join(row))
print()
print("pooled ratios: R/P, R/Q, Q/P (R and Q over 6 windows, P over 3); separation of windows")


def sep(x, y, nx, ny):
    if min(x) > max(y):
        return f"{nx}>{ny} all"
    if min(y) > max(x):
        return f"{ny}>{nx} all"
    return "overlap"


for p in PAYLOADS:
    for c, m in LOADS:
        r = windows(OUT, "R", p, c, m, range(1, 7))
        q = windows(OUT, "Q", p, c, m, range(1, 7))
        pp = windows(OUT, "P", p, c, m, (1, 2, 3))
        s = windows(OUT, "S", p, c, m, (1, 2, 3))
        print(f"{p:6} c{c:<3}m{m:<4} R/P {mean(r) / mean(pp):.3f} ({sep(r, pp, 'R', 'P')})  R/Q {mean(r) / mean(q):.3f} "
              f"({sep(r, q, 'R', 'Q')})  Q/P {mean(q) / mean(pp):.3f}  R/S {mean(r) / mean(s):.3f}")
print()
print("against pilot 4 (same cell, same windows: R and Q pooled over 6, S and P over 3)")
for p in PAYLOADS:
    for c, m in LOADS:
        parts = []
        for a in ("S", "R", "Q", "P"):
            new = windows(OUT, a, p, c, m, range(1, 7) if a in ("R", "Q") else (1, 2, 3))
            old = windows(OLD, a, p, c, m, range(1, 7) if a in ("R", "Q") else (1, 2, 3))
            parts.append(f"{a} {mean(new) / mean(old):.3f}")
        print(f"{p:6} c{c:<3}m{m:<4} " + "  ".join(parts))
print()
print("latency, c1 m1 (microseconds): p50 p99 p99.9 mean over all windows of the arm")


def pct(v, q):
    return v[min(len(v) - 1, max(0, int(round(q * (len(v) - 1)))))]


for p in PAYLOADS:
    for a in ("S", "R", "Q", "P"):
        vals, non200 = [], 0
        for k in (range(1, 7) if a in ("R", "Q") else (1, 2, 3)):
            with open(os.path.join(OUT, f"latency-{a}-{p}-{k}.tsv"), encoding="utf-8") as fh:
                for line in fh:
                    f = line.split("\t")
                    if len(f) < 3:
                        continue
                    non200 += f[1] != "200"
                    vals.append(int(f[2]))
        vals.sort()
        print(f"{p:6} {a} n {len(vals):8} non200 {non200} p50 {pct(vals, .5)} p99 {pct(vals, .99)} "
              f"p99.9 {pct(vals, .999)} mean {mean(vals):.1f}")
