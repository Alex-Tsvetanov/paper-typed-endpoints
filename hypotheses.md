# Hypotheses

Status: DRAFT of 2026-10-31, not frozen. Written new from `design/freeze-proposal.md` and Alex's
answers of 2026-10-31 to its questions Q1 to Q4 (relayed by the coordinator; quoted in section 6).
It is frozen by a commit that changes this status paragraph and fills the values of section 7 by
the rules written here, and nothing else, once three pilots are back: the Windows end-to-end pilot
(Laptop request 15), the Windows compile pilot (request 17) and the Linux compile pilot of the
sizes 32 to 512 (request 18). It is frozen before the first timed window of either campaign. From
the freezing commit on, the text above the revision log is never edited; every change goes into
the revision log at the end. Nothing in this file is a result.

Words:
- R is the reflection generator and its arm (`tep`, C++26 static reflection). P is the `protoc`
  generator; its end-to-end arm uses one Arena per request on a reused block. Q is the `protoc`
  arm with reused non-arena messages (Alex, 2026-10-30). S is the transport with no codec, a
  reference lower bound, never compared.
- Linux is the laptop's Arch; Windows is the laptop's Windows. They run on the same laptop (AMD
  Ryzen 7 5800H). No claim compares them.
- A pilot is a laptop run named in STATE.md (Laptop requests 13, 14, 15, 17 and 18). Pilots fix
  directions by the rules below and decide nothing else; no pilot number is a result.
- Separated: in a cell's pilot builds or windows, every R value is on one side of every value of
  the arm it is compared with.
- Descriptive: a cell reported with its ratio and interval, with no hypothesis and no test.

## 1. Claims and scope

Each claim is per OS, about the pinned toolchain (GCC 16.2.0 on Linux; MinGW-w64 GCC 16.2.0,
winlibs r3, on Windows; protobuf 36.2 with abseil 20250512.1; libnghttp2 1.70.0), the synthetic
schemas of `bench/gen_schema.py` and the two echo payloads of `server/requests.cpp`, and is worded
no wider. Out of scope: gRPC C++ (arm G; Alex's Q1, section 6: a later campaign with its own
hypotheses and freeze), streaming rpcs, TLS, proto3 features the generator refuses (maps, oneof,
`optional`, imports), and any comparison of Linux with Windows.

## 2. Compile experiment

Cells per OS: shapes wide and many (`gen_schema.py`) at N in {16, 32, 64, 128, 256, 512, 1024,
2048, 4096} fields: 18 cells, each built by R and by P. `R many 4096` is built with
`-fconstexpr-ops-limit=67108864` (revision log 2026-10-30), every other build with the compiler's
defaults; that cell is labelled as the secondary curve wherever it appears.

Measure. Primary: the whole build's wall time, the sum of the phases `compile_curve.py` records,
from `.proto` to the linked program (R: compile, link; P: protoc, compile_pb, compile_user, link).
Writing the schema and the user program before the first phase, and the program's check after the
last, are not timed. Secondary, descriptive: each phase's wall time; peak memory of the largest
process of any phase, which is the peak resident set on Linux (`maxrss_kib`) and the peak committed
memory on Windows (`peak_commit_kib`). The two memory measures are never called by one name.

Hypotheses. One contrast per cell and OS: R against P on the primary measure.
- C1 (wide). R builds faster than P.
- C2 (many, from 1024 fields; Alex's Q3, section 6). R builds slower than P.
- Rule for every cell, C1 and C2 included: a cell carries the direction its OS's pilot shows only
  if the pilot built it and its builds are separated (`bench/pilot/directions.py`, whole builds,
  pilots pooled). Any other cell is descriptive. A cell whose separated direction contradicts C1 or
  C2 carries the pilot's direction, and the contradiction is reported.
- Known on Linux from Laptop request 13 (A40, 3 builds per generator): wide 16, 1024, 2048, 4096 R
  faster, separated (whole-build ratios 0.302, 0.893, 0.915, 0.759); many 16 R faster (0.319);
  many 1024, 2048, 4096 R slower (1.128, 1.289, 1.449). Open: section 7, items 1 and 2.

Design (D9). Ten build pairs per cell and OS; a pair is one R and one P build of the cell. Within
each replicate the 18 cells run in a seeded random order and the two builds of a pair in a seeded
random order; the seeds and the order are written before the first build. Serial builds, each
build's process tree on CPU 14, its sibling 15 idle (`lab/laptop/README.md`; `taskset -c 14` on
Linux, a job object with `--cpu 14` on Windows), boost off, after the lab's quiet check.

