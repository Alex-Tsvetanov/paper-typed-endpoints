# P5 typed endpoints: state digest

Read this first. Keep it under 200 lines. Brief: `Papers/p5-typed-endpoints/AGENTS.md`.
Plan: `Papers/plan/merged-plan-v2.md` section 2.5 and P5's row in section 3.2.
Design: `design/design-note.md` (DRAFT, not frozen). Compiler defects: `design/compiler-findings.md`.
Reporting: this session reports to the coordinator; read Papers `CLAUDE.md`, "Coordinator" (Alex, 2026-10-29).

## What P5 is

C++26 static reflection (P2996) generates unary protobuf endpoints from `.proto` text, without
`protoc`. The paper measures compile time against schema size, end-to-end unary calls against
`protoc`-generated code, and interoperability with grpc-go. Unary only. Held for the доцент group,
not the defence list. It extends the accepted TELECOM 2026 paper (ctpb). The ctpb repository
(`D:\Dev\GitHub\Compile-time-Protobuf`) is the conference artifact: read it, never edit it. Its
camera-ready errata (`paper/ERRATA-camera-ready.md` there, untracked) are Alex's open item. The
design note maps each erratum to the P5 test or experiment that replaces it.

## Repository

- Local only: `Papers/p5-typed-endpoints/typed-endpoints`, branch `main`, no remote. It becomes
  `paper-typed-endpoints` on GitHub (Apache-2.0) only at Alex's yes.
- C++ namespace `tep`. Header-only library under `include/tep/`.
  - Portable C++23 (GCC, clang, MSVC): `schema.hpp` (lexer, parser, checks, name resolution,
    definition order), `wire.hpp` (varint, zigzag, fixed, bounded reader, writer, UTF-8),
    `grpc.hpp` (5-byte prefix, status codes, method path), `fixed_string.hpp`.
  - Reflection only: `reflect.hpp`: static schema tables (one parse per translation unit),
    `define_messages<S>()` (one aggregate per message, dependencies first), `message_t`,
    `enum_value`, codec (`byte_size`, `encode_to`, `encode`, `encode_framed`, `merge_from`,
    `decode`), member-wise `operator==`, and `endpoint<S, "pkg.Service", Impl>` with
    `invoke` and `invoke_framed`. Hard `#error` without `__cpp_impl_reflection`; no fallback.
- Supported proto3: scalars, enums (as `int32_t`), nested messages (`tep::box<M>`, one pointer
  when absent), repeated (packed by default, `[packed]` honored), reserved, the descriptor.proto
  options of each level (allow-lists), services with unary rpcs. Unknown and custom options are
  refused (custom options need an import). Refused with a diagnostic: map, oneof, `optional`, `required`, groups, extensions,
  import, editions, streaming, recursive messages, `[default]`, `message_set_wire_format`.
- End-to-end (087812c, `server/`, `TEP_E2E`): arms R and P of an echo service (Person 34 bytes,
  Order 953 bytes) behind one interface, byte-identical (`test_arms`); `tep_server --arm R|P`,
  HTTP/2 on the pinned libnghttp2 1.70.0 (`bench/build_nghttp2.sh`), one pinned worker per CPU,
  epoll on Linux, WSAPoll on Windows (`test_server`, loopback). P: one Arena per request on a reused
  64 KiB block. Q1 to Q4 answered by Alex 2026-10-30 (`design/revision-log.md`): h2load on Linux,
  ghz on Windows; P's arena; MinGW first, else a declared gap. 19/19 in `linux-gcc1620-e2e` (ASan,
  protobuf with the same flags), `-e2e-release`, and `mingw-gcc1620-e2e-release` (MinGW abseil,
  protobuf, nghttp2 unpatched, `bench/build_protobuf_mingw.sh`; libstdc++ dynamic, finding F4).
- Related-work survey (858a9b6): `design/related-work-survey.md`, Sonnet gathered, Opus checked
  every claim (33 corrected, 3 removed, 7 items added, 5 still unverified). The gap held.
- Fuzzing (c1dd0fb): `fuzz/` targets for the schema front end, the wire and gRPC parsers, and the
  decoder (GCC only, round trip). CTest smoke tests in every preset; `linux-clang-fuzz` builds
  libFuzzer executables.
