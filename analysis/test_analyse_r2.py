# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""Tests of the replication and mixed-traffic analysis (analyse_r2.py) on synthetic folders written in
the requests' formats (bench/requests/replication.md, mixed-traffic.md), with the real order files of
bench/campaign-r2/. No timing: every number here is made up, except the first campaign's committed
results/summary.json and macros.tex, which the family rule and the macro names are checked against.
Run: python -I -m unittest discover -s analysis -p "test_*.py"
"""
from __future__ import annotations

import copy
import hashlib
import json
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyse as A  # noqa: E402
import analyse_r2 as R2  # noqa: E402
import macros as M  # noqa: E402
import stats as S  # noqa: E402
import test_analyse as T  # noqa: E402

ORDERS_R2 = HERE.parent / "bench" / "campaign-r2"
FIRST = json.loads((HERE.parent / "results" / "summary.json").read_text(encoding="utf-8"))
FIRST_MACROS = (HERE.parent / "results" / "macros.tex").read_text(encoding="utf-8")
MIX_RATE = {"R": 1.2, "P": 1.0, "Q": 1.1, "S": 1.3}


def setUpModule():
    global SAVED
    SAVED = (S.B_RESAMPLES, dict(A.ORDER_SHA256), T.ORDERS)
    S.B_RESAMPLES = 200  # enough to exercise the code; the campaign uses 10,000
    A.ORDER_SHA256 = dict(R2.ORDER_SHA256)
    T.ORDERS = ORDERS_R2  # test_analyse's synthetic writers then follow the replication's orders


def tearDownModule():
    S.B_RESAMPLES, A.ORDER_SHA256, T.ORDERS = SAVED


def macros_of(text: str) -> dict[str, str]:
    return dict(re.findall(r"^\\newcommand\{\\([A-Za-z]+)\}\{(.*)\}$", text, re.M))


class FamiliesTest(unittest.TestCase):
    def test_the_rule_on_the_first_campaign_gives_the_frozen_families(self):
        directions, hypotheses = R2.families_from(FIRST)
        self.assertEqual(hypotheses, A.E2E_HYPOTHESES)
        self.assertEqual(len(hypotheses), 7)
        self.assertEqual(sum(d is not None for d in directions.values()), 36)
        R2.check_families(FIRST)

    def test_a_different_first_campaign_stops_the_analysis(self):
        moved = copy.deepcopy(FIRST)
        c = next(c for c in moved["e2e"]["contrasts"]
                 if (c["os"], c["payload"], c["load"], c["other"]) == ("windows", "medium", "light", "Q"))
        c["low"] = 1.001  # its interval now lies above 1, so the rule adds a hypothesis
        with self.assertRaises(A.AnalysisError):
            R2.check_families(moved)
        moved = copy.deepcopy(FIRST)
        next(a for a in moved["e2e"]["arms"] if a["os"] == "windows")["heavy_server_share"] = 99.0
        R2.check_families(moved)  # one saturated arm is not every arm: the families stay

    def test_the_order_files_are_the_named_ones(self):
        for (experiment, os_name), digest in R2.ORDER_SHA256.items():
            path = ORDERS_R2 / f"{experiment}-order-{os_name}.tsv"
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)
            self.assertNotEqual(digest, SAVED[1].get((experiment, os_name)))


def write_mixed_run(folder: Path, arm: str, rep: int, tag: str, mode: str, shares: list, peaks: list) -> None:
    """One mixed server run. mode: ok; bad (one window not clean); rc (one generator's exit status 7);
    over (the server counts more calls than the bound allows); nostart (a log, and nothing else)."""
    log = folder / f"server-{arm}-mix-{tag}.log"
    if mode == "nostart":
        log.write_text("tep_server: bind failed\n")
        return
    total = 0
    for k in ("warmup", "1", "2", "3", "4"):
        rc = {}
        for body, part in (("medium", 0.6), ("smaller", 0.4)):
            rate = MIX_RATE[arm] * 100000 * part
            count = int(rate * 10)
            name = f"h2load-{arm}-mix{body}-c6-m128-{tag}-{k}.txt"
            (folder / name).write_text(T.window_text("linux", count, rate, not (mode == "bad" and k == "3")))
            total += count
            rc[body] = 7 if (mode == "rc" and k == "2" and body == "smaller") else 0
        shares.append({"window": f"{arm}-{tag}-{k}", "arm": arm, "rep": rep, "rerun": tag.endswith("x"), "k": k, "rc": rc,
                       "around": {"tep_server": 90.0, "h2load": 800.0}, "mid": {"tep_server": 96.0, "h2load": 850.0}})
    counted = total + (10 * 2 * 6 * 128 + 1 if mode == "over" else 7)
    log.write_text(f"tep_server: arm {arm}, port 50051, 1 worker(s)\ntep_server: calls ok {counted}, failed 0\n")
    peaks.append({"arm": arm, "rep": rep, "rerun": tag.endswith("x"), "field": "VmHWM", "kib": 30720})


def write_mixed(folder: Path, void: dict | None = None) -> None:
    """void maps (arm, rep) to the modes of its first run and its rerun, e.g. ("bad", "ok")."""
    folder.mkdir(parents=True)
    shutil.copy(ORDERS_R2 / "mixed-order-linux.tsv", folder)
    shares, peaks, lines = [], [], []
    for row in T.order_rows("mixed", "linux"):
        arm, rep = row["arm"], int(row["rep"])
        first, rerun = (void or {}).get((arm, rep), ("ok", None))
        write_mixed_run(folder, arm, rep, f"r{rep}", first, shares, peaks)
        if first != "ok":
            lines.append(f"void {arm} mix r{rep}")
            write_mixed_run(folder, arm, rep, f"r{rep}x", rerun, shares, peaks)
            if rerun != "ok":
                lines.append(f"void {arm} mix r{rep}x")
    (folder / "mixed-shares.jsonl").write_text("".join(json.dumps(s) + "\n" for s in shares))
    (folder / "mixed-peak.jsonl").write_text("".join(json.dumps(p) + "\n" for p in peaks))
    (folder / "validate.txt").write_text("checks: synthetic\n" + "".join(v + "\n" for v in lines))


class MixedTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_clean_campaign(self):
        write_mixed(self.tmp / "m")
        out = R2.analyse_mixed(self.tmp / "m")
        self.assertEqual(out["m"], 2)
        for c in out["contrasts"]:
            self.assertEqual((c["replicates"], c["in_direction"], c["holm_pass"]), (10, 10, True))
            self.assertAlmostEqual(c["ratio"], MIX_RATE["R"] / MIX_RATE[c["other"]], places=3)
        r = next(a for a in out["arms"] if a["arm"] == "R")
        self.assertAlmostEqual(r["rps"], 120000, places=0)
        self.assertAlmostEqual(r["smaller_share"], 0.4, places=3)
        self.assertEqual((r["server_share"], r["generator_share"], r["peak_mib"]), (96.0, 850.0, 30.0))

    def test_void_runs_with_valid_reruns_keep_their_replicates(self):
        write_mixed(self.tmp / "m", {("Q", 4): ("bad", "ok"), ("R", 2): ("nostart", "ok"), ("P", 9): ("rc", "ok"),
                                     ("S", 1): ("over", "ok")})
        out = R2.analyse_mixed(self.tmp / "m")
        self.assertEqual(out["void"], ["P mix r9", "Q mix r4", "R mix r2", "S mix r1"])
        self.assertEqual(out["m"], 2)
        self.assertTrue(all(c["replicates"] == 10 for c in out["contrasts"]))

    def test_a_run_void_twice_makes_the_contrasts_descriptive(self):
        write_mixed(self.tmp / "m", {("S", 3): ("bad", "bad")})
        out = R2.analyse_mixed(self.tmp / "m")
        self.assertEqual((out["m"], out["touched"]), (0, True))
        self.assertTrue(all(not c["hypothesis"] and c["replicates"] == 10 for c in out["contrasts"]))

    def test_mismatches_stop_the_analysis(self):
        write_mixed(self.tmp / "a")
        (self.tmp / "a" / "h2load-R-mixsmaller-c6-m128-r3x-1.txt").write_text("stray")
        write_mixed(self.tmp / "b", {("Q", 4): ("bad", "ok")})
        (self.tmp / "b" / "validate.txt").write_text("checks: synthetic\n")
        write_mixed(self.tmp / "c")
        path = self.tmp / "c" / "mixed-shares.jsonl"
        path.write_text("".join(path.read_text().splitlines(keepends=True)[1:]))
        for name in ("a", "b", "c"):
            with self.subTest(name), self.assertRaises(A.AnalysisError):
                R2.analyse_mixed(self.tmp / name)


class ReplicationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_analyse_py_reads_the_replication_orders(self):
        T.write_e2e(self.tmp / "l", "linux")
        T.write_e2e(self.tmp / "w", "windows")
        out = A.analyse_e2e({"linux": self.tmp / "l", "windows": self.tmp / "w"})
        self.assertEqual(out["m"], 7)
        T.write_compile(self.tmp / "cl", "linux")
        T.write_compile(self.tmp / "cw", "windows")
        self.assertEqual(A.analyse_compile({"linux": self.tmp / "cl", "windows": self.tmp / "cw"})["m"], 36)

    def test_macros_are_the_first_campaigns_names_with_the_prefix(self):
        write_mixed(self.tmp / "m")
        summary = dict(FIRST, mixed=R2.analyse_mixed(self.tmp / "m"))
        r2 = macros_of(R2.render(summary))
        first = macros_of(FIRST_MACROS)
        self.assertFalse(set(r2) & set(first))
        for name, value in r2.items():
            self.assertTrue(name.startswith(("Rep", "Mix")), name)
            if name.startswith("Rep") and not name.startswith("RepSeedOrder"):
                self.assertEqual(value, first[name[3:]], name)  # same summary, same value
        self.assertEqual(r2["RepSeedOrderEndToEndLinux"], str(S.seed_of("P5 replication e2e linux")))
        self.assertEqual(r2["MixLinuxRvsQInDir"], "10")
        self.assertNotIn("RepAlpha", r2)
        names = {n for n, _ in R2.replication_rows(summary)}
        for make in (M.compile_table, M.growth_table):
            used = set(re.findall(r"\\([A-Za-z]+)\{\}", R2.prefixed(make(summary), names)))
            self.assertTrue(used and used <= set(r2), sorted(used - set(r2))[:3])


if __name__ == "__main__":
    unittest.main()
