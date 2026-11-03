# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""P5's gRPC C++ arm analysis: hypotheses-m2.md, sections 2 to 4.

Reads the unpacked `out` folder of the arm G campaign (Laptop request 36, bench/requests/grpc.md),
which ran arms R, P, Q, S and G on Linux in the first campaign's procedure; writes
results-m2/summary.json and results-m2/macros.tex and prints a report. Standard library only.

  python -I analysis/analyse_m2.py --g-linux D [--out results-m2]

Window files, latency logs, shares, peaks, validity, voids and reruns are those of the first
campaign's Linux run (analyse.py), for five arms; G's server log ends in `g_server: calls ok N,
failed 0` instead of tep_server's line. Choices hypotheses-m2.md leaves open, fixed here before any
G window ran:
- The R against G test is two-sided: p = min(1, 2 min(P(X >= k), P(X <= k))), k the replicates with
  R's rate above G's (a tie counts as not above).
- A run void twice makes every contrast of its payload descriptive, as analyse.py does.
- Bootstrap seeds: seed_of("P5 bootstrap grpc linux <payload> <load> <arm>/G").
"""
from __future__ import annotations

import argparse
import json
import platform
import re
import sys
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyse as A  # noqa: E402
import macros as M  # noqa: E402
import stats as S  # noqa: E402

ORDER_SHA256 = "bd28e6a7447d88f06622238e2265020f2cbc6b1b3cf03d899ac5fca8d3b1994a"  # bench/campaign-m2/
ARMS = ("R", "P", "Q", "S", "G")
SERVER_LAST = re.compile(r"^(?:tep_server|g_server): calls ok (\d+), failed 0$")
FILE_PATTERN = re.compile(r"^(h2load|latency|server)-[RPQSG]-(small|medium)-")
VOID_LINE = re.compile(r"^void ([RPQSG] (?:small|medium) r\d+x?)$")


def two_sided_p(k: int, n: int) -> Fraction:
    """hypotheses-m2.md section 3: the exact two-sided sign test's p for k of n replicates above."""
    return min(Fraction(1), 2 * min(S.sign_p(k, n), S.sign_p(n - k, n)))


def check_run(folder: Path, arm: str, payload: str, tag: str, rcs: dict) -> tuple[bool, dict]:
    """analyse.check_run for five arms: the same windows, checks and bound, either server's last line."""
    valid, total, slack, rates = True, 0, 0, {"light": [], "heavy": []}
    for name, c, m, k in A.run_windows("linux", arm, payload, tag):
        path = folder / name
        if not path.is_file():
            valid = False
            continue
        if name not in rcs:
            A.fail(f"{folder}: shares.jsonl has no line for {name}")
        ok, count, rate = A.parse_window(path, "linux")
        valid = valid and ok and rcs[name] == 0
        total += count
        slack += 2 * c * m
        if k != "warmup":
            rates["light" if (c, m) == A.LOADS["light"] else "heavy"].append(rate)
    log = folder / f"server-{arm}-{payload}-{tag}.log"
    lines = log.read_text(encoding="utf-8", errors="replace").strip().splitlines() if log.is_file() else []
    last = SERVER_LAST.match(lines[-1].strip()) if lines else None
    valid = valid and bool(last) and total <= int(last.group(1)) <= total + slack
    return valid, rates


def load(folder: Path) -> dict:
    order = A.read_order(folder, "g", "linux")
    if len(order) != 100:
        A.fail(f"{folder}: the order has {len(order)} rows, want 100")
    shares = A.read_keyed(folder / "shares.jsonl", ("file", "rc", "mid"), lambda s: s["file"])
    rcs = {name: s["rc"] for name, s in shares.items()}
    peaks = A.read_keyed(folder / "peak.jsonl", ("arm", "payload", "rep", "rerun", "kib"),
                         lambda p: (p["arm"], p["payload"], p["rep"], bool(p["rerun"])))
    runs, void, touched, values = [], set(), set(), {}
    for row in order:
        arm, payload, rep = row["arm"], row["payload"], int(row["rep"])
        for tag in (f"r{rep}", f"r{rep}x"):
            runs.append((arm, payload, tag))
            valid, rates = check_run(folder, arm, payload, tag, rcs)
            if valid:
                entry = A.run_entry(folder, "linux", arm, payload, rep, tag, rates, shares, peaks)
                if arm == "G":  # G's process is g_server; the summary reads the server's share as tep_server's
                    entry["share"] = {k: [dict(m, tep_server=m.get("g_server")) for m in v] for k, v in entry["share"].items()}
                values[(arm, payload, rep)] = entry
                break
            void.add(f"{arm} {payload} {tag}")
        else:
            touched.add(payload)
    stray = {p.name for p in folder.iterdir() if FILE_PATTERN.match(p.name)} - A.possible_files("linux", runs)
    if stray:
        A.fail(f"{folder}: unexpected files {sorted(stray)[:5]}")
    named = {m[1] for line in A.text_of(folder / "validate.txt").splitlines() if (m := VOID_LINE.match(line.strip()))}
    if named != void:
        A.fail(f"{folder}: validate.txt's void lines differ from the analysis's")
    return {"values": values, "void": sorted(void), "touched": sorted(touched)}