- Decoding: up to 16 fields compare inline, larger messages dispatch through field-number tables
  (613d6fb, 1e4b25d). Repeated fields reserve for the elements that arrive together and grow
  geometrically otherwise (`test_growth`; the old exact reserve per packed chunk was quadratic).
  Unmeasured: nested sizes are recomputed while writing. R loses many from 2048 (A29): it emits
  decode code per field, protoc parses from tables. Rejected on a size proxy, not timed: table
  dispatch in invoke (.text +4.5%), out-of-line slow paths (+4 to 5%). The compile trade-off
  question waits for the medium runtime gap (A31) to be understood.
- Heap allocations per call (`test_allocs`, a count): small 0 for S, R, P; medium S 0, P 32 (its
  16 long strings, parsed and copied; arena strings keep heap buffers), R 93, 62 after the growth
  fix. R's extra 30, by code reading: 14 vectors and the boxed customer, per parse and per copy.
- Constant-evaluation budget: GCC 16's default `-fconstexpr-ops-limit` is 33,554,432 operations
  per outermost evaluation (every expression node counts; a variable template forced early gets
  its own counter, so define_messages forces the tables). `bench/constexpr_ops.sh` bisects it
  (deterministic). At d7d7ab8 the tables take many 1024, 2048, 4096: 8.91M, 17.8M, 35.7M; wide
  1024 6.55M. Every ladder cell fits except many 4096, the one secondary-curve cell (coordinator,
  2026-10-30; `design/revision-log.md`). Code generation, not evaluation, dominates compiles.
- Desktop toolchains (functional builds): `D:\Dev\toolchains\{gcc-16.2.0-linux, protobuf-36.2-linux,
  protobuf-36.2-linux-asan, nghttp2-1.70.0-linux}`, mounted under `/home/dev/opt`; and
  `%USERPROFILE%\opt\{protobuf-36.2-mingw, nghttp2-1.70.0-mingw, protobuf-36.2-msvc-asan,
  nghttp2-1.70.0-msvc-asan}`. `bench/compile_curve.py` builds and checks one R or P cell.

## Build and test (desktop, functional only; no timing)

- Presets in `CMakePresets.json`, build trees under `build/<preset>` (ignored).
- Linux: `docker run --rm --cpus 4 --user dev -v D:\Dev\GitHub\Papers:/d/Dev/GitHub/Papers` and
  each of the four toolchains above `-v D:\Dev\toolchains\<name>-linux[-asan]:/home/dev/opt/<name>[-asan]:ro`,
  `-w <repo> papers-dev:arch-2026-10-23 bash tools/dev_linux_all.sh <presets>` (`MSYS_NO_PATHCONV=1`).
- Windows: from a `vcvars64.bat` shell, `cmake --preset msvc-debug`, build, ctest.
- Results (pass or fail only): growth fix, `linux-gcc1620-{e2e,e2e-release}` 22 of 22,
  `mingw-gcc1620-e2e-release` 21 of 21 (`allocs` is Linux only). At 91dbece `msvc-{debug,asan}` 8 of
  8 portable, `reflection_layer` Skipped with the reason; `msvc-e2e-asan` builds, `server` fails at
  start-up (M2).
- Tests: golden wire vectors from protoc 3.21.12 (`tests/golden/README.md`, a development oracle);
  the encoder matches them byte for byte for all scalar types, enums, nested and repeated fields.
- TSan: not run yet; the server has threads (`test_server`, two workers). Needs a TSan preset, a TSan
  protobuf, and `--security-opt seccomp=unconfined` in the container.

## Toolchain facts (desktop, 2026-10-23)

- GCC 16 (`papers-dev` 16.2.1, pinned 16.2.0): P2996 with `-freflection` (202603L), `#embed`
  (202502L). It refuses `^^std::int32_t`; reflect such types through a template.
- clang 22.1.8 rejects `-freflection`. MSVC 19.51.36246 has no P2996 and no `#embed`.
- CMake 4.3.2 (Windows) has no C++26 flag for MSVC: the MSVC build uses C++23.
- MinGW-w64 GCC 16.2.0 (Alex decides 1): the winlibs r3 zip (276,414,382 bytes, sha256
  3c693050...b6a1, `lab/laptop/versions.md`) unpacked to
  `C:\Users\alext\opt\winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3` (not on PATH). P2996 and `#embed`;
  F1 reproduces, F2 does not; no ASan or UBSan; `thread_local` is emulated (review M1).
