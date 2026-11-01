"""Windows end-to-end pilot analysis (Laptop request 15, ghz): pilot 5's design, R and Q twice per
payload in mirrored order, S and P once. Windows 1 to 3 are an arm's first run, 4 to 6 its second.

Prints, per payload and load: pooled means, R/P and R/Q with window separation, the server's and
ghz's CPU share (percent of one CPU, labrec.process_shares, mean of the windows' mid samples), the
c1 m1 latency percentiles ghz reports, and the rule of hypotheses.md section 3 for E1 and E2.
Pilot numbers; nothing here is a result.
Usage: python -I pilot_e2e_windows.py <pilot out dir>
"""
import csv
import os
import re
import statistics
import sys

OUT = sys.argv[1]
LOADS = [(1, 1), (12, 1), (12, 8), (12, 32), (12, 128), (48, 32), (48, 128)]
PAYLOADS = ["small", "medium"]
RUNS = {"R": range(1, 7), "Q": range(1, 7), "S": (1, 2, 3), "P": (1, 2, 3)}
mean = statistics.fmean


def ghz(arm, payload, c, m, k):
    with open(os.path.join(OUT, f"ghz-{arm}-{payload}-c{c}-m{m}-{k}.txt"), encoding="utf-8", errors="replace") as f:
        text = f.read()
    rps = float(re.search(r"Requests/sec:\s+([\d.]+)", text).group(1))
    pct = {q: re.search(rf"\s{q} % in ([\d.]+ \w+)", text) for q in (50, 99)}
    statuses = re.findall(r"\[(\w+)\]\s+\d+ responses", text)
    return rps, {q: (v.group(1) if v else None) for q, v in pct.items()}, statuses


shares: dict[tuple, tuple[float, float]] = {}
with open(os.path.join(OUT, "shares-summary.tsv"), encoding="utf-8") as f:
    for row in csv.DictReader(f, delimiter="\t"):
        shares[(row["arm"], row["payload"], int(row["c"]), int(row["m"]), row["k"])] = (
            float(row["server_mid"]), float(row["ghz_mid"]))


def rates(arm, payload, c, m):
    """Requests per second of the arm's valid windows: a window with any status other than OK is left out."""
    return [rps for rps, _, statuses in (ghz(arm, payload, c, m, k) for k in RUNS[arm]) if set(statuses) == {"OK"}]


def sep(x, y, nx, ny):
    if min(x) > max(y):
        return f"{nx}>{ny} all"
    if min(y) > max(x):
        return f"{ny}>{nx} all"
    return "overlap"


bad = [(a, p, c, m, k) for a in RUNS for p in PAYLOADS for c, m in LOADS for k in RUNS[a]
       if set(ghz(a, p, c, m, k)[2]) != {"OK"}]
print(f"windows with a status other than OK: {len(bad)}")
print()
print("pooled req/s (R, Q over 6 windows; S, P over 3); ratios with window separation; CPU share % (server, ghz)")
for p in PAYLOADS:
    for c, m in LOADS:
        r, q, pp, s = (rates(a, p, c, m) for a in ("R", "Q", "P", "S"))
        srv = {a: mean(shares[(a, p, c, m, str(k))][0] for k in RUNS[a]) for a in RUNS}
        cli = mean(shares[(a, p, c, m, str(k))][1] for a in RUNS for k in RUNS[a])
        print(f"{p:6} c{c:<3}m{m:<4} S {mean(s):7.0f} R {mean(r):7.0f} Q {mean(q):7.0f} P {mean(pp):7.0f} | "
              f"R/P {mean(r) / mean(pp):.3f} ({sep(r, pp, 'R', 'P')}) R/Q {mean(r) / mean(q):.3f} ({sep(r, q, 'R', 'Q')}) | "
              f"server S {srv['S']:.0f} R {srv['R']:.0f} Q {srv['Q']:.0f} P {srv['P']:.0f}, ghz {cli:.0f}")
print()
print("c1 m1 latency reported by ghz (p50, p99 per window)")
for p in PAYLOADS:
    for a in ("S", "R", "Q", "P"):
        lat = [ghz(a, p, 1, 1, k)[1] for k in RUNS[a]]
        print(f"{p:6} {a}: " + "  ".join(f"{d[50]}/{d[99]}" for d in lat))
print()
print("hypotheses.md section 3 rule on this pilot")
# E1 needs the server at a CPU share of at least 0.95 at c12 m128 for every arm and payload.
saturated = all(shares[(a, p, 12, 128, str(k))][0] >= 95.0 for p in PAYLOADS for a in ("R", "Q", "P") for k in RUNS[a])
for p, c, m, name in (("small", 12, 128, "E1"), ("medium", 12, 128, "E1"), ("medium", 1, 1, "E2")):
    r, q, pp = (rates(a, p, c, m) for a in ("R", "Q", "P"))
    for other, x in (("P", pp), ("Q", q)):
        separated = min(r) > max(x)
        needs = name == "E1"
        verdict = "hypothesis" if separated and (saturated or not needs) else "descriptive"
        why = [] if separated else ["windows not separated with R ahead"]
        if needs and not saturated:
            why.append("server CPU share below 95% in some window")
        print(f"{name} {p} c{c} m{m} R vs {other}: {verdict}" + (f" ({'; '.join(why)})" if why else ""))
