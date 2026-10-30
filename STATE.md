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
- Supported proto3: scalars, enums (as `int32_t`), nested messages (`tep::box<M>`, one pointer,
  keeps storage when cleared), repeated (`tep::repeated<T>` for messages, strings and bytes, which
  keeps cleared elements; `std::vector` otherwise; packed by default), reserved, the descriptor.proto
  options of each level (allow-lists), services with unary rpcs. Unknown and custom options are
  refused (custom options need an import). Refused with a diagnostic: map, oneof, `optional`, `required`, groups, extensions,
  import, editions, streaming, recursive messages, `[default]`, `message_set_wire_format`.
- End-to-end (`server/`, `TEP_E2E`): arms R, P, Q and S of an echo service (Person 34 bytes, Order
  953 bytes) behind one interface, R, P and Q byte-identical (`test_arms`); `tep_server --arm
  R|P|Q|S`, HTTP/2 on the pinned libnghttp2 1.70.0, one pinned worker per CPU, epoll on Linux,
  WSAPoll on Windows. P: one Arena per request on a reused 64 KiB block. Q (Alex, 2026-10-30):
  protobuf with reused non-arena messages, per its C++ tutorial's optimization tips. R: the
  endpoint keeps one request and one response per rpc (`parse_from`, `clear`), under Q2. Q and R
  drop reused messages after a call over 64 KiB. MinGW: libstdc++ dynamic (finding F4).
- Related-work survey (858a9b6): `design/related-work-survey.md` (5 claims still unverified). The gap held.
- Fuzzing (c1dd0fb): `fuzz/` targets for the schema front end, the wire and gRPC parsers, and the
  decoder (GCC only, round trip). CTest smoke tests in every preset; `linux-clang-fuzz` builds
  libFuzzer executables.
- Interop (design note 6, functional): `interop/go` (Go 1.27.2, grpc v1.84.0, `go.sum`), built by
  `tools/build_interop.sh` from the module cache (`C:\Users\alext\opt\_dl\p5-go-modcache`, 117 MB);
  `test_interop` (`-DTEP_INTEROP_BIN`, `-DTEP_GHZ`): grpc-go client and ghz against R and P, tep
  against the grpc-go server; each side checks every field. Passes on Linux (Release, ASan), MinGW.
- Decoding: up to 16 fields compare inline, larger messages through field-number tables. Repeated
  fields reserve for elements that arrive together, else grow geometrically (`test_growth`). Encoding
  writes back to front, sizing each message once (A35: sizing Item again was 7.4% of R's medium
  cycles). R loses many from 2048 (A29): per-field decode against protoc's tables.
- Heap allocations per call (`test_allocs`, a count, identical requests): small 0 for every arm;
  medium S 0, P 32 (arena strings keep heap buffers), Q 0 (protobuf's cleared repeated fields
  keep their elements), R 93, 62 (growth fix), 27 (reuse), 0 (keep-storage types, Alex). Reuse:
  `parse_from` is clear plus merge (fuzzed, split inputs); kept storage holds no earlier data
  (`tests/kept_storage.hpp`); slack rule per container; slots taken out per call (re-entry safe).
  Size proxy, not timed: .text many 1024 +15.2%, wide 1024 +2.4% (60eff0e to reuse); then many 1024
  +0.5%, many 2048 +0.6%, wide 1024 -5.8% (back writer; GIMPLE statements many 1024 -0.6%).
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
- Results (pass or fail only), handler table, with interop: `linux-gcc1620-{e2e,e2e-release}` 25
  of 25 (`allocs` Skipped under ASan), `mingw-gcc1620-e2e-release` 24 of 24, `fuzz_decode` 300,000
  mutations under ASan+UBSan; `msvc-e2e-asan` 9 of 9 at 60eff0e (transport only).
- Tests: golden wire vectors from protoc 3.21.12 (`tests/golden/README.md`, a development oracle);
  the encoder matches them byte for byte for all scalar types, enums, nested and repeated fields.
- TSan: not run yet (server threads); needs a preset, a TSan protobuf, `seccomp=unconfined` in docker.

## Toolchain facts (desktop, 2026-10-23)

- GCC 16.2.0: P2996 with `-freflection` (202603L), `#embed`; refuses `^^std::int32_t` (use a template).
- clang 22.1.8 rejects `-freflection`. MSVC 19.51.36246: no P2996, no `#embed`, C++23 (CMake 4.3.2).
- MinGW-w64 GCC 16.2.0 (Alex decides 1; winlibs r3, `lab/laptop/versions.md`) in
  `C:\Users\alext\opt\winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3` (not on PATH). P2996 and `#embed`;
  F1 reproduces, F2 does not; no ASan or UBSan; `thread_local` is emulated (review M1).
- Compiler findings (`design/compiler-findings.md`): F1 GCC 16 `string_view::find` from an offset
  on a literal in constexpr (avoided); F2 GCC UBSan `null`, `nonnull-attribute`,
  `returns-nonnull-attribute` break constant evaluation (dropped from the preset); F3 clang 22
  with libstdc++ 16, no constexpr `std::string` (clang presets use libc++); F4 MinGW static
  libstdc++ multiple definition (dynamic libstdc++).

## Status

- [x] Repository, design note draft, compiler findings; tests pass on Linux, MSVC (portable), MinGW.
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
- [ ] Next: Laptop requests 13 and 14 (compile rerun, pilot 5) at the keep-storage commit; then
      the paper's runs; D5 records at the code freeze. Alex answered both questions (revision log).

