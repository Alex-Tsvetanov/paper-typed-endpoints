# Hypotheses, second freeze: the replication and mixed traffic

Status: FROZEN on 2026-11-03 by the commit that adds this file, together with its run orders
(`bench/campaign-r2/`) and its analysis (`analysis/analyse_r2.py`), before any replication build or
window ran. From that commit on, the text above the revision log is never edited; every change goes
into the revision log at the end. `hypotheses.md` (frozen 2026-11-01) is not changed by this file.
Nothing in this file is a result.

Why. Grok's referee report of round 1 (Papers `p5-typed-endpoints/reviews/grok-r1-review.md`) asks,
as M1(a), that directions and family membership be fixed on data held out from the confirmatory
runs and the campaign be run again, and, as M4, for a timed window of mixed traffic. Alex answered
question Q20261103-p5-1 (`lab/pipeline/questions.md`), which listed these runs, on 2026-11-03,
verbatim: "P5-1: Just run them, yes." This file covers M1(a) and M4. M2 (a gRPC C++ arm) and M3 (a
second load generator) need downloads first; each gets its own freeze.

Words: as `hypotheses.md`. In addition:
- The first campaign is the campaign of Laptop requests 22 to 25 (chain A55, 2026-11-01 and 02),
  analysed at ANALYSIS_COMMIT c5a326b; its results are `results/summary.json` and
  `results/macros.tex`.
- The replication is the campaign this file adds: requests 27 to 30, the same experiments again.
- The mixed-traffic experiment is request 31 (section 4).
- The smaller order is the medium Order without its last item and without its customer, the
  message `test_allocs` alternates with the medium Order (`hypotheses.md` section 4).

## 1. Claims and scope

As `hypotheses.md` section 1, per OS and about the same pinned toolchain, schemas and payloads,
with one addition: mixed traffic of the medium and the smaller order on Linux (section 4). Out of
scope, as there: gRPC C++, streaming, TLS, refused proto3 features, and any comparison of Linux
with Windows. No result of this file is pooled with a result of the first campaign.

## 2. The replication's families: fixed on the first campaign

The rule below uses only the first campaign's data. No replication build or window existed when it
was applied, so the replication's data are held out from it; the replication also runs in new
seeded orders (section 3). The pilots, which fixed the first campaign's directions, play no part
here.

Rule. Each contrast of `hypotheses.md` sections 2 and 3 carries a direction in the replication if
its 95% BCa interval in the first campaign lies wholly on one side of 1; the direction is that
side. Any other contrast is descriptive. Two exceptions keep Alex's earlier answers
(`hypotheses.md` section 6): the small payload at c1 m1 carries no direction (his Q2); and a Windows
contrast at c12 m128 carries none unless the first campaign's server share at c12 m128 was at least
95% of one CPU for every arm and payload (his acceptance of item (e)).

Applied to `results/summary.json` (the first campaign; `analysis/analyse_r2.py`, `families_from`):
- Compile: every interval of the 36 cells lies on one side of 1. Directions: wide, R faster at every
  size; many, R faster from 16 to 256 fields and R slower from 512 to 4096, on both OSes; many 4096
  stays the secondary curve. These are the directions of `hypotheses.md` section 7, items 1 and 2.
- End to end, Linux: the four contrasts of E1 (c12 m128, both payloads, R against P and against Q)
  and the two of E2 (medium at c1 m1) lie above 1 and carry "R serves more". Small at c1 m1:
  descriptive (Q2).
- End to end, Windows: medium at c1 m1, R against P, lies above 1 and carries "R serves more".
  Medium at c1 m1, R against Q: its interval contains 1, so descriptive. Every c12 m128 contrast:
  descriptive, since the first campaign's server share there was below 95% (from 23% to 61% of one
  CPU across arms and payloads). Small at c1 m1: descriptive (Q2).
- So the families are the first freeze's: compile m = 36 and end to end m = 7, with the same
  members and directions. The analysis applies the rule to `results/summary.json` again and stops
  if it gives anything else.

## 3. The replication (requests 27 to 30)

Everything of `hypotheses.md` sections 2 and 3 holds unchanged: cells, measures, ten replicates
per OS, placement, boost off, the quiet check, validity, voids and reruns, the exact one-sided
paired sign test with a tie counting against, Holm at alpha 0.05 per experiment across both OSes,
ratio effect sizes with 95% BCa intervals from 10,000 clustered resamples, the descriptive
secondary measures, and the Linux ghz cross-check, which is never a contrast. What differs:
- Orders. New seeded orders in `bench/campaign-r2/`, from `bench/campaign_order.py --r2`; the
  labels begin "P5 replication", so the seeds are new (compile Linux 2666215862, Windows
  4102188713; end to end Linux 1083394441, Windows 3492828547).
