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
  keep their elements), R 93, then 62 (growth fix), then 27 (reuse: the copy into the cleared
  response builds 12 items, the customer and one long label; a std::vector cannot keep cleared
  elements). Reuse: `parse_from` gives decode's result (fuzzed, split inputs), retained storage
  within twice the last messages' (slack rule), slots taken out per call (re-entry safe).
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
- Results (pass or fail only), back writer, with interop: `linux-gcc1620-{e2e,e2e-release}` 23
  of 23 (`allocs` Skipped under ASan), `mingw-gcc1620-e2e-release` 22 of 22, `fuzz_decode` 300,000
  mutations under ASan+UBSan; `msvc-e2e-asan` 9 of 9 at 60eff0e (transport only).
- Tests: golden wire vectors from protoc 3.21.12 (`tests/golden/README.md`, a development oracle);
  the encoder matches them byte for byte for all scalar types, enums, nested and repeated fields.
- TSan: not run yet; the server has threads (`test_server`, two workers). Needs a TSan preset, a TSan
  protobuf, and `--security-opt seccomp=unconfined` in the container.

## Toolchain facts (desktop, 2026-10-23)

- GCC 16.2.0: P2996 with `-freflection` (202603L), `#embed`; refuses `^^std::int32_t` (use a template).
- clang 22.1.8 rejects `-freflection`. MSVC 19.51.36246: no P2996, no `#embed`, C++23 (CMake 4.3.2).
- MinGW-w64 GCC 16.2.0 (Alex decides 1; winlibs r3, `lab/laptop/versions.md`) in
  `C:\Users\alext\opt\winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3` (not on PATH). P2996 and `#embed`;
  F1 reproduces, F2 does not; no ASan or UBSan; `thread_local` is emulated (review M1).
- GCC 16.2.0 on every host since 2026-10-29 (Alex decides 10; Laptop request 4; dev image above).
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
- [ ] Next: attribute the reuse compile cost (proxies); pilot 4 (A38b); then one question to Alex (R
      against Q on medium, many-shape compile with reuse); D5 records at the code freeze.

## Laptop requests

Since 2026-10-26 (Alex, AGENTS.md bc1a76d) a ready request goes straight to the coordinator's
queue; design, scope and pushes still go to Alex.

1. Done 2026-10-24 on Arch (archive `gap1-a-q4-visit-a1-c0eb9960`, sha256 files match): Arch's
   gcc is the image's exact package; F1 reproduced at `-O1` and `-O2`; the probe printed its line.
2. Done 2026-10-29 (A23 at c525595, `gap44-a-q1-p5-pilot-c2670a4d`, 4b01a015...cc82): within 0.3%.
3. Done 2026-10-30 (A31 at fce6a17; archive `gap52-a-q2-p5-e2e-run-2bd355f5` sha256 30ed7123...619e).
   Pilot. Spread at most 1.7%; knee c12 m128; c1 m1 client-bound (about 30k req/s, every arm).
4. Done 2026-10-29 (A19, archive `gap40-a-q1-p5-gcc-877d16d0`, sha256 17fac736...0beb):
   `~/opt/gcc-16.2.0` on Arch; 14 of 14, F1 as on the desktop; make -j14 2,611 s.
5. and 6. Done 2026-10-30: A27 at 4d205b4 (`gap48-a-q1-p5-rerun-0763609d`, sha256 22574257...4710),
   A28 at 614c9f4 (`gap49-a-q1-p5-rerun2-2228616a`, sha256 b90f1c76...e749).
7. Done 2026-10-30 (A29 at fa60375, archive `gap50-a-q1-p5-rerun3-4b1cde4a`, sha256 345bea80...bf09):
   51/51; P across A23 to A29 within 0.9%. R/P 0.29, 0.30 at 16; wide 0.86, 0.90, 0.77 (1024 to
   4096); many 1.00, 1.20, 1.56. RSS: R 2.9 times P at many 4096. A27 to A28 cut R wide 4096 by half.
8. Done 2026-10-30 (A33 at 16fdbd5; archives `gap55-a-q1-p5-e2e2-build-cb4bb3c2` sha256
   9fe06b9d...d036, `gap55-a-q2-p5-e2e2-run-f472a6ce` sha256 b083da61...080f). Pilot. Checks pass.
   R/P small 1.03 to 1.08, medium 0.92 to 1.01. perf (cycles:u, c12 m128, sums of listed symbols):
   libc 22.5% of R's medium cycles against 9.0% of P's: R's heap allocations (62 per call then).
9. Done 2026-10-30 (A35 at 3537817; archives `gap57-a-q1-p5-e2e3-build-b0ae2b00` sha256
   b445446f...f82f, `gap57-a-q2-p5-e2e3-run-c201db87` sha256 a6f99e27...b3d1). Pilot. Checks pass;
   allocs R 27, P 32, Q 0; spread at most 1.8%. Every window: R beats P (small 1.02 to 1.07, medium
   1.01 to 1.11) and Q on small (1.005 to 1.03); Q beats R on medium (0.99, then 0.84 to 0.91 from c12
   m8), but the later arm lost in both orders. Profile proxy, user cycles per medium request: R 9,178
   (A33 10,291), P 9,535, Q 7,927; libc R 16.3%, Q 4.7%; R's own code ties Q's: the gap is allocation.
10. Done 2026-10-30 (A38 step 1 at 30818a5, `compile-rerun-4.md`; archive `gap60-a-q1-p5-rerun4b-c9812e38`
    sha256 5f817497...acc6): 51/51, P within 0.8% of A29. R against A29, 16 to 4096: many +8, +24, +27,
    +24%; wide +6, +3.5, 0, -2.4%. R/P many 0.33, 1.24, 1.52, 1.92 (R loses from 1024); wide 0.30,
    0.89, 0.90, 0.74. R many 1024: instantiation 5.2 to 8.0 s, code generation 21.6 to 26.5 s,
    parsing unchanged, RSS 947 to 1,234 MiB. The reuse code (one slot per rpc, replace mode). Pilot.
11. Queued as A38 steps 2 and 3 at 30818a5: `bench/requests/e2e-pilot-4.md` (scripts: I review first).

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
