# Request: D5 sanitizer records (Laptop request 16)

Plain: make P5's sanitizer records at the frozen commit, so that the campaign's numbers can be
cited (CLAUDE.md D5). Linux records on the laptop's Arch, the transport's record on the laptop's
Windows. Not timed. The coordinator makes the records with the lab's record tools; this request
names what each record builds, runs and must show.

Scope (Alex's decision 2 of 2026-10-23, STATE.md; `bench/coverage.json`): GCC ASan, UBSan
(without `null`, `nonnull-attribute`, `returns-nonnull-attribute`, finding F2) and TSan for the
reflection layer and the server; the full clang set for the portable layers; MSVC ASan of the
transport on Windows only (Alex, 2026-10-30, review M2). Declared gaps stay in `coverage.json`:
MSan for the reflection layer (no compiler), arms P and Q under MSVC ASan, every Windows build of
the reflection layer (MinGW has no sanitizers; the Linux records cover the same first-party files).

## Commit

The frozen commit, named in the queue entry. Before the freeze, a dry run at the newest commit
shows a red record while it can still be fixed.

## Prerequisites (untimed, laptop's Arch)

1. Already there: `~/opt/gcc-16.2.0`, `~/opt/nghttp2-1.70.0`, `~/opt/h2load-1.70.0`, clang 22.1.8 and
   `~/opt/libcxx-msan-gcc` (the MSan-instrumented libc++ P1's records use). From Laptop request 19
   (`libcxx-22.1.8.md`): `~/opt/libcxx-22.1.8`, the plain libc++ the clang ASan and TSan presets
   link (the dry run of 2026-10-31 found no libc++ on the laptop).
2. Copy `C:\Users\alext\opt\_dl\p5-protobuf-36.2\` from the desktop (as in Laptop request 3), then
   build abseil and protobuf twice with the presets' sanitizer flags (protobuf's data layouts and
   checks change under the sanitizers, so it must be built with the same flags):
   ```
   EXTRA_FLAGS="-fsanitize=address,undefined -fno-sanitize=null,nonnull-attribute,returns-nonnull-attribute -fno-sanitize-recover=all -fno-omit-frame-pointer" \
     bash bench/build_protobuf.sh <download folder> ~/opt/protobuf-36.2-asan 14
   EXTRA_FLAGS="-fsanitize=thread" bash bench/build_protobuf.sh <download folder> ~/opt/protobuf-36.2-tsan 14
   ```
   Each prints `libprotoc 36.2`. On the desktop both built in the dev image.

## Records

Each record: configure the preset, build, run the whole CTest suite (`--output-on-failure`), and
count sanitizer reports with the lab's shared report pattern in every log, the server logs
included. Green: every test passes or is a declared skip, and 0 reports.

| Record | Preset | CTest in the desktop's dev image | Then |
|---|---|---|---|
| L GCC ASan+UBSan | `linux-gcc1620-e2e` | 25 tests with the interop test, all pass but `allocs`, skipped (it counts heap allocations, which ASan replaces); 24 on the laptop, which builds no interop programs | `fuzz_decode_smoke 300000 tests/golden/everything.bin`, and the load below |
| L GCC TSan | `linux-gcc1620-e2e-tsan` (docker needs `--security-opt seccomp=unconfined`) | 24 tests without the interop test, all pass but `allocs`, skipped (as under ASan) | the load below, two workers |
| L clang ASan+UBSan | `linux-clang-asan` | 8 tests, all pass but `reflection_layer`, skipped (clang has no P2996) | none |
| L clang TSan | `linux-clang-tsan` | 8 tests, as above | none |
| L clang MSan | `linux-clang-msan` | not run: the desktop has no instrumented libc++ | none |
| W MSVC ASan | `msvc-e2e-asan` (transport and arm S only) | 9 of 9 at 60eff0e (desktop MSVC) | none |

The load runs the measured code paths under the sanitizer: the record's `tep_server` for each arm
S, R, P and Q, one at a time, driven by `~/opt/h2load-1.70.0/bin/h2load` with each body
(`server/bodies/small.bin`, `medium.bin`), 2 windows of 5 s at 12 connections of 32 streams, as the
pilots call it. Placement from `lab/laptop/README.md`: for ASan one worker on CPU 14 and h2load on
CPUs 2 to 13; for TSan two workers (`--cpus 12,14`) and h2load on CPUs 2 to 11. Each server log
must end in `failed 0` with `calls ok` within the pilots' bound, and hold no sanitizer report. In
the desktop's dev image the same load in count mode (20,000 calls per arm and body, 12 connections
of 32 streams; one worker under ASan, two under TSan) gave 0 failed and no report for every arm.

The Windows record builds `nghttp2-1.70.0-msvc-asan` with `bench/build_deps_msvc.sh` (inside a
vcvars64 environment, from the copied `nghttp2-1.70.0.tar.xz`), where the preset expects it under
`%USERPROFILE%\opt`, rather than reusing P3's build, whose options may differ.

## Inputs hash

The measured end-to-end binary is `tep_server` (target `tep_server`, presets
`linux-gcc1620-e2e-release` and `mingw-gcc1620-e2e-release`); its first-party inputs are
`include/tep/`, `server/` and the generated `e2e.pb.*`. The compile-curve builds compile
`include/tep/` with a generated schema (`bench/compile_curve.py`); the records' test targets
compile the same headers. Options that select code: `TEP_E2E`, `TEP_E2E_TRANSPORT_ONLY`,
`TEP_FUZZ`. How a record gates the compile-curve builds is the lab's rule to apply.

`lab/bin/inputs_hash.py` hashes a target's own translation units and their headers, not the
libraries it links, and `tep_server`'s own object is `main.cpp` alone (`server/CMakeLists.txt`):
the measured code is in the static libraries `e2e_transport`, `e2e_arms` and `e2e_pb`. Every GCC
and MSVC record, and every measured release build, therefore hashes the targets `tep_server`,
`e2e_transport`, `e2e_arms` and, where the build has it (not under `TEP_E2E_TRANSPORT_ONLY`),
`e2e_pb`. The dry run of 2026-10-31 hashed `tep_server` alone: 4 files, 1 translation unit.

The clang records build no `tep_server` (the reflection layer needs P2996, which clang lacks), so
their inputs hash names the portable targets they build: `test_schema`, `test_wire`, `test_grpc`,
`fuzz_schema_smoke` and `fuzz_wire_smoke`, with the same root label and option keys. On the desktop
(`lab/bin/inputs_hash.py` on a `linux-clang-asan` build) these hash `include/tep/schema.hpp`,
`wire.hpp`, `grpc.hpp` and their tests and fuzz drivers: the portable layers that `tep_server` also
compiles. The dry run of 2026-10-31 hashed `tep_server` in the clang MSan record, found no objects,
and wrote no inputs file.

## Outputs

Per record: the JSON record, the full logs under `~/lab/records-logs/<record>/` with a sha256, the
compiler identification, and the protobuf build logs.

## Expected time

No measured basis on the laptop. Two sanitizer builds of abseil and protobuf, five configure,
build and test cycles, and eight short server runs in each of the two GCC records; a cap of
3 hours bounds the Linux part and 1 hour the Windows part.