- Code. The jobs run at this file's freezing commit. Every measured build compiles the first-party
  inputs of CODE_FREEZE 9a3dc92: each build job's inputs hash list (`lab/bin/inputs_hash.py`) must
  equal the first campaign's lists, file by file and digest by digest, and the compile builds pass
  the same compile-curve gate. So the D5 records of 9a3dc92 (A52 on Linux, A57 on Windows) cover
  the replication. A difference stops the job before its first timed build or window.
- Analysis. `analysis/analyse_r2.py` runs `analysis/analyse.py`'s code, unchanged since
  ANALYSIS_COMMIT, on the replication's folders; it writes `results-r2/`. Every replication macro is
  the first campaign's macro name with the prefix `Rep`.
- Reporting. Both campaigns are reported, each with its own tests. The replication's tests are this
  freeze's confirmatory tests; a contrast that passes in one campaign and fails in the other is
  reported as such, and neither campaign's result is dropped.

## 4. Mixed traffic on Linux (request 31)

Cell: one, on Linux, at a total load of 12 connections of 128 streams, the c12 m128 load of
section 3 split between two generators. The server and its placement are those of `hypotheses.md`
section 3 (one worker on CPU 14, its sibling 15 idle, boost off, after the quiet check), with the
replication's Linux build (request 29).

Load. Each 10 s window runs two h2load 1.70.0 processes, started together, both calling
`/bench.e2e.Echo/EchoOrder` with the gRPC headers of section 3: one posts the medium order
(`medium.bin`) on CPUs 2 to 7, the other the smaller order (`smaller.bin`) on CPUs 8 to 13, each with
6 connections of 128 streams and 6 threads. Which body each call carries is fixed by its
connection; how many calls each body gets is what the server serves, and is reported.

Replicate. In each of ten replicates, the four arms R, P, Q and S in a seeded order
(`bench/campaign-r2/mixed-order-linux.tsv`, label "P5 mixed e2e linux", seed 3611928170); per arm
one server run: a 10 s warm-up window, discarded, then 4 windows of 10 s.

Measure. Primary: requests per second, the sum of the two processes' completed OK calls per window
second (h2load's "req/s"); a replicate's value is the mean of its 4 windows. Secondary,
descriptive: the smaller order's share of the calls; the server's CPU share and the two generators'
summed share per window (`mid`, as section 3); the server's peak resident set (`VmHWM`), read
before it stops.

Hypothesis. X1: under mixed traffic, R serves more requests per second than P, and than Q (2
contrasts). The direction is fixed by the first campaign, where R served more than P and than Q on
the medium payload at c12 m128 with every interval above 1. No mixed-traffic pilot was run. S runs
as the reference and is never compared.

Test. The exact one-sided paired sign test over the 10 replicates (a tie counts against). The two
contrasts form their own family, m = 2, Holm at alpha 0.05 family-wise; 2^-10 = 0.00098 < 0.025.
Effect sizes: the ratio of mean requests per second, with a 95% BCa interval from a clustered
bootstrap that resamples replicates with all their arms (10,000 resamples, seed recorded).

Validity, per server run: each of its 10 window files reports 0 failed, 0 errored and 0 timeout
requests and only 2xx status codes; each generator exits with status 0; the server log ends with
`failed 0`, and its count of OK calls lies between the sum S of the generators' counts over the
run's windows (warm-up included) and S plus 2 c m summed over its 10 files. A failed run voids that
arm's run in that replicate; it runs again once after the last row. A second failure makes both
contrasts descriptive, and is reported.

## 5. Numbers and gates

As `hypotheses.md` section 5. Numbers come only from `results-r2/macros.tex`, generated by
`analysis/analyse_r2.py` from the jobs' archives. D5: the measured builds' first-party inputs are
those of 9a3dc92 (section 3), covered by the green records A52 and A57. `write_bodies`, which
writes the request bodies and is in no measured target, gained the smaller order at this commit and
is in no sanitizer record; `bench/coverage.json` declares that gap. Its output is checked instead:
each build job checks every body's sha256 against published values (the small and medium bodies
byte for byte the first campaign's), and `write_bodies` reads each body back and parses it.
Every timed build is the optimized Release build.

Laptop time, from the first campaign's own durations: compile about 3.1 h on Linux and 3.4 h on
Windows; end to end about 2.1 h and 2.0 h; mixed traffic about 0.7 h (40 server runs of 50 s of
windows); builds a few minutes. About 11.5 h in all, one OS at a time.

## Revision log

None yet.