def contrast(payload: str, load_name: str, arm: str, values: dict, touched: bool) -> dict:
    reps = [r for r in A.REPLICATES if (arm, payload, r) in values and ("G", payload, r) in values]
    pairs = [(values[(arm, payload, r)][load_name], values[("G", payload, r)][load_name]) for r in reps]
    hypothesis = arm == "R" and len(reps) == 10 and not touched
    out = {"payload": payload, "load": load_name, "arm": arm, "replicates": len(reps), "hypothesis": hypothesis,
           "touched": touched}
    out |= A.interval(pairs, S.ratio_of_means, f"P5 bootstrap grpc linux {payload} {load_name} {arm}/G")
    if hypothesis:
        k = sum(a > g for a, g in pairs)  # a tie counts as not above
        out["above"], out["p"] = k, two_sided_p(k, len(pairs))
    return out


def analyse(folder: Path) -> dict:
    data = load(folder)
    contrasts = [contrast(payload, load_name, arm, data["values"], payload in data["touched"])
                 for payload in A.PAYLOADS for load_name in A.LOADS for arm in ("R", "P", "Q")]
    tests = [c for c in contrasts if c["hypothesis"]]
    pvals = [c["p"] for c in tests]
    for c, adj, ok in zip(tests, S.holm_adjust(pvals), S.holm_pass(pvals)):
        c["holm_adjusted"], c["holm_pass"] = adj, ok
        c["direction"] = ("R above" if c["ratio"] > 1 else "R below") if ok else None
    saved = A.ARMS
    try:
        A.ARMS = ARMS  # analyse.e2e_arm_summary walks A.ARMS
        arms = A.e2e_arm_summary("linux", data["values"])
    finally:
        A.ARMS = saved
    return {"contrasts": contrasts, "arms": arms, "m": len(tests), "void": data["void"], "touched": data["touched"]}


def render(summary: dict) -> str:
    g = summary["grpc"]
    rows = [("GrpcM", str(g["m"])), ("GrpcSeedOrder", str(S.seed_of("P5 grpc e2e linux")))]
    for c in g["contrasts"]:
        base = f"Grpc{M.cap(c['payload'])}{M.cap(c['load'])}{c['arm']}vsG"
        rows += [(base + "Ratio", M.ratio(c["ratio"])), (base + "Low", M.ratio(c["low"])), (base + "High", M.ratio(c["high"])),
                 (base + "Reps", str(c["replicates"]))]
        if c["arm"] == "R":
            rows += [(base + "Above", str(c.get("above", M.NONE))), (base + "P", M.pval(c.get("p"))),
                     (base + "Pass", M.yes(c.get("holm_pass")))]
    for a in g["arms"]:
        base = f"Grpc{M.cap(a['payload'])}{a['arm']}"
        rows += [(base + "LightRps", M.whole(a["light_rps"])), (base + "HeavyRps", M.whole(a["heavy_rps"])),
                 (base + "LightSharePct", M.one(a["light_server_share"])), (base + "HeavySharePct", M.one(a["heavy_server_share"])),
                 (base + "HeavyGenSharePct", M.one(a["heavy_generator_share"])), (base + "Peak", M.one(a["peak_mib"])),
                 (base + "LatMed", M.one(a["light_latency_median_us"])), (base + "LatNinetyNine", M.one(a["light_latency_p99_us"]))]
    names = [n for n, _ in rows]
    if len(names) != len(set(names)):
        raise ValueError("two macros share a name")
    head = "% Generated by analysis/analyse_m2.py (hypotheses-m2.md). Do not edit.\n"
    return head + "".join(f"\\newcommand{{\\{n}}}{{{v}}}\n" for n, v in rows)


def report(summary: dict) -> str:
    g = summary["grpc"]
    lines = [f"gRPC C++ arm (Linux): m = {g['m']}; void runs {g['void']}; touched {g['touched']}"]
    for c in g["contrasts"]:
        test = (f"{c['above']}/{c['replicates']} above, two-sided p {M.pval(c['p'])} Holm {M.yes(c['holm_pass'])}"
                if c["hypothesis"] else "descriptive")
        lines.append(f"  {c['payload']:6} {c['load']:5} {c['arm']}/G {M.ratio(c['ratio'])} [{M.ratio(c['low'])}, {M.ratio(c['high'])}]  {test}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--g-linux", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=Path("results-m2"))
    a = ap.parse_args(argv)
    A.ORDER_SHA256 = {("g", "linux"): ORDER_SHA256}  # read_order looks the digest up when it runs
    summary = {"python": platform.python_version(), "alpha": S.ALPHA, "resamples": S.B_RESAMPLES, "level": S.LEVEL,
               "grpc": analyse(a.g_linux)}
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "summary.json").write_text(json.dumps(A.jsonable(summary), indent=1) + "\n", encoding="utf-8", newline="\n")
    (a.out / "macros.tex").write_text(render(summary), encoding="utf-8", newline="\n")
    print(report(summary))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except A.AnalysisError as e:
        print(f"analysis stopped: {e}", file=sys.stderr)
        sys.exit(2)
