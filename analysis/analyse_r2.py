# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""P5's replication and mixed-traffic analysis: hypotheses-r2.md, sections 2 to 5.

Reads the unpacked `out` folders of the replication's four jobs (bench/requests/replication.md: the
compile and end-to-end campaigns again, at the same measured code, in new seeded orders) and of the
mixed-traffic job (bench/requests/mixed-traffic.md); writes results-r2/summary.json,
results-r2/macros.tex, results-r2/tables/compile.tex and growth.tex, and prints a report. Standard
library only.

  python -I analysis/analyse_r2.py --compile-linux D --compile-windows D --e2e-linux D --e2e-windows D
                                   --mixed-linux D [--first results/summary.json] [--out results-r2]

The replication is analysed by analyse.py's own code (frozen at c5a326b), unchanged: only the order
files' digests differ. Its families are hypotheses-r2.md section 2's rule applied to the first
campaign's summary (results/summary.json); families_from() applies the rule and the analysis stops
unless the result equals analyse.py's tables (DIRECTION, E2E_HYPOTHESES). Every replication macro is
analyse.py's macro with the prefix Rep; the mixed-traffic macros are named Mix<Field>.

Choices hypotheses-r2.md leaves open, fixed here before any replication or mixed window ran:
- A mixed window is two h2load processes started together, one per body (medium, smaller), each at
  c6 m128. Its value is the sum of the two processes' "req/s"; a server run's value is the mean of
  its 4 windows' values (the warm-up is discarded).
- Validity of a mixed server run: all 10 window files present and clean, each generator's exit
  status 0 (`rc` in mixed-shares.jsonl), and the server log's last line `calls ok N, failed 0` with
  S <= N <= S + 2 c m summed over the 10 files (S: the sum of the generators' counts, warm-up
  included). A void run is replaced by its rerun when the rerun is valid; a run void twice makes
  the mixed contrasts descriptive.
- The smaller-body share of a run: the smaller body's count over both counts, windows 1 to 4.
- Shares: tep_server's `mid` share and h2load's (the sum of both processes), means over windows 1 to 4.
- Bootstrap seeds: seed_of("P5 bootstrap mixed linux R/<arm>") for the mixed contrasts; the
  replication keeps analyse.py's labels (its data are new, its seeds need not be).