Test. For a directional cell, an exact one-sided paired sign test over its 10 pairs, counting the
pairs whose R build lies in the claimed direction (a tie counts against). All directional compile
contrasts of both OSes form one family, m at most 36; Holm at alpha 0.05 family-wise. With all 10
pairs in favour p = 2^-10 = 0.00098 < 0.05/36 = 0.00139, so every step can pass.

Effect sizes, every cell: the ratio of medians R/P with a 95% BCa interval from a clustered
bootstrap that resamples build pairs (10,000 resamples, seed recorded in the analysis script).
Growth, descriptive: per generator and shape, the ratio of medians between N and 2N for each
consecutive pair of sizes, with its interval. The design note's draft "grows at most linearly"
is not a hypothesis (the pilots contradict it; freeze proposal section 3).

Validity. A build counts if `built` and `checked` are true and, on Windows, each phase reads back
affinity `0x4000` and none has `timed_out`. A failed build voids its pair; the pair is built again
once after the last replicate. A second failure makes the cell descriptive, and is reported.

## 3. End-to-end experiment

Arms R, P and Q are compared; S runs as the reference. `tep_server` with one worker on CPU 14,
its sibling 15 idle; boost off, as every P5 pilot ran (`"boost": false`); after the lab's quiet
check. Load generator on Linux: h2load 1.70.0 (pinned build) on CPUs 2 to 13; on
Windows: ghz v0.121.0 (pinned) with affinity 0x3FFC and `--cpus 12`, mapped from h2load's
connections and streams as in Laptop request 15 (`bench/requests/e2e-pilot-windows.md`).

Cells per OS: payload small (Person, 34 bytes) or medium (Order, 953 bytes), at load c1 m1 (one
connection, one stream) or c12 m128 (12 connections of 128 streams, the knee of Laptop request 3).

Replicate: for one payload, one server run per arm: a 10 s warm-up window at c12 m128 (discarded),
then 4 windows of 10 s at c1 m1 with per-request latency recorded, then 4 windows of 10 s at
c12 m128. For each payload and replicate the four arms run in a seeded random order. A replicate's
value in a cell is the mean of its 4 windows' requests per second. Ten replicates per OS (D9).

Measure. Primary: requests per second, completed OK calls per window second. Secondary,
descriptive: at c1 m1, on Linux the latency median and p99 from h2load's per-request log; on
Windows the mean call cycle, 1/(requests per second), which includes ghz's own time per call
(ghz's percentiles on Windows are quantized to about 0.5 ms, revision log 2026-10-31); the
server's CPU share per window (`labrec.process_shares`); the server's peak
memory, read by the harness before it stops the server, which is the peak resident set on Linux
(`VmHWM`) and the peak working set on Windows (`PeakWorkingSetSize`).

Hypotheses. Contrasts R against P and R against Q.
- E1. At c12 m128, R serves more requests per second than P and than Q, for both payloads (4
  contrasts per OS).
- E2. At c1 m1 with the medium payload, R serves more requests per second than P and than Q (2
  contrasts per OS).
- Small payload at c1 m1: descriptive (Alex's Q2, section 6; the transport alone, S, is within
  0.3% of every arm there).
- Rule: a contrast carries its hypothesis on an OS only if that OS's pilot windows for it are
  separated in the claimed direction. On Windows, E1 also needs the Windows pilot to show the
  server at a CPU share of at least 0.95 at c12 m128 for every arm and payload; otherwise the
  Windows E1 contrasts are descriptive. The 0.95 is a design choice, not a measured value: E1
  compares the servers only if ghz saturates the server's one CPU. Any other contrast is descriptive.
- Known on Linux from Laptop request 14 (pilot 5): every E1 and E2 contrast separated in the
  claimed direction (R/P small 1.084, medium 1.234; R/Q small 1.046, medium 1.028 at c12 m128;
  medium R/P 1.057, R/Q 1.023 at c1 m1). Open: section 7, item 3.
- On Windows, in the pilot (Laptop request 15), ghz bounded the c12 m128 rows: it used about 97%
  of its 12 CPUs while the server used 24% to 62% of one, and every arm, S included, served within
  1% of the others. Those rows show the load generator's limit; their ratios do not compare the
  servers, and the paper says so wherever they appear.

Test. Exact one-sided paired sign test over the 10 replicates (a tie counts against). All
directional end-to-end contrasts of both OSes form one family, m at most 12; Holm at alpha 0.05
family-wise; 2^-10 < 0.05/12. Effect sizes, every cell: the ratio of mean requests per second
with a 95% BCa interval from a clustered bootstrap that resamples replicates, each with all its
arms (10,000 resamples, seed recorded).

Validity, per window: h2load reports 0 failed and 0 errored requests, or ghz reports only OK
statuses; the server log ends with `failed 0`; the server's count of OK calls lies between the
generator's count and that count plus 2 c m per window (the pilots' bound). A failed window voids that arm's
server run in that replicate; it runs again once after the last replicate. A second failure makes
the cells it touches descriptive, and is reported.