- GCC 16.2.0 on every host since 2026-10-29 (Alex decides 10; Laptop request 4; dev image above).
- Compiler findings (`design/compiler-findings.md`): F1 GCC 16 `string_view::find` from an offset
  on a literal in constexpr (avoided); F2 GCC UBSan `null`, `nonnull-attribute`,
  `returns-nonnull-attribute` break constant evaluation (dropped from the preset); F3 clang 22
  with libstdc++ 16, no constexpr `std::string` (clang presets use libc++); F4 MinGW static
  libstdc++ multiple definition (dynamic libstdc++).
- `ctpb-dev:latest` (bfa32b62a446): its protoc 3.21.12 only made the golden vectors.

## Status

- [x] Repository, STATE.md, design note draft, compiler findings.
- [x] Skeleton building; tests pass on Linux, MSVC (portable layers) and MinGW GCC 16 (all).
- [x] Fresh reviews 1 (2026-10-23, 8764052) and 2 (8764052..de304a9): all CRITICAL, HIGH, MEDIUM
      fixed. Open LOW: option value types (`[deprecated = "yes"]` accepted), `1abc` one floating
      token, no decode allocation cap beyond the 4 MiB frame (bounded by sizeof(M) per 2 bytes).
- [x] Third fresh review (Opus, c525595..c1dd0fb, 2026-10-30): 2 HIGH (rpc space reserved per
      service in proportion to the file; rpc names in rpc type lookup), 2 MEDIUM (fuzz checks,
      push_back assert), 3 LOW. HIGH and MEDIUM fixed in 6a0d8c9, verified by the same reviewer.
      Open LOW: run-time hash probing with crafted collisions; reserves kept in the file.
- [x] Fourth fresh review (Opus, 23dfab3..1e4b25d, 2026-10-30): no CRITICAL or HIGH; 4 MEDIUM,
      7 LOW. Fixed and verified: M3 (multi-worker test), M4 (console handler), L5, L7 to L11
      (34f65a0, 91dbece). Open for Alex (via the coordinator): M1 MinGW emulated TLS biases P on
      Windows; M2 Windows sanitizer record (MSVC ASan cannot start protoc's generated code, see
      `bench/coverage.json`; clang-cl 22.1.0 fails to build abseil/protobuf as configured). L6 caveat.
- [ ] Next: Laptop request 8 (second e2e pilot after the growth fix); then R's medium gap (the
      options that change types or reuse messages are Alex's); interop with grpc-go (downloads wait
      for Alex); D5 records at the code freeze.

## Laptop requests

Since 2026-10-26 (Alex, AGENTS.md bc1a76d) a ready request goes straight to the coordinator's
queue; design, scope and pushes still go to Alex.

1. Done 2026-10-24 on Arch (archive `gap1-a-q4-visit-a1-c0eb9960`, sha256 files match): Arch's
   gcc is the image's exact package; F1 reproduced at `-O1` and `-O2`; the probe printed its line.
2. Done 2026-10-29 (A23 at c525595, archive `gap44-a-q1-p5-pilot-c2670a4d`, sha256 4b01a015...cc82).
   Pilot. Replicates within 0.3%; at 1024 R spends 62 to 78% in code generation.
3. Done 2026-10-30 (A31 try 4 at fce6a17, `bench/requests/e2e-pilot.md`; archives
   `gap54-a-q1-p5-e2e-build3-7186ae39` sha256 3ac6e3dc...9a7a, `gap52-a-q2-p5-e2e-run-2bd355f5`
   sha256 30ed7123...619e). Pilot, not a result. 132 windows, 0 failed, all 2xx, server counts in
   the bound; the job's count also matched `h2load-version.txt`. Window spread at most 1.7%. Knee
   at c12 m128; c48 adds nothing (small) or loses (medium); c1 m1 is client-bound (about 30k req/s,
   every arm). R/P from c12: small 1.01 to 1.07 (every R window above every P window), medium 0.97,
   0.92, then 0.79 to 0.87 (every P window above). Medium order S P R; clock floor held.
4. Done 2026-10-29 (A19, archive `gap40-a-q1-p5-gcc-877d16d0`, sha256 17fac736...0beb):
   `~/opt/gcc-16.2.0` on Arch; 14 of 14, F1 as on the desktop; make -j14 2,611 s.
