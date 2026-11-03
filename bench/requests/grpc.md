# Request: the gRPC C++ arm G on Linux (Laptop requests 34 to 36)

Plain: the work of `hypotheses-m2.md`. Request 34 builds gRPC 1.84.2 on the laptop three times
(Release, and under each sanitizer); request 35 makes arm G's D5 records; request 36 builds the
measured G and runs the campaign of arms R, P, Q, S and G. All on the laptop's Linux, after
requests 29 and 31 to 33. 34 and 35 are untimed; 36's build is untimed and its run is timed.

Commit: P5 at M2_FREEZE, the commit that adds `hypotheses-m2.md` (its hash is in the coordinator
message that queues these requests); every job refuses any other HEAD and runs the measured-code
guard of `replication.md` item 1, which must exit 0 (arm G lives in `arm_g/`, outside the guarded
paths). Job names `p5-m2-grpc-build-l`, `p5-m2-records-l`, `p5-m2-g-build-l`, `p5-m2-g-run-l`.

## Request 34: three gRPC prefixes (untimed)

Copy the desktop's `C:\Users\alext\opt\_dl\p5-grpc-1.84.2\` (ten files with `SHA256SUMS`; Alex's
yes of 2026-11-03, `bench/pins.md` Lab request 5) to the laptop; `sha256sum -c --strict SHA256SUMS`
there. The protobuf downloads are already on the laptop from Laptop request 3
(`p5-protobuf-36.2`). Then, with the pinned GCC 16.2.0 (`~/opt/gcc-16.2.0`):

```
bash bench/build_grpc.sh <grpc downloads> <protobuf downloads> ~/opt/grpc-1.84.2 14
ASAN_OPTIONS=detect_leaks=0 EXTRA_FLAGS="-fsanitize=address,undefined -fno-sanitize=null,nonnull-attribute,returns-nonnull-attribute -fno-sanitize-recover=all -fno-omit-frame-pointer" \
  bash bench/build_grpc.sh <grpc downloads> <protobuf downloads> ~/opt/grpc-1.84.2-asan 14
EXTRA_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  bash bench/build_grpc.sh <grpc downloads> <protobuf downloads> ~/opt/grpc-1.84.2-tsan 14
```

UBSan runs without its `null`, `nonnull-attribute` and `returns-nonnull-attribute` checks, as in P5's
other GCC records (finding F2): with them, GCC 16 cannot constant-evaluate abseil's
`hash_policy_traits.h`, and the abseil build stops (seen in the desktop's dry run, 2026-11-03).
`detect_leaks=0` only while building: the sanitized protoc and `grpc_cpp_plugin` run during the
build, and a leak report at their exit would stop it. Each build ends by printing `libprotoc 36.2`
and the gRPC, protobuf, c-ares and re2 libraries; keep `<prefix>/logs` (the system's OpenSSL and
zlib versions are in `packages.txt`). The builds run without network: gRPC's six definition archives
are unpacked by the script. Each prefix folder is new; the job refuses if one exists.

## Request 35: arm G's D5 records (untimed)

For each record, from `arm_g/`: `cmake --preset <preset>`, `cmake --build`, `ctest --output-on-failure`
(with `ASAN_OPTIONS=detect_leaks=0` for the build step of `g-asan` only, as in 34; the tests run with
the sanitizers' defaults), then the load below, and count sanitizer reports with the lab's shared
report pattern in every log, the server logs included. Green: the one test (`g_server`) passes, the
load's checks pass, and 0 reports.

| Record | Preset | Prefix |
|---|---|---|
| L GCC ASan+UBSan, arm G | `g-asan` | `~/opt/grpc-1.84.2-asan` |
| L GCC TSan, arm G | `g-tsan` | `~/opt/grpc-1.84.2-tsan` |

The load runs G's measured path under the sanitizer: the record's `g_server --port 50051` under
`taskset -c 14` (`--threads 1`, the measured setting), driven by `~/opt/h2load-1.70.0/bin/h2load` on
CPUs 2 to 13 with each body of request 29's `bodies/` (`small.bin` on `EchoPerson`, `medium.bin` on
`EchoOrder`, the gRPC headers of request 24), in count mode: 20,000 calls at 1 connection of 1 stream
and 20,000 at 12 connections of 128 streams, for each body. Every h2load file must show 0 failed, 0
errored, 0 timeout and only 2xx; the server is stopped with SIGTERM, must exit 0, and its last line
must be `g_server: calls ok 80000, failed 0`.

Inputs hash: `lab/bin/inputs_hash.py` on the target `g_server` of each record's build tree, with
`--list <record>.inputs.tsv`. The first-party inputs are `arm_g/g_server.cpp` and the generated
`e2e.pb.*` and `e2e.grpc.pb.*`; option that selects code: none. Declared gap: MemorySanitizer
(`bench/coverage.json`).

## Request 36: the measured G and the campaign

Build job (untimed): it refuses to start unless both records of request 35 are green (D5); a red or
missing record stops request 36 before its build. Then `cmake --preset g-release` from `arm_g/` into a
new folder, build, `ctest`: the one test passes. The inputs hash of `g_server` with its list must equal both records' of request 35,
file by file; a difference stops the job before the run job. Keep the sha256 of `g_server`.

Run job (timed; the quiet check at job start): request 29's run job (`e2e-campaign.md` as amended by
`replication.md`) with these differences.
- The arms are R, P, Q and S from request 29's build (`~/lab/work/p5-r2-e2e`, checked against its
  `SHA256SUMS`) and G from this build (its sha256 checked).
- The order file `bench/campaign-m2/g-order-linux.tsv` (sha256
  bd28e6a7447d88f06622238e2265020f2cbc6b1b3cf03d899ac5fca8d3b1994a): a `#` line, a header row, then
  100 rows `index rep payload arm`, checked and copied to the output folder before the first window.
- G is started as `taskset -c 14 <build>/g_server --port <port> --threads 1`, waiting for its
  `g_server: arm G` line, and stopped with SIGTERM; its log is `server-G-<payload>-<tag>.log`, whose
  last line must be `g_server: calls ok N, failed 0` with N in the same bound as tep_server's.
- Windows inside a server run, file names, latency logs, `shares.jsonl` (the process name of G is
  `g_server`), `peak.jsonl`, validity, voids, reruns and `validate.txt`: as request 29's run job,
  with arm letters R, P, Q, S and G. No ghz cross-check.

Checks: the executed order equals the order file; 900 window files (100 runs of 9 windows), 400
latency logs, 100 server logs, 100 peak readings, a shares line for every window, plus the reruns'
files. Exit codes as `compile-campaign.md`. The analysis is `analysis/analyse_m2.py --g-linux <36 run>/out`.

## Expected time

34: three gRPC builds; their time is unknown until the first (a desktop build in the dev image is
the only reference, and its time is not recorded). 35: two arm G builds, two test runs and the
count-mode loads, under an hour. 36: 100 server runs of about 95 s, about 2.6 h, plus any reruns; a
slot of 4 hours.
