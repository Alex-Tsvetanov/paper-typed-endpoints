# P5 freeze proposal (draft for Alex, through the coordinator)

Written 2026-10-31. Nothing here is frozen. After Alex's answers, `hypotheses.md` is written new
from this proposal and frozen before any timed window; changes after that go to the revision log.
Answered by Alex on 2026-10-31, the recommended option to each of Q1 to Q4 (revision log);
`hypotheses.md` (DRAFT) is written from it and holds where the two differ.
Every number below comes from a laptop pilot named in STATE.md (Laptop requests); pilots are not
results and nothing here is citable.

## 1. Where P5 stands

- Compile, Linux (A40 at db1f31c): R/P wide 0.31, 0.90, 0.92, 0.76 at 16, 1024, 2048 and 4096
  fields; many 0.32, 1.14, 1.29, 1.45. Every R build separated from every P build in each cell.
- End to end, Linux (pilot 5 at db1f31c). At the knee load, 12 connections of 128 streams (Laptop
  request 3): R/P small 1.084, medium 1.234; R/Q small 1.046, medium 1.028; every window
  separated. At one connection and one stream: medium R/P 1.057, R/Q 1.023, separated; small
  R/P 1.007, R/Q 1.006, windows overlap, and S (the transport with no codec) is within 0.3% of R.
- Windows: no end-to-end run yet (Laptop request 15 is queued). There is no Windows compile timing
  tool: `bench/compile_curve.py` reads peak memory with `wait4` and finds protobuf with
  `pkg-config`, both Linux only.

## 2. What freezes

- The P5 commit after Laptop request 15 and the items of section 6.
- `hypotheses.md` (new), `bench/coverage.json` (final), the campaign scripts (the coordinator's).
- D5 records at that commit, green, before the first timed window.

## 3. Compile campaign (Linux)

- Cells: N in {16, 32, 64, 128, 256, 512, 1024, 2048, 4096}, shapes wide and many, generators R
  and P: 36 cells. `many` 4096 is the one secondary-curve cell (`-fconstexpr-ops-limit=67108864`,
  revision log 2026-10-30).
- Per build: wall time and peak RSS, by phase, as `compile_curve.py` records them now.
- D9: one contrast per shape and size, m = 18; Holm at alpha 0.05 starts at alpha/m = 0.00278.
  R = 10 builds per generator and cell (2^-10 = 0.00098). Builds run in (R, P) pairs, the order
  within each pair random, seeded and recorded.
- Time: 360 builds. From A40's medians, with sizes 32 to 512 bounded by the size-1024 value, at
  most 15,715 s (4.4 h), plus the checks and three `-ftime-report` builds.
- Hypothesis candidates:
  - C1. For the wide shape, R builds faster than P at every size (pilot: true at all four sizes
    measured, every build separated).
  - C2. For the many shape, see question Q3: no direction claimed, or R slower than P from 1024.
  - The design note's draft "build time grows at most linearly in N" does not hold in the pilot:
    from 2048 to 4096 fields R grows 2.33 times (wide) and 2.19 times (many), P 2.81 and 1.95
    times. Proposal: report the growth (a fitted exponent with its interval), no linearity claim.

## 4. End-to-end campaign (Linux; Windows if Laptop request 15 shows the server limits)

- Arms: R, P (one Arena per request) and Q (reused messages), compared; S, the transport with no
  codec, as the reference lower bound, not compared.
- Cells per OS: payload small or medium, load one connection with one stream (c1 m1) or 12
  connections of 128 streams (c12 m128): 4 cells.
- Replicate: one server run per arm and payload, a 10 s warm-up window (discarded), then 4 windows
  of 10 s at c1 m1 and 4 at c12 m128. A replicate's value in a cell is the mean of its 4 windows.
  For each payload and replicate the four arms run in a random order, seeded and recorded.
- D9: contrasts R against P and R against Q in the cells with a hypothesis, at most 8 per OS and
  16 over both; Holm starts at alpha/16 = 0.003125. R = 10 replicates; an exact one-sided paired
  sign test with all 10 pairs in favour gives p = 2^-10 = 0.00098.
- Windows per OS: 4 arms x 2 payloads x 10 replicates x (1 + 8) windows = 720 windows of 10 s,
  7,200 s, plus server starts and checks: about 2.2 h per OS.
- Metrics: requests per second (the headline); latency median and p99 at c1 m1 (Linux: h2load's
  per-request log; Windows: ghz's percentiles); the server's CPU share per window (process
  shares); the server's peak RSS (read by the harness before it stops the server: `VmHWM` on
  Linux, `PeakWorkingSetSize` on Windows).
- Hypothesis candidates:
  - E1. At c12 m128, R serves more requests per second than P and than Q, for both payloads.
  - E2. At c1 m1 with the medium payload, R serves more requests per second than P and than Q.
  - E3. At c1 m1 with the small payload, see question Q2.
  - Windows: the same hypotheses only if Laptop request 15 shows the server near 100% of its CPU
    at c12 m128 under ghz. Otherwise the Windows rows are reported without hypotheses, and the
    claims stay per OS.
- Cross-check (decision 4 of 2026-10-23, "one cross-check"): on Linux, the medium payload at
  c12 m128 also driven by ghz for arms R and P, 4 windows each, reported as a check of the load
  generator, not as a contrast (about 2 minutes).

## 5. Arm G (gRPC C++)

Alex kept G on 2026-10-23, to be built after R and P. It is not built: gRPC C++ and its
dependencies, pinned, with GCC on Linux and MSVC on Windows (its documented configuration). See
question Q1.

## 6. Before the freeze (this session)

- Laptop request 15, the Windows pilot: queued with the coordinator.
- Golden vectors with the pinned protoc 36.2: done 2026-10-31, byte for byte identical.
- D5 record request: GCC ASan+UBSan and TSan for the reflection layer and the server; clang
  ASan+UBSan, TSan and MSan for the portable layers; MSVC ASan of the transport on Windows (M2).
- A fresh review of the code changed since the last review.

## 7. Laptop time

- Linux: compile campaign at most 4.4 h; end to end about 2.2 h; the records have no measured
  basis on the laptop yet.
- Windows: end to end about 2.2 h if the pilot passes; compile curves only if Q4 is (a), after a
  Windows compile pilot that gives them a basis.
- Plan v2 estimated 8.0 h in all.

## 8. Questions for Alex

- Q1, arm G. (a) Freeze R, P and Q now; G becomes a later campaign with its own hypotheses (R
  against G), in the paper if it is done in time. (b) Build G first, freeze after. (c) Drop G and
  declare it. Recommended: (a), since G answers another question (what a C++ user gets today,
  with its own transport) and R, P and Q do not wait for it.
- Q2, small payload at one connection and one stream. (a) No directional hypothesis; the cell is
  reported with intervals, since the transport alone is within 0.3% of every arm there. (b)
  Non-inferiority: R is at most 2% slower than P and than Q (exact sign test on R - 0.98 X).
  Recommended: (a).
- Q3, compile, many shape. (a) No direction claimed; each ratio reported with its interval. (b)
  Claim the pilot's direction: R slower than P from 1024 fields. Recommended: (b), the honest
  direction stated beforehand.
- Q4, Windows compile curves. (a) Port `compile_curve.py` to Windows (peak memory from a job
  object, protobuf found without pkg-config), pilot it, then measure; Alex's decision D-W
  (2026-10-23) planned both generators on Windows. (b) Declare Windows compile curves a gap;
  Windows covers the end-to-end runs only. Recommended: (a).
