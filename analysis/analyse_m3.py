# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""P5's second-load-generator analysis: hypotheses-m3.md, sections 2 to 5.

Reads the unpacked `out` folders of the oha pilot (Laptop request 32) and, when its gate passed, of
the oha campaign (request 33, bench/requests/oha.md); writes results-m3/summary.json and
results-m3/macros.tex and prints a report. Standard library only.

  python -I analysis/analyse_m3.py --pilot-linux D [--oha-linux D] [--replication results-r2/summary.json]
                                   [--out results-m3]

Choices hypotheses-m3.md leaves open, fixed here before any oha window ran:
- A window is one oha JSON file. It is clean when `errorDistribution` is empty and
  `statusCodeDistribution` holds only "200"; its count is that 200 count, and its rate is the count
  over `summary.total`, the seconds oha ran, which on HTTP/2 include the calls still in flight at
  the deadline. A server run's value is the mean of its 4 windows' rates (the warm-up discarded).
- Validity of a server run: every window file present and clean, oha's exit status 0 (`rc` in
  oha-shares.jsonl), and the server log's last line `calls ok N, failed 0` with S <= N <= S + 2 c m
  per window (S: the sum of oha's counts, warm-up included). Void runs and reruns as analyse.py.
- The gate (section 2): the pilot's runs all valid, and for every arm and payload the mean of
  tep_server's `mid` share over the pilot's 2 windows at least 95% of one CPU.
- Shares: tep_server's `mid` share and oha's, means over windows 1 to 4.
- Bootstrap seeds: seed_of("P5 bootstrap oha linux <payload> heavy R/<arm>").
- Against the replication (descriptive, when --replication is given): each arm's oha rate over its
  h2load rate at c12 m128 in the replication's Linux campaign.
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

ORDER_SHA256 = "20faa8f1d7e52c2e420917b2ebb91953da5d1400cc66a36bfd4f780382424d62"  # bench/campaign-m3/
LOAD = (12, 128)
SHARE_RULE = 95.0  # percent of one CPU (hypotheses.md decision (e), hypotheses-m3.md section 2)
PILOT_ROWS = [(payload, arm) for payload in ("medium", "small") for arm in A.ARMS]
OHA_FILE = re.compile(r"^(oha|server)-[RPQS]-(small|medium)-")


def oha_window(path: Path) -> tuple[bool, int, float]:
    """(clean, count of 200 responses, 200 responses per second) of one oha JSON file."""
    try:
        data = json.loads(A.text_of(path))
        codes, errors, total = data["statusCodeDistribution"], data["errorDistribution"], float(data["summary"]["total"])
    except (ValueError, KeyError, TypeError):
        return False, 0, 0.0
    count = int(codes.get("200", 0))
    clean = not errors and set(codes) == {"200"} and total > 0
    return clean, count, count / total if total > 0 else 0.0


def windows(arm: str, payload: str, tag: str, ks: tuple[str, ...]) -> list[tuple[str, str]]:
    return [(f"oha-{arm}-{payload}-c{LOAD[0]}-m{LOAD[1]}-{tag}-{k}.json", k) for k in ks]


def check_run(folder: Path, arm: str, payload: str, tag: str, ks: tuple[str, ...], shares: dict) -> tuple[bool, dict]:
    valid, total, slack, rates, mids = True, 0, 0, [], []
    for name, k in windows(arm, payload, tag, ks):
        path = folder / name
        if not path.is_file():
            valid = False
            continue
        if name not in shares:
            A.fail(f"{folder}: oha-shares.jsonl has no line for {name}")
        ok, count, rate = oha_window(path)
        valid = valid and ok and shares[name]["rc"] == 0
        total += count
        slack += 2 * LOAD[0] * LOAD[1]
        if k != "warmup":
            rates.append(rate)
            mids.append(shares[name]["mid"])
    log = folder / f"server-{arm}-{payload}-{tag}.log"
    lines = log.read_text(encoding="utf-8", errors="replace").strip().splitlines() if log.is_file() else []
    last = A.SERVER_LAST.match(lines[-1].strip()) if lines else None
    valid = valid and bool(last) and total <= int(last.group(1)) <= total + slack
    return valid, {"rps": statistics.fmean(rates) if len(rates) == len(ks) - 1 else None,
                   "server_share": A.mean_present([m.get("tep_server") for m in mids])[0],
                   "generator_share": A.mean_present([m.get("oha") for m in mids])[0]}


def stray_check(folder: Path, expected: set[str]) -> None:
    stray = {p.name for p in folder.iterdir() if OHA_FILE.match(p.name)} - expected
    if stray:
        A.fail(f"{folder}: unexpected files {sorted(stray)[:5]}")


def analyse_pilot(folder: Path) -> dict:
    """Section 2: the gate. One server run per payload and arm: a warm-up and 2 windows."""
    shares = A.read_keyed(folder / "oha-shares.jsonl", ("file", "rc", "mid"), lambda s: s["file"])
    rows, expected = [], set()
    for payload, arm in PILOT_ROWS:
        valid, entry = check_run(folder, arm, payload, "pilot", ("warmup", "1", "2"), shares)
        rows.append({"payload": payload, "arm": arm, "valid": valid} | entry)
        expected |= {n for n, _ in windows(arm, payload, "pilot", ("warmup", "1", "2"))} | {f"server-{arm}-{payload}-pilot.log"}
    stray_check(folder, expected)
    passed = all(r["valid"] and (r["server_share"] or 0) >= SHARE_RULE for r in rows)
    said = A.text_of(folder / "gate.txt").strip()
    if said != ("pass" if passed else "fail"):
        A.fail(f"{folder}: gate.txt says {said!r}, the analysis {'pass' if passed else 'fail'}")
    shares_seen = [r["server_share"] for r in rows if r["server_share"] is not None]
    return {"rows": rows, "pass": passed, "min_server_share": min(shares_seen) if shares_seen else None}


def analyse_campaign(folder: Path) -> dict:
    """Sections 3 and 4: the campaign's contrasts, R against P and Q at c12 m128 for both payloads."""
    order = A.read_order(folder, "oha", "linux")
    if len(order) != 80:
        A.fail(f"{folder}: the order has {len(order)} rows, want 80")
    shares = A.read_keyed(folder / "oha-shares.jsonl", ("file", "rc", "mid"), lambda s: s["file"])
    ks = ("warmup", "1", "2", "3", "4")
    runs, void, touched, values = [], set(), set(), {}
    for row in order:
        arm, payload, rep = row["arm"], row["payload"], int(row["rep"])
        for tag in (f"r{rep}", f"r{rep}x"):
            runs.append((arm, payload, tag))
            valid, entry = check_run(folder, arm, payload, tag, ks, shares)
            if valid:
                values[(arm, payload, rep)] = entry
                break
            void.add(f"{arm} {payload} {tag}")
        else:
            touched.add(payload)
    stray_check(folder, {n for a, p, t in runs for n, _ in windows(a, p, t, ks)} | {f"server-{a}-{p}-{t}.log" for a, p, t in runs})
    if A.read_validate(folder, "e2e")["void"] != void:
        A.fail(f"{folder}: validate.txt's void lines differ from the analysis's")
    contrasts = []
    for payload in A.PAYLOADS:
        for other in ("P", "Q"):
            reps = [r for r in A.REPLICATES if ("R", payload, r) in values and (other, payload, r) in values]
            pairs = [(values[("R", payload, r)]["rps"], values[(other, payload, r)]["rps"]) for r in reps]
            hypothesis = len(reps) == 10 and payload not in touched
            c = {"payload": payload, "other": other, "replicates": len(reps), "hypothesis": hypothesis,
                 "touched": payload in touched}
            c |= A.interval(pairs, S.ratio_of_means, f"P5 bootstrap oha linux {payload} heavy R/{other}")
            if hypothesis:
                k = sum(r > o for r, o in pairs)  # a tie counts against
                c["in_direction"], c["p"] = k, S.sign_p(k, len(pairs))
            contrasts.append(c)
    tests = [c for c in contrasts if c["hypothesis"]]
    pvals = [c["p"] for c in tests]
    for c, adj, ok in zip(tests, S.holm_adjust(pvals), S.holm_pass(pvals)):
        c["holm_adjusted"], c["holm_pass"] = adj, ok
    arms = []
    for payload in A.PAYLOADS:
        for arm in A.ARMS:
            got = [values[(arm, payload, r)] for r in A.REPLICATES if (arm, payload, r) in values]
            if got:
                arms.append({"payload": payload, "arm": arm, "replicates": len(got),
                             "rps": statistics.fmean(g["rps"] for g in got),
                             "server_share": A.mean_present([g["server_share"] for g in got])[0],
                             "generator_share": A.mean_present([g["generator_share"] for g in got])[0]})
    return {"contrasts": contrasts, "arms": arms, "m": len(tests), "void": sorted(void), "touched": sorted(touched)}


def against_replication(campaign: dict, replication: dict) -> list[dict]:
    """Descriptive: each arm's oha rate over its h2load rate at c12 m128 in the replication (Linux)."""
    h2load = {(a["payload"], a["arm"]): a["heavy_rps"] for a in replication["e2e"]["arms"] if a["os"] == "linux"}
    return [{"payload": a["payload"], "arm": a["arm"],
             "ratio": a["rps"] / h2load[(a["payload"], a["arm"])] if h2load.get((a["payload"], a["arm"])) else None}
            for a in campaign["arms"]]


def render(summary: dict) -> str:
    pilot = summary["pilot"]
    rows = [("OhaGate", M.yes(pilot["pass"])), ("OhaPilotMinSharePct", M.one(pilot["min_server_share"])),
            ("OhaSeedOrder", str(S.seed_of("P5 oha e2e linux")))]
    rows += [(f"OhaPilot{M.cap(r['payload'])}{r['arm']}SharePct", M.one(r["server_share"])) for r in pilot["rows"]]
    camp = summary.get("campaign")
    if camp:
        rows.append(("OhaM", str(camp["m"])))
        for c in camp["contrasts"]:
            base = f"Oha{M.cap(c['payload'])}Rvs{c['other']}"
            rows += [(base + "Ratio", M.ratio(c["ratio"])), (base + "Low", M.ratio(c["low"])), (base + "High", M.ratio(c["high"])),
                     (base + "Reps", str(c["replicates"])), (base + "InDir", str(c.get("in_direction", M.NONE))),
                     (base + "P", M.pval(c.get("p"))), (base + "Pass", M.yes(c.get("holm_pass")))]
        for a in camp["arms"]:
            base = f"Oha{M.cap(a['payload'])}{a['arm']}"
            rows += [(base + "Rps", M.whole(a["rps"])), (base + "SharePct", M.one(a["server_share"])),
                     (base + "GenSharePct", M.one(a["generator_share"]))]
        rows += [(f"OhaOverRep{M.cap(x['payload'])}{x['arm']}", M.ratio(x["ratio"])) for x in summary.get("against_replication") or []]
    names = [n for n, _ in rows]
    if len(names) != len(set(names)):
        raise ValueError("two macros share a name")
    head = "% Generated by analysis/analyse_m3.py (hypotheses-m3.md). Do not edit.\n"
    return head + "".join(f"\\newcommand{{\\{n}}}{{{v}}}\n" for n, v in rows)


def report(summary: dict) -> str:
    pilot = summary["pilot"]
    lines = [f"oha pilot: gate {'pass' if pilot['pass'] else 'fail'}; lowest server share {M.one(pilot['min_server_share'])}%"]
    lines += [f"  {r['payload']:6} {r['arm']} valid {r['valid']} server {M.one(r['server_share'])}%" for r in pilot["rows"]]
    camp = summary.get("campaign")
    if camp:
        lines.append(f"oha campaign: m = {camp['m']}; void runs {camp['void']}; touched {camp['touched']}")
        for c in camp["contrasts"]:
            test = (f"{c['in_direction']}/{c['replicates']} p {M.pval(c['p'])} Holm {M.yes(c['holm_pass'])}"
                    if c["hypothesis"] else "descriptive")
            lines.append(f"  {c['payload']:6} R/{c['other']} {M.ratio(c['ratio'])} [{M.ratio(c['low'])}, {M.ratio(c['high'])}]  {test}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pilot-linux", type=Path, required=True)
    ap.add_argument("--oha-linux", type=Path)
    ap.add_argument("--replication", type=Path)
    ap.add_argument("--out", type=Path, default=Path("results-m3"))
    a = ap.parse_args(argv)
    A.ORDER_SHA256 = {("oha", "linux"): ORDER_SHA256}  # read_order looks the digest up when it runs
    summary = {"python": platform.python_version(), "alpha": S.ALPHA, "resamples": S.B_RESAMPLES, "level": S.LEVEL,
               "pilot": analyse_pilot(a.pilot_linux)}
    if summary["pilot"]["pass"] != bool(a.oha_linux):
        A.fail("a campaign folder is given exactly when the gate passed")
    if a.oha_linux:
        summary["campaign"] = analyse_campaign(a.oha_linux)
        if a.replication:
            summary["against_replication"] = against_replication(
                summary["campaign"], json.loads(a.replication.read_text(encoding="utf-8")))
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