Cross-check (Alex's decision 4 of 2026-10-23): on Linux, the medium payload at c12 m128 also driven
by ghz for R and P, 4 windows each, reported as a check of the load generator, never as a contrast.

## 4. Reported without hypotheses

Heap allocations per call (`test_allocs`, counted from the frozen build, untimed): per arm and
payload for identical requests, and when calls alternate between the medium order and a smaller
one (the measured runs send one fixed body per window, so the alternating count is a threat).
Interoperability with grpc-go (functional, every field checked). The constant-evaluation
operation counts of the schema tables (deterministic for one GCC build).

## 5. Numbers and gates

Numbers in the paper come only from `results/macros.tex`, generated from the campaign's archives by
the analysis scripts at the analysis commit. A number is citable only if the D5 sanitizer records
of the frozen commit are green for each platform measured (`bench/coverage.json` lists the gaps).
Every timed build is the optimized Release build; sanitizer and Debug builds are never timed.

## 6. Alex's answers of 2026-10-31 (verbatim, relayed by the coordinator)

- Q1 "P5 freeze, arm G (gRPC C++, kept by you 2026-10-23, never built). What now?" = "Freeze R,P,Q
  now (Recommended)".
- Q2 "P5 small payload at 1 connection, 1 stream: the arms tie (transport alone within 0.3%).
  Hypothesis?" = "No direction (Recommended)".
- Q3 "P5 compile, many shape: pilots show R slower than P from 1024 fields. Hypothesis?" = "Claim
  direction (Recommended)".
- Q4 "P5 Windows compile curves (the tool is Linux only). What to do?" = "Port and pilot
  (Recommended)".
- The choices this file makes within those answers, put to Alex the same day: "(a) no-direction
  cells get intervals, not two-sided tests; (b) one Holm family per experiment across both OSes;
  (c) a cell takes a direction only from a pilot on that OS with every R build on one side of every
  P build, which adds many 16 to 512 (many 16 on Linux = R faster); (d) compile time includes the
  link; (e) the Windows end-to-end hypothesis needs the server at CPU share of at least 0.95 at
  c12 m128. Accept?" = "Accept all (Recommended)".

## 7. Values filled at the freeze (by the rules above, from the pilots named)

1. Linux compile cells at 32, 64, 128, 256 and 512 fields, both shapes: direction or descriptive,
   from Laptop request 18.
2. Windows compile cells, all 18: direction or descriptive, from Laptop request 17 (its sizes 16,
   1024, 2048, 4096 and 32 to 512).
3. Windows end-to-end contrasts E1 and E2: hypothesis or descriptive, from Laptop request 15.
4. The frozen commit of the code (`CODE_FREEZE`), named in the freezing commit's message; the D5
   records are made at it.

## Revision log

None yet.
