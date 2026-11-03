# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""Tests of the gRPC C++ arm analysis (analyse_m2.py) on synthetic folders in the first campaign's
Linux format with a fifth arm, G, and the real order file of bench/campaign-m2/. No timing: every
number here is made up. Run: python -I -m unittest discover -s analysis -p "test_*.py"
"""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import sys
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyse as A  # noqa: E402
import analyse_m2 as M2  # noqa: E402
import stats as S  # noqa: E402
import test_analyse as T  # noqa: E402

ORDER = HERE.parent / "bench" / "campaign-m2" / "g-order-linux.tsv"
FIRST_MACROS = (HERE.parent / "results" / "macros.tex").read_text(encoding="utf-8")


def setUpModule():
    global SAVED
    SAVED = (S.B_RESAMPLES, A.ORDER_SHA256, dict(T.RATE))
    S.B_RESAMPLES = 200  # enough to exercise the code; the campaign uses 10,000
    A.ORDER_SHA256 = {("g", "linux"): M2.ORDER_SHA256}


def tearDownModule():
    S.B_RESAMPLES, A.ORDER_SHA256 = SAVED[0], SAVED[1]
    T.RATE.clear()
    T.RATE.update(SAVED[2])


def write_g(folder: Path, void: dict | None = None) -> None:
    """The campaign folder; void maps (arm, payload, rep) to the modes of its run and rerun."""
    folder.mkdir(parents=True)
    shutil.copy(ORDER, folder)
    lines_of = [ln for ln in ORDER.read_text(encoding="ascii").splitlines() if ln and not ln.startswith("#")]
    head = lines_of[0].split("\t")
    shares, peaks, lines = [], [], []
    for row in (dict(zip(head, ln.split("\t"))) for ln in lines_of[1:]):
        arm, payload, rep = row["arm"], row["payload"], int(row["rep"])
        first, rerun = (void or {}).get((arm, payload, rep), ("ok", None))
        T.write_run(folder, "linux", arm, payload, rep, f"r{rep}", first, shares, peaks)
        if first != "ok":
            lines.append(f"void {arm} {payload} r{rep}")
            T.write_run(folder, "linux", arm, payload, rep, f"r{rep}x", rerun, shares, peaks)
            if rerun != "ok":
                lines.append(f"void {arm} {payload} r{rep}x")
    for s in shares:  # G's server process is g_server (bench/requests/grpc.md)
        if s["arm"] == "G":
            s["mid"] = {"g_server": 91.0, "h2load": s["mid"]["h2load"]}
    (folder / "shares.jsonl").write_text("".join(json.dumps(s) + "\n" for s in shares))
    (folder / "peak.jsonl").write_text("".join(json.dumps(p) + "\n" for p in peaks))
    (folder / "validate.txt").write_text("checks: synthetic\n" + "".join(v + "\n" for v in lines))


class GrpcTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        T.RATE["G"] = 0.5

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_order_and_two_sided_p(self):
        self.assertEqual(hashlib.sha256(ORDER.read_bytes()).hexdigest(), M2.ORDER_SHA256)
        self.assertEqual([M2.two_sided_p(k, 10) for k in (10, 0, 9, 5)],
                         [Fraction(2, 1024), Fraction(2, 1024), Fraction(22, 1024), Fraction(1)])

    def test_clean_campaign_r_above(self):
        write_g(self.tmp / "g")
        out = M2.analyse(self.tmp / "g")
        self.assertEqual(out["m"], 4)
        for c in out["contrasts"]:
            self.assertAlmostEqual(c["ratio"], T.RATE[c["arm"]] / T.RATE["G"], places=3)
            self.assertEqual(c["hypothesis"], c["arm"] == "R")
            if c["hypothesis"]:
                self.assertEqual((c["above"], c["holm_pass"], c["direction"]), (10, True, "R above"))
        self.assertEqual({a["arm"] for a in out["arms"]}, set(M2.ARMS))
        g = next(a for a in out["arms"] if (a["payload"], a["arm"]) == ("medium", "G"))
        r = next(a for a in out["arms"] if (a["payload"], a["arm"]) == ("medium", "R"))
        self.assertEqual((g["heavy_server_share"], r["heavy_server_share"], g["share_nulls"]), (91.0, 95.0, 0))

    def test_g_above_r_passes_the_other_way(self):
        T.RATE["G"] = 2.0
        write_g(self.tmp / "g")
        out = M2.analyse(self.tmp / "g")
        tested = [c for c in out["contrasts"] if c["hypothesis"]]
        self.assertTrue(tested and all((c["above"], c["holm_pass"], c["direction"]) == (0, True, "R below") for c in tested))

    def test_voids(self):
        write_g(self.tmp / "g", {("G", "medium", 2): ("bad", "ok"), ("R", "small", 5): ("bad", "nostart")})
        out = M2.analyse(self.tmp / "g")
        self.assertEqual(out["void"], ["G medium r2", "R small r5", "R small r5x"])
        self.assertEqual((out["m"], out["touched"]), (2, ["small"]))

    def test_macros(self):
        write_g(self.tmp / "g")
        A.ORDER_SHA256 = {}
        self.assertEqual(M2.main(["--g-linux", str(self.tmp / "g"), "--out", str(self.tmp / "out")]), 0)
        A.ORDER_SHA256 = {("g", "linux"): M2.ORDER_SHA256}
        text = (self.tmp / "out" / "macros.tex").read_text()
        names = re.findall(r"^\\newcommand\{\\([A-Za-z]+)\}", text, re.M)
        first = set(re.findall(r"^\\newcommand\{\\([A-Za-z]+)\}", FIRST_MACROS, re.M))
        self.assertTrue(names and all(n.startswith("Grpc") for n in names) and not set(names) & first)
        self.assertIn("\\newcommand{\\GrpcMediumHeavyRvsGPass}{yes}", text)
        self.assertIn("\\newcommand{\\GrpcM}{4}", text)


if __name__ == "__main__":
    unittest.main()