## Laptop requests

Since 2026-10-26 (Alex, AGENTS.md bc1a76d) a ready request goes straight to the coordinator's
queue; design, scope and pushes still go to Alex.

1. Done 2026-10-24 on Arch (archive `gap1-a-q4-visit-a1-c0eb9960`, sha256 files match): Arch's
   gcc is the image's exact package; F1 reproduced at `-O1` and `-O2`; the probe printed its line.
2. Done 2026-10-29 (A23 at c525595, `gap44-a-q1-p5-pilot-c2670a4d`, 4b01a015...cc82): within 0.3%.
3. Done 2026-10-30 (A31 at fce6a17; `gap52-a-q2-p5-e2e-run-2bd355f5` 30ed7123...619e): knee c12 m128.
4. Done 2026-10-29 (A19, archive `gap40-a-q1-p5-gcc-877d16d0`, sha256 17fac736...0beb):
   `~/opt/gcc-16.2.0` on Arch; 14 of 14, F1 as on the desktop; make -j14 2,611 s.
5. and 6. Done 2026-10-30: A27 at 4d205b4 (`gap48-a-q1-p5-rerun-0763609d`, sha256 22574257...4710),
   A28 at 614c9f4 (`gap49-a-q1-p5-rerun2-2228616a`, sha256 b90f1c76...e749).
7. Done 2026-10-30 (A29 at fa60375, archive `gap50-a-q1-p5-rerun3-4b1cde4a`, sha256 345bea80...bf09):
   51/51; P across A23 to A29 within 0.9%. R/P 0.29, 0.30 at 16; wide 0.86, 0.90, 0.77 (1024 to
   4096); many 1.00, 1.20, 1.56. RSS: R 2.9 times P at many 4096. A27 to A28 cut R wide 4096 by half.
8. Done 2026-10-30 (A33 at 16fdbd5; `gap55-a-q1-p5-e2e2-build-cb4bb3c2` 9fe06b9d...d036,
   `gap55-a-q2-p5-e2e2-run-f472a6ce` b083da61...080f). Pilot. libc 22.5% of R's medium cycles (62 allocations).
9. Done 2026-10-30 (A35 at 3537817; `gap57-a-q1-p5-e2e3-build-b0ae2b00` b445446f...f82f,
   `gap57-a-q2-p5-e2e3-run-c201db87` a6f99e27...b3d1). Pilot. Q beat R on medium (0.84 to 0.91), R beat
   P and Q on small; order confounded (pilot 4 settles it). Medium user cycles per request: R 9,178.
10. Done 2026-10-30 (A38 step 1 at 30818a5; `gap60-a-q1-p5-rerun4b-c9812e38` 5f817497...acc6): 51/51, P
    within 0.8% of A29. R against A29, 16 to 4096: many +8, +24, +27, +24%, wide +6 to -2.4%. R/P many
    0.33, 1.24, 1.52, 1.92, wide 0.30, 0.89, 0.90, 0.74. Reuse grew instantiation and code generation.
11. Done 2026-10-30 (A38b at 30818a5, `e2e-pilot-4.md`; `gap60-a-q2-p5-e2e4-build-8df8d661` eac51854...43dd,
    `gap60-a-q3-p5-e2e4-run-3d198c01` 4db2d9ce...3273). Pilot. 264 windows, checks pass, allocs unchanged.
    An arm's second run within 1.6% of its first: no order effect. Every window: R beats P (small 1.02
    to 1.08, medium 1.01 to 1.12) and Q on small from c12 m8 (1.02); Q beats R on medium from c12 m8
    (0.94 to 0.85); ties at c1 and c12 m1. R medium +2 to 3% over A35 (back writer). Medium user cycles
    per request (proxy): R 8,906, Q 7,755; own code R 5,598, Q 5,737; libc R 1,651, Q 321: allocation.
12. Done 2026-10-30 (A39 at 15aaa6a, `compile-handlers.md`; `gap61-a-q1-p5-handlers-79de7859` 5ab64c2a...29f4).
    Pilot. Every build separated: R many 1024 -4.9%, 2048 -13.1%, 4096 -24.3% against 30818a5 (4096
    170.0 s, A29 181.2 s); constant evaluation 6.22 to 3.59 s (1024), 18.96 to 8.10 s (2048). R/P many
    1.18, 1.32, 1.46 (A29 1.00, 1.20, 1.56; P above 1024 from A38). What reuse still costs is code.
13. and 14. Queued as chain A40 at db1f31c, after P4's A37: `compile-rerun-5.md`, `e2e-pilot-5.md`.

## Lab requests

Specs for 1 to 3 (exact versions, URLs, published sha256): `bench/pins.md` (2026-10-29).

1. Done 2026-10-29: protobuf 36.2 and abseil 20250512.1 in `C:\Users\alext\opt\_dl\p5-protobuf-36.2`.
2. Done 2026-10-30 (Alex: "Download all (Recommended)"): Go 1.27.2 and ghz v0.121.0 for Windows and
   Linux, sha256 equal to `bench/pins.md`, in `%USERPROFILE%\opt` and `D:\Dev\toolchains`; modules by `go.sum`.
3. Done 2026-10-30: nghttp2 1.70.0 and libev 4.33 (source). 4. Done 2026-10-24: MinGW zip on the laptop.

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

6. Gap block approved: Lab request 4 (MinGW zip) and Laptop request 1 (Arch gcc, probe); both done.
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
