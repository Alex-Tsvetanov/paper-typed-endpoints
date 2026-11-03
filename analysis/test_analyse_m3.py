# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""Tests of the second-load-generator analysis (analyse_m3.py) on synthetic folders written in the
format of bench/requests/oha.md, with the real order file of bench/campaign-m3/. No timing: every
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
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyse as A  # noqa: E402
import analyse_m3 as M3  # noqa: E402
import stats as S  # noqa: E402

ORDER = HERE.parent / "bench" / "campaign-m3" / "oha-order-linux.tsv"
FIRST_MACROS = (HERE.parent / "results" / "macros.tex").read_text(encoding="utf-8")
RATE = {"R": 1.2, "P": 1.0, "Q": 1.1, "S": 1.3}


def setUpModule():
    global SAVED
    SAVED = (S.B_RESAMPLES, A.ORDER_SHA256)
    S.B_RESAMPLES = 200  # enough to exercise the code; the campaign uses 10,000
    A.ORDER_SHA256 = {("oha", "linux"): M3.ORDER_SHA256}


def tearDownModule():
    S.B_RESAMPLES, A.ORDER_SHA256 = SAVED


def oha_json(count: int, total: float, mode: str = "ok") -> str:
    codes = {"200": count} if mode != "status" else {"200": count, "503": 3}
    errors = {"connection closed": 2} if mode == "error" else {}
    return json.dumps({"summary": {"successRate": 1.0, "total": total, "requestsPerSec": count / total},
                       "statusCodeDistribution": codes, "errorDistribution": errors})


def write_run(folder: Path, arm: str, payload: str, tag: str, ks: tuple, mode: str, shares: list, share: float = 97.0) -> None:
    """mode: ok; bad (an error in window 1); rc (oha's exit status 3 in window 1); nostart (a log only)."""
    log = folder / f"server-{arm}-{payload}-{tag}.log"
    if mode == "nostart":
        log.write_text("tep_server: bind failed\n")
        return
    total = 0
    for name, k in M3.windows(arm, payload, tag, ks):
        count = int(RATE[arm] * (2 if payload == "small" else 1) * 100000 * 10)
        (folder / name).write_text(oha_json(count, 10.0, "error" if (mode == "bad" and k == "1") else "ok"))
        total += count
        shares.append({"file": name, "arm": arm, "payload": payload, "k": k, "rc": 3 if (mode == "rc" and k == "1") else 0,
                       "around": {"tep_server": 90.0, "oha": 900.0}, "mid": {"tep_server": share, "oha": 950.0}})
    log.write_text(f"tep_server: arm {arm}, port 50051, 1 worker(s)\ntep_server: calls ok {total + 3}, failed 0\n")


def write_pilot(folder: Path, low_share: float | None = None, gate: str | None = None) -> None:
    folder.mkdir(parents=True)
    shares = []
    for payload, arm in M3.PILOT_ROWS:
        share = low_share if (low_share is not None and (payload, arm) == ("small", "P")) else 97.0
        write_run(folder, arm, payload, "pilot", ("warmup", "1", "2"), "ok", shares, share)
    (folder / "oha-shares.jsonl").write_text("".join(json.dumps(s) + "\n" for s in shares))
    passed = low_share is None or low_share >= 95.0
    (folder / "gate.txt").write_text((gate or ("pass" if passed else "fail")) + "\n")


def order_rows() -> list[dict]:
    lines = [ln for ln in ORDER.read_text(encoding="ascii").splitlines() if ln and not ln.startswith("#")]
    head = lines[0].split("\t")
    return [dict(zip(head, ln.split("\t"))) for ln in lines[1:]]


def write_campaign(folder: Path, void: dict | None = None) -> None:
    folder.mkdir(parents=True)
    shutil.copy(ORDER, folder)
    shares, lines = [], []
    ks = ("warmup", "1", "2", "3", "4")
    for row in order_rows():
        arm, payload, rep = row["arm"], row["payload"], int(row["rep"])
        first, rerun = (void or {}).get((arm, payload, rep), ("ok", None))
        write_run(folder, arm, payload, f"r{rep}", ks, first, shares)
        if first != "ok":
            lines.append(f"void {arm} {payload} r{rep}")
            write_run(folder, arm, payload, f"r{rep}x", ks, rerun, shares)
            if rerun != "ok":
                lines.append(f"void {arm} {payload} r{rep}x")
    (folder / "oha-shares.jsonl").write_text("".join(json.dumps(s) + "\n" for s in shares))
    (folder / "validate.txt").write_text("checks: synthetic\n" + "".join(v + "\n" for v in lines))


class OhaTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_order_file_is_the_named_one(self):
        self.assertEqual(hashlib.sha256(ORDER.read_bytes()).hexdigest(), M3.ORDER_SHA256)
        self.assertEqual(len(order_rows()), 80)

    def test_window_cleanliness(self):
        for mode, clean in (("ok", True), ("error", False), ("status", False)):
            path = self.tmp / f"{mode}.json"
            path.write_text(oha_json(1000, 10.0, mode))
            self.assertEqual(M3.oha_window(path)[0], clean, mode)
        (self.tmp / "torn.json").write_text("{")
        self.assertEqual(M3.oha_window(self.tmp / "torn.json"), (False, 0, 0.0))
        self.assertEqual(M3.oha_window(self.tmp / "ok.json")[1:], (1000, 100.0))

    def test_gate(self):
        write_pilot(self.tmp / "pass")
        self.assertTrue(M3.analyse_pilot(self.tmp / "pass")["pass"])
        write_pilot(self.tmp / "fail", low_share=90.0)
        out = M3.analyse_pilot(self.tmp / "fail")
        self.assertEqual((out["pass"], out["min_server_share"]), (False, 90.0))
        write_pilot(self.tmp / "said", low_share=90.0, gate="pass")
        with self.assertRaises(A.AnalysisError):
            M3.analyse_pilot(self.tmp / "said")

    def test_clean_campaign(self):
        write_campaign(self.tmp / "c")
        out = M3.analyse_campaign(self.tmp / "c")
        self.assertEqual(out["m"], 4)
        for c in out["contrasts"]:
            self.assertEqual((c["replicates"], c["in_direction"], c["holm_pass"]), (10, 10, True))
            self.assertAlmostEqual(c["ratio"], RATE["R"] / RATE[c["other"]], places=3)
        r = next(a for a in out["arms"] if (a["payload"], a["arm"]) == ("small", "R"))
        self.assertAlmostEqual(r["rps"], 240000, places=0)
        self.assertEqual((r["server_share"], r["generator_share"]), (97.0, 950.0))

    def test_voids_and_reruns(self):
        write_campaign(self.tmp / "c", {("Q", "medium", 4): ("bad", "ok"), ("R", "small", 2): ("nostart", "ok"),
                                        ("P", "small", 9): ("rc", "ok")})
        out = M3.analyse_campaign(self.tmp / "c")
        self.assertEqual(out["void"], ["P small r9", "Q medium r4", "R small r2"])
        self.assertEqual(out["m"], 4)
        write_campaign(self.tmp / "d", {("S", "medium", 3): ("bad", "bad")})
        out = M3.analyse_campaign(self.tmp / "d")
        self.assertEqual((out["m"], out["touched"]), (2, ["medium"]))

    def test_main_and_macros(self):
        write_pilot(self.tmp / "p")
        write_campaign(self.tmp / "c")
        replication = {"e2e": {"arms": [{"os": "linux", "payload": p, "arm": a, "heavy_rps": 50000.0}
                                        for p in A.PAYLOADS for a in A.ARMS]}}
        (self.tmp / "rep.json").write_text(json.dumps(replication))
        A.ORDER_SHA256 = {}
        self.assertEqual(M3.main(["--pilot-linux", str(self.tmp / "p"), "--oha-linux", str(self.tmp / "c"),
                                  "--replication", str(self.tmp / "rep.json"), "--out", str(self.tmp / "out")]), 0)
        A.ORDER_SHA256 = {("oha", "linux"): M3.ORDER_SHA256}
        text = (self.tmp / "out" / "macros.tex").read_text()
        names = re.findall(r"^\\newcommand\{\\([A-Za-z]+)\}", text, re.M)
        first = set(re.findall(r"^\\newcommand\{\\([A-Za-z]+)\}", FIRST_MACROS, re.M))
        self.assertTrue(names and all(n.startswith("Oha") for n in names) and not set(names) & first)
        self.assertIn("\\newcommand{\\OhaGate}{yes}", text)
        self.assertIn("\\newcommand{\\OhaOverRepSmallR}{4.800}", text)  # 240,000 over 50,000
        write_pilot(self.tmp / "f", low_share=80.0)
        with self.assertRaises(A.AnalysisError):  # a failed gate takes no campaign folder
            M3.main(["--pilot-linux", str(self.tmp / "f"), "--oha-linux", str(self.tmp / "c"), "--out", str(self.tmp / "o2")])


if __name__ == "__main__":
    unittest.main()
