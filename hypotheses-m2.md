# Hypotheses, fourth freeze: a gRPC C++ reference arm on Linux (arm G)

Status: FROZEN on 2026-11-03 by the commit that adds this file, together with arm G's code
(`arm_g/`), its run order (`bench/campaign-m2/`) and its analysis (`analysis/analyse_m2.py`), before
any arm G window ran. From that commit on, the text above the revision log is never edited; every
change goes into the revision log at the end. `hypotheses.md`, `hypotheses-r2.md` and
`hypotheses-m3.md` are not changed by this file. Nothing in this file is a result.

Why. Grok's referee report of round 1 asks, as M2, for a gRPC C++ arm on the same load, since every
timed arm shares one custom HTTP/2 transport while production C++ users compare against gRPC C++.
Alex answered Q20261103-p5-1 ("P5-1: Just run them, yes.") and Q20261103-p5-3, which asked to
download gRPC for this arm (verbatim: "P6-1, P5-2, P5-3: option 1 on all three"), on 2026-11-03. Arm
G is the arm `hypotheses.md` section 1 left to a later campaign with its own hypotheses and freeze
(Alex's Q1 of 2026-10-31).

Words: as the earlier files. G is arm G, the echo service served by gRPC C++.

## 1. Arm G

gRPC C++ 1.84.2, built by `bench/build_grpc.sh` with the pinned GCC 16.2.0 and the flags of arm P's
protobuf (C++26, -O2 -DNDEBUG, static), on protobuf 36.2 and abseil 20250512.1, the same versions
arm P uses, with c-ares 1.34.9, re2 2025-11-05, and the system's OpenSSL and zlib (`bench/pins.md`,
Lab request 5). The server, `arm_g/g_server.cpp`, serves the same `e2e.proto` service with the same
messages, from code that protoc 36.2 and gRPC's `grpc_cpp_plugin` generate. It follows gRPC's
performance guide (grpc.io, "Performance Best Practices", last modified 12 November 2024) for C++:
it uses the async completion-queue API, which the guide says is faster than the callback API "for
truly high-QPS workloads"; it polls its one completion queue from one thread, the guide's "numcpu's
threads" on the one CPU every arm gets; and it registers 2,048 outstanding requests per method,
more than the 1,536 streams of the heavy load, as the guide asks ("register enough server requests
for the desired level of concurrency"). Each call slot is reused with a fresh `ServerContext`, as
gRPC's own benchmark server does. It is a project of its own (`arm_g/`), so the measured code of
arms R, P, Q and S stays the first campaign's.

The whole G process, gRPC's internal threads included, runs under `taskset -c 14`, the one CPU of
every other arm's worker, with CPU 15 idle.

## 2. The campaign (request 36)

On Linux only: on Windows, ghz bounds the heavy load (`hypotheses.md` section 3), and a MinGW-w64
build of gRPC was not attempted. The arms are R, P, Q and S from the replication's Linux build
(request 29) and G (request 36's build). The cells, the load generator, the windows and their order
inside a server run are the first campaign's on Linux (`hypotheses.md` section 3): payloads small
and medium; per server run a 10 s warm-up at c12 m128, 4 windows at c1 m1 with per-request latency,
then 4 windows at c12 m128; h2load 1.70.0 on CPUs 2 to 13. Ten replicates; in each, the payloads in
a seeded order and, per payload, the five arms in a seeded order (`bench/campaign-m2/
g-order-linux.tsv`, label "P5 grpc e2e linux").

Measure, validity, voids and reruns: as `hypotheses.md` section 3, with G's server log ending in
`g_server: calls ok N, failed F`, which must say `failed 0`.

## 3. Hypotheses and tests

No pilot of G was run, and no direction is taken from one. The contrasts are R against G in each of
the four cells (2 payloads, 2 loads), each tested two-sided: an exact paired sign test over the 10
replicates, p = min(1, 2 min(P(X >= k), P(X <= k))) for k replicates with R above G and X binomial
with 10 trials and 1/2 (a tie counts as R not above). The four form their own family, m = 4, Holm at
alpha 0.05 family-wise; the smallest two-sided p, 2 x 2^-10 = 0.00195, is below 0.05/4 = 0.0125. A
passed contrast's direction is the side of 1 its ratio lies on.

Effect sizes, every cell: the ratio of mean requests per second, R over G, with a 95% BCa interval
from a clustered bootstrap that resamples replicates with all their arms (10,000 resamples, seed
recorded). P over G and Q over G are reported the same way, descriptive, with no test. Secondary,
descriptive, as `hypotheses.md` section 3: latency at c1 m1, CPU shares, peak resident set.

## 4. Gates

D5. G's first-party code, `arm_g/g_server.cpp`, gets its own green records before any G number is
cited (request 35): GCC AddressSanitizer with UndefinedBehaviorSanitizer (without its `null`,
`nonnull-attribute` and `returns-nonnull-attribute` checks, as P5's other GCC records: with them
GCC 16 cannot constant-evaluate abseil) and GCC ThreadSanitizer, each with gRPC, protobuf, abseil,
c-ares and re2 built under the same flags (`build_grpc.sh`, EXTRA_FLAGS), running `arm_g/test_g.cpp` and the sanitized G under h2load for both payloads at both
loads. The measured G build's first-party inputs must equal the records'. MemorySanitizer is a
declared gap (`bench/coverage.json`): GCC has none, and a clang MSan build would need every
dependency instrumented. Arms R, P, Q and S are the replication's build, covered by the records of
9a3dc92.

Numbers come only from `results-m2/macros.tex`, generated by `analysis/analyse_m2.py`.

Laptop time: the gRPC builds (Release, ASan with UBSan, TSan; request 34), unknown until the first
laptop build; the records (request 35), under an hour; the campaign about 2.6 h (100 server runs of
95 s).

## Revision log

None yet.