"""
from __future__ import annotations

import argparse
import json
import platform
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyse as A  # noqa: E402
import macros as M  # noqa: E402
import stats as S  # noqa: E402

ORDER_SHA256 = {  # bench/campaign-r2/, the replication's orders (bench/requests/replication.md)
    ("compile", "linux"): "5885d91d04254bb15ab39465407cc0cfd4ff9a12dbe7d03ab4a0a8b14f44f1bd",
    ("compile", "windows"): "198a0eaa7ee6f21d93bde2a6d614bd56482db7375504792b9ce126b0b4605b52",
    ("e2e", "linux"): "ddca9587882483566890cb760d0ac18046d919e509680feeb888b1b67c3e6cf7",
    ("e2e", "windows"): "c856033d4a94f07245e30ee54d3e3e2b77453e0db3d33e20a36a3a943e768351",
    ("mixed", "linux"): "ab0c2e84a44a2d0af4641017d413eba974991a2ed35864ed94f124cf3efe572f",
}
PREFIX = "Rep"
SHARE_RULE = 95.0  # hypotheses.md decision (e), percent of one CPU at c12 m128, every arm and payload
BODIES = ("medium", "smaller")
MIX = (6, 128)  # each generator's connections and streams (hypotheses-r2.md section 4)
MIX_ARMS = ("R", "P", "Q", "S")
MIX_LINE = re.compile(r"^(void) ([RPQS] mix r\d+x?)$")
MIX_FILE = re.compile(r"^(h2load|server)-[RPQS]-mix")


# ---------------------------------------------------------------- section 2: the families, from the first campaign


def families_from(first: dict) -> tuple[dict, set]:
    """hypotheses-r2.md section 2: a contrast carries a direction when the first campaign's 95% interval
    lies wholly on one side of 1, in that direction; small at c1 m1 has none (Alex's Q2), and a Windows
    c12 m128 contrast has none unless the first campaign's server share there was at least 95% for every
    arm and payload (decision (e)). Returns the compile directions by (os, shape, n) and the end-to-end
    hypotheses as analyse.py names them."""
    def side(c: dict) -> str | None:
        return "above" if c["low"] > 1 else "below" if c["high"] < 1 else None

    directions = {(c["os"], c["shape"], c["n"]): {"below": "faster", "above": "slower", None: None}[side(c)]
                  for c in first["compile"]["cells"]}
    shares = {(a["os"], a["payload"], a["arm"]): a["heavy_server_share"] for a in first["e2e"]["arms"]}
    saturated = all((shares.get(("windows", p, arm)) or 0) >= SHARE_RULE for p in A.PAYLOADS for arm in A.ARMS)
    hypotheses = set()
    for c in first["e2e"]["contrasts"]:
        if (c["payload"], c["load"]) == ("small", "light") or ((c["os"], c["load"]) == ("windows", "heavy") and not saturated):
            continue
        if side(c) == "below":
            A.fail(f"{c['os']} {c['payload']} {c['load']} R/{c['other']}: R serves fewer; analyse.py tests only 'more'")
        if side(c) == "above":
            hypotheses.add((c["os"], c["payload"], c["load"], c["other"]))
    return directions, hypotheses


def check_families(first: dict) -> None:
    directions, hypotheses = families_from(first)
    want = {(o, shape, n): A.DIRECTION[(shape, n)] for o in A.OSES for shape, n in A.CELLS}
    if directions != want or hypotheses != A.E2E_HYPOTHESES:
        A.fail("the first campaign's families by hypotheses-r2.md section 2 differ from analyse.py's tables")


# ---------------------------------------------------------------- section 4: mixed traffic (Linux)


def mixed_windows(arm: str, tag: str) -> list[tuple[str, str, str]]:
    """The 5 windows of one mixed server run, warm-up first, each as (file, body, k)."""
    return [(f"h2load-{arm}-mix{body}-c{MIX[0]}-m{MIX[1]}-{tag}-{k}.txt", body, k)
            for k in ("warmup", "1", "2", "3", "4") for body in BODIES]


def check_mixed_run(folder: Path, arm: str, tag: str, shares: dict) -> tuple[bool, dict]:
    """Validity of one mixed server run and its windows' values (section 4)."""
    valid, total, slack = True, 0, 0
    rates = {k: 0.0 for k in ("1", "2", "3", "4")}
    counts = {body: 0 for body in BODIES}
    for name, body, k in mixed_windows(arm, tag):
        path = folder / name
        if not path.is_file():
            valid = False
            continue
        window = f"{arm}-{tag}-{k}"
        if window not in shares:
            A.fail(f"{folder}: mixed-shares.jsonl has no line for {window}")
        ok, count, rate = A.parse_window(path, "linux")
        valid = valid and ok and shares[window]["rc"].get(body) == 0
        total += count
        slack += 2 * MIX[0] * MIX[1]
        if k != "warmup":
            rates[k] += rate
            counts[body] += count
    log = folder / f"server-{arm}-mix-{tag}.log"
    lines = log.read_text(encoding="utf-8", errors="replace").strip().splitlines() if log.is_file() else []
    last = A.SERVER_LAST.match(lines[-1].strip()) if lines else None
    valid = valid and bool(last) and total <= int(last.group(1)) <= total + slack
    both = sum(counts.values())
    return valid, {"rps": statistics.fmean(rates.values()), "smaller_share": counts["smaller"] / both if both else None}


def load_mixed(folder: Path) -> dict:
    order = A.read_order(folder, "mixed", "linux")
    if len(order) != 40:
        A.fail(f"{folder}: the order has {len(order)} rows, want 40")
    shares = A.read_keyed(folder / "mixed-shares.jsonl", ("window", "rc", "mid"), lambda s: s["window"])
    peaks = A.read_keyed(folder / "mixed-peak.jsonl", ("arm", "rep", "rerun", "kib"),
                         lambda p: (p["arm"], p["rep"], bool(p["rerun"])))
    runs, void, values, touched = [], set(), {}, False
    for row in order:
        arm, rep = row["arm"], int(row["rep"])
        for tag in (f"r{rep}", f"r{rep}x"):
            runs.append((arm, tag))
            valid, entry = check_mixed_run(folder, arm, tag, shares)
            if valid:
                break
            void.add(f"{arm} mix {tag}")
        else:
            touched = True
            continue
        key = (arm, rep, tag.endswith("x"))
        if key not in peaks:
            A.fail(f"{folder}: mixed-peak.jsonl has no line for {key}")
        kib = peaks[key]["kib"]
        mids = [shares[f"{arm}-{tag}-{k}"]["mid"] for k in ("1", "2", "3", "4")]
        entry |= {"peak_mib": None if kib is None else kib / 1024,
                  "server_share": A.mean_present([m.get("tep_server") for m in mids])[0],
                  "generator_share": A.mean_present([m.get("h2load") for m in mids])[0]}
        values[(arm, rep)] = entry
    expected = {n for arm, tag in runs for n, _, _ in mixed_windows(arm, tag)} | {f"server-{arm}-mix-{tag}.log" for arm, tag in runs}
    stray = {p.name for p in folder.iterdir() if MIX_FILE.match(p.name)} - expected
    if stray:
        A.fail(f"{folder}: unexpected files {sorted(stray)[:5]}")
    named = {m[2] for line in A.text_of(folder / "validate.txt").splitlines() if (m := MIX_LINE.match(line.strip()))}
    if named != void:
        A.fail(f"{folder}: validate.txt's void lines differ from the analysis's")
    return {"values": values, "void": sorted(void), "touched": touched}


def analyse_mixed(folder: Path) -> dict:
    data = load_mixed(folder)
    values, contrasts = data["values"], []
    for other in ("P", "Q"):
        reps = [r for r in A.REPLICATES if ("R", r) in values and (other, r) in values]
        pairs = [(values[("R", r)]["rps"], values[(other, r)]["rps"]) for r in reps]
        hypothesis = len(reps) == 10 and not data["touched"]
        c = {"other": other, "replicates": len(reps), "hypothesis": hypothesis, "touched": data["touched"]}
        c |= A.interval(pairs, S.ratio_of_means, f"P5 bootstrap mixed linux R/{other}")
        if hypothesis:
            k = sum(r > o for r, o in pairs)  # a tie counts against
            c["in_direction"], c["p"] = k, S.sign_p(k, len(pairs))
        contrasts.append(c)
    tests = [c for c in contrasts if c["hypothesis"]]
    pvals = [c["p"] for c in tests]
    for c, adj, ok in zip(tests, S.holm_adjust(pvals), S.holm_pass(pvals)):
        c["holm_adjusted"], c["holm_pass"] = adj, ok
    arms = []
    for arm in MIX_ARMS:
        got = [values[(arm, r)] for r in A.REPLICATES if (arm, r) in values]
        if not got:
            continue
        def mean(key: str) -> float | None:
            return A.mean_present([g[key] for g in got])[0]
        peaks = [g["peak_mib"] for g in got if g["peak_mib"] is not None]
        arms.append({"arm": arm, "replicates": len(got), "rps": mean("rps"), "smaller_share": mean("smaller_share"),
                     "server_share": mean("server_share"), "generator_share": mean("generator_share"),
                     "peak_mib": statistics.median(peaks) if peaks else None})
    return {"contrasts": contrasts, "arms": arms, "m": len(tests), "void": data["void"], "touched": data["touched"]}


# ---------------------------------------------------------------- macros


def mixed_macros(mixed: dict) -> list[tuple[str, str]]:
    out = [("MixM", str(mixed["m"])), ("MixSeedOrder", str(S.seed_of("P5 mixed e2e linux")))]
    for c in mixed["contrasts"]:
        base = f"MixLinuxRvs{c['other']}"
        out += [(base + "Ratio", M.ratio(c["ratio"])), (base + "Low", M.ratio(c["low"])), (base + "High", M.ratio(c["high"])),
                (base + "Reps", str(c["replicates"])), (base + "InDir", str(c.get("in_direction", M.NONE))),
                (base + "P", M.pval(c.get("p"))), (base + "Pass", M.yes(c.get("holm_pass")))]
    for a in mixed["arms"]:
        base = f"MixLinux{a['arm']}"
        out += [(base + "Rps", M.whole(a["rps"])),
                (base + "SmallerPct", M.one(None if a["smaller_share"] is None else 100 * a["smaller_share"])),
                (base + "SharePct", M.one(a["server_share"])), (base + "GenSharePct", M.one(a["generator_share"])),
                (base + "Peak", M.one(a["peak_mib"]))]
    return out


def replication_rows(summary: dict) -> list[tuple[str, str]]:
    """analyse.py's macros of the replication, without the constants the first campaign's macros.tex
    already holds (Alpha, Resamples) and with the replication's order seeds."""
    seeds = [(f"SeedOrder{word}{M.cap(o)}", str(S.seed_of(f"P5 replication {e} {o}")))
             for e, word in (("compile", "Compile"), ("e2e", "EndToEnd")) for o in A.OSES]
    rows = [("CompM", str(summary["compile"]["m"])), ("EM", str(summary["e2e"]["m"]))] + seeds
    return rows + M.compile_macros(summary) + M.e2e_macros(summary) + M.phase_macros(summary)


def prefixed(text: str, names: set[str]) -> str:
    """A table body of analyse.py's macros, each name given the prefix."""
    return re.sub(r"\\([A-Za-z]+)", lambda m: "\\" + (PREFIX + m[1] if m[1] in names else m[1]), text)


def render(summary: dict) -> str:
    rows = [(PREFIX + n, v) for n, v in replication_rows(summary)] + mixed_macros(summary["mixed"])
    names = [n for n, _ in rows]
    if len(names) != len(set(names)):
        raise ValueError("two macros share a name")
    head = "% Generated by analysis/analyse_r2.py (hypotheses-r2.md). Do not edit.\n"
    return head + "".join(f"\\newcommand{{\\{n}}}{{{v}}}\n" for n, v in rows)


def report(summary: dict) -> str:
    lines = ["replication (analyse.py's report of the new data):", M.report(summary)]
    x = summary["mixed"]
    lines.append(f"mixed (Linux, medium with smaller, 2 x c6 m128): m = {x['m']}; void runs {x['void']}; touched {x['touched']}")
    for c in x["contrasts"]:
        test = (f"{c['in_direction']}/{c['replicates']} p {M.pval(c['p'])} Holm {M.yes(c['holm_pass'])}"
                if c["hypothesis"] else "descriptive")
        lines.append(f"  R/{c['other']} {M.ratio(c['ratio'])} [{M.ratio(c['low'])}, {M.ratio(c['high'])}]  {test}")
    for a in x["arms"]:
        lines.append(f"  {a['arm']} {M.whole(a['rps'])} req/s, smaller {M.one(None if a['smaller_share'] is None else 100 * a['smaller_share'])}%")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("compile-linux", "compile-windows", "e2e-linux", "e2e-windows", "mixed-linux"):
        ap.add_argument(f"--{name}", type=Path, required=True)
    ap.add_argument("--first", type=Path, default=Path("results") / "summary.json")
    ap.add_argument("--out", type=Path, default=Path("results-r2"))
    a = ap.parse_args(argv)
    check_families(json.loads(a.first.read_text(encoding="utf-8")))
    A.ORDER_SHA256 = dict(ORDER_SHA256)  # read_order looks the digests up when it runs
    summary = {
        "python": platform.python_version(),
        "alpha": S.ALPHA, "resamples": S.B_RESAMPLES, "level": S.LEVEL,
        "compile": A.analyse_compile({"linux": a.compile_linux, "windows": a.compile_windows}),
        "e2e": A.analyse_e2e({"linux": a.e2e_linux, "windows": a.e2e_windows}),
        "mixed": analyse_mixed(a.mixed_linux),
    }
    names = {n for n, _ in replication_rows(summary)}
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "summary.json").write_text(json.dumps(A.jsonable(summary), indent=1) + "\n", encoding="utf-8", newline="\n")
    (a.out / "macros.tex").write_text(render(summary), encoding="utf-8", newline="\n")
    (a.out / "tables").mkdir(exist_ok=True)
    for table, make in (("compile.tex", M.compile_table), ("growth.tex", M.growth_table)):
        text = prefixed(make(summary), names).replace("analysis/analyse.py (analysis/macros.py)", "analysis/analyse_r2.py")
        (a.out / "tables" / table).write_text(text, encoding="utf-8", newline="\n")
    print(report(summary))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except A.AnalysisError as e:
        print(f"analysis stopped: {e}", file=sys.stderr)
        sys.exit(2)
