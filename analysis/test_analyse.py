# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""Tests of the campaign analysis on synthetic campaign folders written in the requests' formats
(bench/requests/compile-campaign.md, e2e-campaign.md), with the real order files. No timing: every
number here is made up. Run: python -I -m unittest discover -s analysis -p "test_*.py"
"""
from __future__ import annotations

import json
import random
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
import macros as M  # noqa: E402
import stats as S  # noqa: E402

ORDERS = HERE.parent / "bench" / "campaign"


def setUpModule():
    global SAVED
    SAVED = S.B_RESAMPLES
    S.B_RESAMPLES = 200  # enough to exercise the code; the campaign uses 10,000


def tearDownModule():
    S.B_RESAMPLES = SAVED


class StatsTest(unittest.TestCase):
    def test_sign_p(self):
        self.assertEqual(S.sign_p(10, 10), Fraction(1, 1024))
        self.assertEqual(S.sign_p(9, 10), Fraction(11, 1024))
        self.assertEqual(S.sign_p(0, 10), 1)

    def test_holm(self):
        self.assertEqual(S.holm_adjust([0.01, 0.04, 0.03]), [0.03, 0.06, 0.06])
        self.assertEqual(S.holm_pass([1 / 1024] * 36), [True] * 36)
        # A 9-of-10 cell ranked last needs only alpha / 1; five of them at m = 36 need 5 x 11/1024 <= 0.05.
        self.assertEqual(S.holm_pass([11 / 1024] + [1 / 1024] * 35), [True] * 36)
        self.assertEqual(S.holm_pass([11 / 1024] * 5 + [1 / 1024] * 31), [False] * 5 + [True] * 31)

    def test_inverted_cdf(self):
        v = [1, 2, 3, 4]
        self.assertEqual([S.inverted_cdf(v, q) for q in (0, 0.25, 0.26, 0.5, 0.99, 1)], [1, 1, 2, 2, 4, 4])

    def test_bca_is_seeded_and_degenerate_cases_hold(self):
        gen = random.Random(1)
        pairs = [(gen.gauss(10, 1), gen.gauss(12, 1)) for _ in range(10)]
        a = S.bca(pairs, S.ratio_of_medians, "x", resamples=300)
        b = S.bca(pairs, S.ratio_of_medians, "x", resamples=300)
        self.assertEqual(a, b)
        self.assertLessEqual(a.low, a.theta)
        self.assertGreaterEqual(a.high, a.theta)
        same = S.bca([(2.0, 1.0)] * 10, S.ratio_of_medians, "y", resamples=100)
        self.assertEqual((same.low, same.theta, same.high), (2.0, 2.0, 2.0))


# ---------------------------------------------------------------- synthetic compile campaigns

def order_rows(experiment: str, os_name: str) -> list[dict]:
    lines = [ln for ln in (ORDERS / f"{experiment}-order-{os_name}.tsv").read_text().splitlines() if not ln.startswith("#")]
    head = lines[0].split("\t")
    return [dict(zip(head, ln.split("\t"))) for ln in lines[1:]]


def build_line(row: dict, os_name: str, rng: random.Random, built: bool = True, affinity: str = "0x4000") -> dict:
    shape, n, gen = row["shape"], int(row["n"]), row["gen"]
    base = 1.0 + n / 256
    factor = 0.5 if A.DIRECTION[(shape, n)] == "faster" else 1.5
    total = base * (factor if gen == "R" else 1.0) * (1 + 0.01 * rng.random())
    names = ["compile", "link"] if gen == "R" else ["protoc", "compile_pb", "compile_user", "link"]
    mem = "maxrss_kib" if os_name == "linux" else "peak_commit_kib"
    phases = []
    for name in names:
        phase = {"phase": name, "exit": 0, "wall_ns": int(total / len(names) * 1e9), mem: 1000 + n}
        if os_name == "windows":
            phase["affinity"] = affinity
        phases.append(phase)
    line = {"gen": gen, "shape": shape, "n": n, "rep": int(row["rep"]), "ops_limit": int(row["ops_limit"]),
            "time_report": False, "timed": True, "phases": phases, "built": built, "checked": built}
    if built:
        line["deps_exit"] = 0
        line["first_party_inputs"] = {f: "0" * 64 for f in A.FIVE} if gen == "R" else {}
    return line


def write_compile(folder: Path, os_name: str, broken: dict | None = None, retry: list | None = None,
                  validate: list[str] | None = None) -> None:
    """A campaign folder; broken maps an order index (from 1) to build_line keyword arguments."""
    folder.mkdir(parents=True)
    shutil.copy(ORDERS / f"compile-order-{os_name}.tsv", folder)
    rng = random.Random(os_name)
    rows = order_rows("compile", os_name)
    lines = [build_line(r, os_name, rng, **(broken or {}).get(i, {})) for i, r in enumerate(rows, 1)]
    (folder / "campaign.jsonl").write_text("".join(json.dumps(x) + "\n" for x in lines))
    if retry:
        (folder / "retry.jsonl").write_text("".join(json.dumps(x) + "\n" for x in retry))
    (folder / "validate.txt").write_text("checks: synthetic\n" + "".join(v + "\n" for v in (validate or [])))


def index_of(os_name: str, shape: str, n: int, rep: int, gen: str) -> int:
    for i, r in enumerate(order_rows("compile", os_name), 1):
        if (r["shape"], r["n"], r["rep"], r["gen"]) == (shape, str(n), str(rep), gen):
            return i
    raise KeyError((shape, n, rep, gen))


def pair_rows(os_name: str, shape: str, n: int, rep: int) -> list[dict]:
    return [r for r in order_rows("compile", os_name) if (r["shape"], r["n"], r["rep"]) == (shape, str(n), str(rep))]


class CompileTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def folders(self, **windows_kwargs) -> dict:
        write_compile(self.tmp / "l", "linux")
        write_compile(self.tmp / "w", "windows", **windows_kwargs)
        return {"linux": self.tmp / "l", "windows": self.tmp / "w"}

    def test_clean_campaign_passes_every_direction(self):
        out = A.analyse_compile(self.folders())
        self.assertEqual(out["m"], 36)
        for c in out["cells"]:
            self.assertEqual((c["pairs"], c["in_direction"], c["holm_pass"]), (10, 10, True), c)
            want = 0.5 if c["direction"] == "faster" else 1.5
            self.assertAlmostEqual(c["ratio"], want, delta=0.02)
        self.assertEqual(len(out["growth"]), 2 * 2 * 2 * 8)

    def test_void_pair_built_again(self):
        i = index_of("windows", "wide", 16, 3, "R")
        retry = [build_line(r, "windows", random.Random(0)) for r in pair_rows("windows", "wide", 16, 3)]
        out = A.analyse_compile(self.folders(broken={i: {"affinity": "0x1"}}, retry=retry,
                                             validate=["void wide 16 r3", "retried wide 16 r3"]))
        self.assertEqual(out["m"], 36)
        self.assertEqual(out["void"]["windows"], ["wide 16 r3"])

    def test_pair_void_twice_makes_its_cell_descriptive(self):
        i = index_of("windows", "many", 512, 5, "P")
        retry = [build_line(r, "windows", random.Random(0), built=(r["gen"] == "R"))
                 for r in pair_rows("windows", "many", 512, 5)]
        out = A.analyse_compile(self.folders(broken={i: {"built": False}}, retry=retry,
                                             validate=["void many 512 r5", "retried many 512 r5", "descriptive many 512"]))
        self.assertEqual(out["m"], 35)
        cell = next(c for c in out["cells"] if (c["os"], c["shape"], c["n"]) == ("windows", "many", 512))
        self.assertIsNone(cell["direction"])
        self.assertEqual(cell["pairs"], 9)

    def test_validate_txt_that_disagrees_stops_the_analysis(self):
        with self.assertRaises(A.AnalysisError):
            A.analyse_compile(self.folders(validate=["void wide 16 r1"]))

    def test_a_line_out_of_order_stops_the_analysis(self):
        folders = self.folders()
        path = folders["linux"] / "campaign.jsonl"
        lines = path.read_text().splitlines()
        lines[0], lines[2] = lines[2], lines[0]
        path.write_text("\n".join(lines) + "\n")
        with self.assertRaises(A.AnalysisError):
            A.analyse_compile(folders)


# ---------------------------------------------------------------- synthetic end-to-end campaigns

RATE = {"R": 1.10, "P": 1.00, "Q": 1.05, "S": 1.20}


def window_text(os_name: str, count: int, rate: float, ok: bool = True) -> str:
    if os_name == "linux":
        failed = 0 if ok else 3
        return (f"finished in 10.00s, {rate:.2f} req/s, 1.00MB/s\n"
                f"requests: {count} total, {count} started, {count} done, {count} succeeded, {failed} failed, 0 errored, 0 timeout\n"
                f"status codes: {count} 2xx, 0 3xx, 0 4xx, 0 5xx\n")
    status = "[OK]   " if ok else "[Unavailable]   "
    return (f"Summary:\n  Count:\t{count}\n  Total:\t10.00 s\n  Requests/sec:\t{rate:.2f}\n\n"
            f"Status code distribution:\n  {status}{count} responses   \n")


def write_run(folder: Path, os_name: str, arm: str, payload: str, rep: int, tag: str, mode: str, shares, peaks) -> None:
    """One server run. mode: ok; bad (non-OK windows); rc (one window's generator exit status 7); nostart (the server
    never listened: its log, and no window, shares, peak or latency file)."""
    log = folder / f"server-{arm}-{payload}-{tag}.log"
    if mode == "nostart":
        log.write_text("tep_server: bind failed\n")
        return
    total = 0
    for name, c, m, k in A.run_windows(os_name, arm, payload, tag):
        rate = RATE[arm] * (1000 if (c, m) == (1, 1) else 50000) * (2 if payload == "small" else 1)
        count = int(rate * 10)
        (folder / name).write_text(window_text(os_name, count, rate, mode != "bad"))
        total += count
        tool = A.tool_of(os_name)
        shares.append({"file": name, "arm": arm, "payload": payload, "c": c, "m": m, "k": k, "rep": rep,
                       "rerun": tag.endswith("x"), "rc": 7 if (mode == "rc" and k == "2") else 0,
                       "around": {"tep_server": 90.0, tool: None}, "mid": {"tep_server": 95.0, tool: 1000.0}})
    log.write_text(f"tep_server: arm {arm}, port 50051, 1 worker(s)\ntep_server: calls ok {total + 5}, failed 0\n")
    peaks.append({"arm": arm, "payload": payload, "rep": rep, "rerun": tag.endswith("x"),
                  "field": "VmHWM" if os_name == "linux" else "PeakWorkingSetSize", "kib": 20480})
    if os_name == "linux":
        for k in range(1, 5):
            (folder / f"latency-{arm}-{payload}-{tag}-{k}.tsv").write_text("".join(f"1\t200\t{30 + j}\n" for j in range(100)))


def write_e2e(folder: Path, os_name: str, void: dict | None = None, cross: bool = True) -> None:
    """void maps (arm, payload, rep) to the modes of its first run and its rerun, e.g. ("bad", "ok")."""
    folder.mkdir(parents=True)
    shutil.copy(ORDERS / f"e2e-order-{os_name}.tsv", folder)
    shares, peaks, lines = [], [], []
    for row in order_rows("e2e", os_name):
        arm, payload, rep = row["arm"], row["payload"], int(row["rep"])
        first, rerun = (void or {}).get((arm, payload, rep), ("ok", None))
        write_run(folder, os_name, arm, payload, rep, f"r{rep}", first, shares, peaks)
        if first != "ok":
            lines.append(f"void {arm} {payload} r{rep}")
            write_run(folder, os_name, arm, payload, rep, f"r{rep}x", rerun, shares, peaks)
            if rerun != "ok":
                lines.append(f"void {arm} {payload} r{rep}x")
    if os_name == "linux" and cross:
        for arm in ("R", "P"):
            for k in range(1, 5):
                (folder / f"cross-ghz-{arm}-medium-c12-m128-{k}.txt").write_text(
                    window_text("windows", 400000, RATE[arm] * 40000))
    (folder / "shares.jsonl").write_text("".join(json.dumps(s) + "\n" for s in shares))
    (folder / "peak.jsonl").write_text("".join(json.dumps(p) + "\n" for p in peaks))
    (folder / "validate.txt").write_text("checks: synthetic\n" + "".join(v + "\n" for v in lines))


class EndToEndTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def folders(self, linux_void=None, windows_void=None, cross=True) -> dict:
        write_e2e(self.tmp / "l", "linux", linux_void, cross)
        write_e2e(self.tmp / "w", "windows", windows_void)
        return {"linux": self.tmp / "l", "windows": self.tmp / "w"}

    def test_clean_campaign(self):
        out = A.analyse_e2e(self.folders())
        self.assertEqual(out["m"], 7)
        for c in out["contrasts"]:
            self.assertEqual(c["replicates"], 10)
            self.assertAlmostEqual(c["ratio"], RATE["R"] / RATE[c["other"]], places=3)
            if c["hypothesis"]:
                self.assertEqual((c["in_direction"], c["holm_pass"]), (10, True))
        self.assertAlmostEqual(out["cross_check"]["ratio"], 1.1, places=3)
        windows_arm = next(a for a in out["arms"] if (a["os"], a["payload"], a["arm"]) == ("windows", "medium", "R"))
        self.assertAlmostEqual(windows_arm["light_cycle_us"], 1e6 / 1100, places=3)
        linux_arm = next(a for a in out["arms"] if (a["os"], a["payload"], a["arm"]) == ("linux", "small", "P"))
        self.assertEqual((linux_arm["light_latency_median_us"], linux_arm["peak_mib"]), (79.5, 20.0))

    def test_void_runs_with_valid_reruns_keep_their_replicates(self):
        out = A.analyse_e2e(self.folders(linux_void={("Q", "medium", 4): ("bad", "ok"), ("R", "small", 2): ("nostart", "ok"),
                                                     ("P", "small", 9): ("rc", "ok")}))
        self.assertEqual(out["m"], 7)
        self.assertEqual(out["void"]["linux"], ["P small r9", "Q medium r4", "R small r2"])
        self.assertTrue(all(c["replicates"] == 10 for c in out["contrasts"]))

    def test_run_void_twice_makes_every_contrast_in_its_cells_descriptive(self):
        out = A.analyse_e2e(self.folders(windows_void={("S", "medium", 2): ("bad", "nostart")}))
        self.assertEqual(out["m"], 6)  # Windows E2 R against P (medium, c1 m1) is lost, though S is in no contrast
        touched = [c for c in out["contrasts"] if (c["os"], c["payload"]) == ("windows", "medium")]
        self.assertEqual({(c["hypothesis"], c["touched"], c["replicates"]) for c in touched}, {(False, True, 10)})

    def test_missing_cross_check_is_reported_not_fatal(self):
        out = A.analyse_e2e(self.folders(cross=False))
        self.assertEqual(len(out["cross_check"]["failed_windows"]), 8)
        self.assertIsNone(out["cross_check"]["ratio"])

    def test_a_repeated_shares_line_stops_the_analysis(self):
        folders = self.folders()
        path = folders["linux"] / "shares.jsonl"
        lines = path.read_text().splitlines()
        path.write_text("\n".join(lines + lines[:1]) + "\n")
        with self.assertRaises(A.AnalysisError):
            A.analyse_e2e(folders)

    def test_an_unexpected_file_stops_the_analysis(self):
        folders = self.folders()
        (folders["windows"] / "ghz-R-small-c12-m128-r3x-1.txt").write_text("stray")
        with self.assertRaises(A.AnalysisError):
            A.analyse_e2e(folders)

    def test_section_4_files_parse(self):
        allocs = self.tmp / "test_allocs.txt"
        allocs.write_text("".join(f"arm {a} {p}: 1.00 allocations, 8.0 bytes per call\n" for a in "SRPQ" for p in ("small", "medium"))
                          + "".join(f"arm {a} medium alternating with a smaller order: 2.00 allocations per call\n" for a in "RPQ")
                          + "test_allocs: all checks passed\n")
        self.assertEqual(len(A.read_allocs(allocs)), 11)  # server/test_allocs.cpp alternates R, P and Q only
        ops = self.tmp / "constexpr-ops.txt"
        ops.write_text("".join(f"{s} {n} tables: ops <= {n * 100} (within 0.5%)\n" for s in ("wide", "many") for n in A.SIZES))
        self.assertEqual(A.read_ops(ops)["many 4096"], 409600)
        ops.write_text("wide 16 tables: above 33554432\n")
        with self.assertRaises(A.AnalysisError):
            A.read_ops(ops)

    def test_macros_render_once_per_name(self):
        compile_dirs = {"linux": self.tmp / "cl", "windows": self.tmp / "cw"}
        write_compile(compile_dirs["linux"], "linux")
        write_compile(compile_dirs["windows"], "windows")
        summary = {"python": "test", "alpha": S.ALPHA, "resamples": S.B_RESAMPLES,
                   "compile": A.analyse_compile(compile_dirs), "e2e": A.analyse_e2e(self.folders()),
                   "allocs": None, "constexpr_ops": None}
        tex = M.render(summary)
        self.assertRegex(tex, r"\\newcommand\{\\CompLinuxWideSixteenRatio\}\{0\.(49|50)\d\}")
        self.assertIn("\\newcommand{\\EWindowsMediumLightRvsPPass}{yes}", tex)
        self.assertIn("\\newcommand{\\CompM}{36}", tex)
        self.assertNotRegex(tex, r"\\newcommand\{\\[A-Za-z]*\d")

    def test_compile_table_names_only_rendered_macros(self):
        compile_dirs = {"linux": self.tmp / "cl", "windows": self.tmp / "cw"}
        write_compile(compile_dirs["linux"], "linux")
        i = index_of("windows", "many", 512, 5, "P")
        retry = [build_line(r, "windows", random.Random(0), built=(r["gen"] == "R"))
                 for r in pair_rows("windows", "many", 512, 5)]
        write_compile(compile_dirs["windows"], "windows", broken={i: {"built": False}}, retry=retry,
                      validate=["void many 512 r5", "retried many 512 r5", "descriptive many 512"])
        summary = {"python": "test", "alpha": S.ALPHA, "resamples": S.B_RESAMPLES,
                   "compile": A.analyse_compile(compile_dirs), "e2e": A.analyse_e2e(self.folders()),
                   "allocs": None, "constexpr_ops": None}
        defined = set(re.findall(r"\\newcommand\{\\(\w+)\}", M.render(summary)))
        rows = M.compile_table(summary).splitlines()[1:]
        self.assertEqual(len(rows), 36)
        for row in rows:
            self.assertTrue(set(re.findall(r"\\(Comp\w+?)\{\}", row)) <= defined, row)
            self.assertEqual(row.count("&"), 11, row)
            self.assertNotIn("--", row)
        self.assertTrue(rows[0].startswith("Linux & wide & 16 & "))
        self.assertTrue(rows[-1].startswith("Windows & many-message & 4096$^{\\dagger}$ & "))
        self.assertEqual(sum("dagger" in r for r in rows), 2)
        self.assertIn("no test & no test & no test", rows[32])  # Windows many 512: its pair void twice


if __name__ == "__main__":
    unittest.main()