5. Done 2026-10-30 (A27 at 4d205b4, archive `gap48-a-q1-p5-rerun-0763609d`, sha256 22574257...4710).
6. Done 2026-10-30 (A28 at 614c9f4, archive `gap49-a-q1-p5-rerun2-2228616a`, sha256 b90f1c76...e749).
7. Done 2026-10-30 (A29 at fa60375, archive `gap50-a-q1-p5-rerun3-4b1cde4a`, sha256 345bea80...bf09):
   51/51 built and checked. P across A23, A27, A29 within 0.9% per cell. Same run, R/P: 0.29 and
   0.30 at 16; wide 0.86, 0.90, 0.77 (1024, 2048, 4096); many 1.00, 1.20, 1.56 (R loses from 2048;
   many 4096 at the raised limit). RSS: R lower for wide, 2.9 times P at many 4096. A27 to A28 cut
   R wide (4096: 246.90 to 126.62 s, code generation 229 to 106 s); many unchanged. Pilot.
8. Ready: `bench/requests/e2e-pilot-2.md`, the first pilot again after the growth fix, R and P
   swapped in each payload's order.

## Lab requests

Specs for 1 to 3 (exact versions, URLs, published sha256): `bench/pins.md` (2026-10-29).

1. Done 2026-10-29: protobuf 36.2 and abseil 20250512.1 in `C:\Users\alext\opt\_dl\p5-protobuf-36.2`.
2. Pin Go 1.27.2 (Alex, D2: the same as P4 and P7) and grpc-go for the interop programs (go.dev publishes sha256 for Go archives; grpc-go
   through `go.sum`). Go is installed on the desktop (`C:\Program Files\Go`), version not recorded.
3. Pin nghttp2 (source) for the transport and h2load; the image has `libnghttp2` 1.70.0 only.
4. Done 2026-10-24 (job `p5-mingw-w`): the MinGW-w64 GCC 16.2.0 zip on the laptop's Windows, checked.

## Alex decides

Answered by Alex on 2026-10-23 (AskUserQuestion, recommended option each time):

1. Windows coverage for the reflection layer: MinGW-w64 GCC 16, probe first. A pinned MinGW-w64
   GCC 16 is probed on the desktop; if it builds and passes the reflection tests, it builds both
   generators on the laptop's Windows. If the probe fails, Windows is a declared gap (Linux only).
2. D5 for the reflection layer: GCC ASan, UBSan (without `null`, `nonnull-attribute`,
   `returns-nonnull-attribute`) and TSan records; the full clang set for the portable layers;
   the gaps declared in `bench/coverage.json`.
3. gRPC C++ reference arm G: kept, built after the reflection and protoc arms.
4. Load generator: h2load (pinned nghttp2 build) drives the timed runs; ghz runs the grpc-go
   interop check and one cross-check.
5. GCC defects F1 and F2: I search GCC Bugzilla for duplicates and draft both reports in
   `design/`; Alex files them himself. Nothing is sent on his behalf.

From the laptop queue plan, answered by Alex on 2026-10-23 (relayed by the coordinator;
`Papers/lab/laptop/queue-plan-2026-10-draft.md`, section 10):

6. Gap block approved: Lab request 4 (the MinGW-w64 GCC 16.2.0 zip copied to the laptop's
   Windows and checked) and Laptop request 1 (the Arch gcc check and the `define_aggregate`
   probe) run in laptop gaps. The coordinator's operator reads both from this file.
7. D1: no `pacman` upgrade or install on the laptop's Arch until P4's Arch blocks end. If Arch's
   gcc package differs from the image's, the fix is a side-by-side GCC build under `~/opt`,
   never a `pacman` change.
8. D2: P5 uses Go 1.27.2, the same as P4 and P7 (Lab request 2).
9. D4: P4's idle day carries only read-only jobs.
10. GCC version (2026-10-29, relayed by the coordinator; question "Declare the gap, or pin 16.2.0
    on both?"): "Pin 16.2.0 on both". Linux build request: `bench/requests/gcc-16.2.0.md`.

Open: none. The two report drafts are in `design/gcc-report-drafts.md` (F2 is probably GCC PR
71962; Bugzilla refuses automated searches, so Alex checks for duplicates by hand before filing).

## Never

No timing on the desktop, of any kind, anywhere (not even ctest seconds in notes). No laptop access.
No push, no remote. Never edit ctpb, frozen files, records or `writing/`. Never name the integration
library (the name guard test stores digests only). No em or en dashes.
