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
  cycles). R's many compiles lose to P from 512 fields (Release flags; Laptop requests 20, 21).
- Heap allocations per call (`test_allocs`, a count, identical requests): small 0 for every arm;
  medium S 0, P 32 (arena strings keep heap buffers), Q 0 (protobuf's cleared repeated fields
  keep their elements), R 93, 62 (growth fix), 27 (reuse), 0 (keep-storage types, Alex; A40 on the
  laptop). Reuse: `parse_from` is clear plus merge (fuzzed, split inputs); kept storage holds no
  earlier data (`tests/kept_storage.hpp`); slack rule per container; slots taken out per call.
  Desktop size proxies (GIMPLE statements, .text; not timed) precede each laptop compile run.
- Constant-evaluation budget: GCC 16's default `-fconstexpr-ops-limit` is 33,554,432 operations
  per outermost evaluation (every expression node counts; a variable template forced early gets
  its own counter, so define_messages forces the tables). `bench/constexpr_ops.sh` bisects it
  (deterministic). At d7d7ab8 the tables take many 1024, 2048, 4096: 8.91M, 17.8M, 35.7M; wide
  1024 6.55M. Every ladder cell fits except many 4096, the one secondary-curve cell (coordinator,
  2026-10-30; `design/revision-log.md`). Code generation, not evaluation, dominates compiles.
- Desktop toolchains (functional builds): `D:\Dev\toolchains\{gcc-16.2.0-linux, protobuf-36.2-linux,
  protobuf-36.2-linux-asan, nghttp2-1.70.0-linux}`, mounted under `/home/dev/opt`; and
  `%USERPROFILE%\opt\{protobuf-36.2-mingw, nghttp2-1.70.0-mingw, protobuf-36.2-msvc-asan,
  nghttp2-1.70.0-msvc-asan}`. `bench/compile_curve.py` builds and checks one R or P cell, on Linux
  and on Windows (MinGW; each phase in a job object: `--cpu`, `--timeout`, `--self-test`).

## Build and test (desktop, functional only; no timing)

- Presets in `CMakePresets.json`, build trees under `build/<preset>` (ignored).
- Linux: `docker run --rm --cpus 4 --user dev -v D:\Dev\GitHub\Papers:/d/Dev/GitHub/Papers` and
  each of the four toolchains above `-v D:\Dev\toolchains\<name>-linux[-asan]:/home/dev/opt/<name>[-asan]:ro`,
  `-w <repo> papers-dev:arch-2026-10-23 bash tools/dev_linux_all.sh <presets>` (`MSYS_NO_PATHCONV=1`).
- Windows: from a `vcvars64.bat` shell, `cmake --preset msvc-debug`, build, ctest.
- Results (pass or fail only), with interop: `linux-gcc1620-{e2e,e2e-release}` 25 of 25,
  `linux-gcc1620-e2e-tsan` 24 (`allocs` skipped under both sanitizers), `mingw-gcc1620-e2e-release`
  24 of 24, `linux-clang-{asan,tsan}` 8 (`reflection_layer` skipped), `fuzz_decode` 300,000
  mutations under ASan+UBSan; `msvc-e2e-asan` 9 of 9 at 60eff0e (transport only).
- Tests: golden wire vectors (`tests/golden/README.md`) from protoc 3.21.12, regenerated with the
  pinned protoc 36.2 on 2026-10-31: identical. The encoder matches them byte for byte.
- Sanitized server under h2load (count mode, every arm and body): ASan one worker, TSan two, 0 failed,
  no report. TSan in docker needs `--security-opt seccomp=unconfined`; `protobuf-36.2-linux-tsan`.

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
- [x] Fresh reviews 1 to 3 (2026-10-23 to 30; the third 6a0d8c9, verified): every CRITICAL, HIGH and
      MEDIUM fixed. Open LOW: option value types, `1abc` one floating token, no decode allocation cap
      beyond the 4 MiB frame, run-time hash probing with crafted collisions, reserves kept in the file.
- [x] Fresh reviews 4 and 5 (Opus, 23dfab3..88e5b4c, 2026-10-30, 31): no CRITICAL or HIGH; all acted on or
      answered by Alex (revision log; review 4's M1 MinGW emulated TLS, M2 Windows record transport only).
- [x] Sixth fresh review (Opus, 88e5b4c..ce2413c, 2026-11-01): no CRITICAL or HIGH; 2 MEDIUM (gate), 14 LOW,
      fixed or accepted (revision log). Outside it: compile builds lacked `-DNDEBUG`, no compile pilot Release.
- [x] Freeze proposal answered by Alex (2026-10-31, decision 11). `hypotheses.md` FROZEN 2026-11-01 (704061a); m 36 and 7.
- [ ] Next: request 16 (D5 records) at CODE_FREEZE 9a3dc92; then the campaigns' requests, at CODE_FREEZE.

## Laptop requests

Since 2026-10-26 (Alex, AGENTS.md bc1a76d) a ready request goes straight to the coordinator's
queue; design, scope and pushes still go to Alex.

1. and 4. Done 2026-10-24 (`gap1-a-q4-*`: F1 at -O1, -O2) and 2026-10-29 (A19: `~/opt/gcc-16.2.0` on Arch).
2. and 3. Done (A23 at c525595, 4b01a015...cc82: within 0.3%; A31 at fce6a17, 30ed7123...619e: knee c12 m128).
5. and 6. Done 2026-10-30: A27 at 4d205b4 (`gap48-...` 22574257...4710), A28 at 614c9f4 (b90f1c76...e749).
7. Done 2026-10-30 (A29 at fa60375, 345bea80...bf09): before reuse. R/P wide 0.86, 0.90, 0.77; many 1.00, 1.20, 1.56.
8. and 9. Done 2026-10-30 (A33 at 16fdbd5, A35 at 3537817). Pilots. libc 22.5% of R's medium cycles; then Q beat R.
10. Done 2026-10-30 (A38 step 1 at 30818a5, `gap60-a-q1-*` 5f817497...acc6): 51/51. R/P many 0.33, 1.24,
    1.52, 1.92, wide 0.30, 0.89, 0.90, 0.74. Reuse grew instantiation and code generation.
11. Done 2026-10-30 (A38b at 30818a5, `e2e-pilot-4.md`, `gap60-a-q2/q3-*`). Pilot. 264 windows; no order
    effect. Q beat R on medium from c12 m8 (0.94 to 0.85). Medium cycles:u per request R 8,902, Q 7,751.
12. Done 2026-10-30 (A39 at 15aaa6a, `compile-handlers.md`). Pilot. R many 1024 to 4096 -4.9, -13.1, -24.3%.
13. Done 2026-10-31 (A40 at db1f31c, `compile-rerun-5.md`; `gap62-a-q1-p5-rerun5-96943287` 3ce6458f...e246).
    Pilot. 51/51; P within 0.8% of A38. R many against A29 +14.4, +7.9, -6.8%; R wide against A38 +0.9 to
    +1.6% (a loss). R/P many 0.32, 1.14, 1.29, 1.45; wide 0.31, 0.90, 0.92, 0.76. RSS R many 4096 4,059 MiB.
14. Done 2026-10-31 (A40 at db1f31c, `e2e-pilot-5.md`; `gap62-a-q2-p5-e2e5-build-7c02aacf` b380de1a...7d8d,
    `gap62-a-q3-p5-e2e5-run-ccf9cf23` b566c31e...7744). Pilot. ctest 24 of 24; R medium 0 allocations; 264
    windows, checks pass; second runs within 1.5%. Every R window above every Q window in 12 of 14 cells:
    small 1.014 to 1.046 from c12 m1, medium 1.023 to 1.041; ties at small c1 m1 (all arms) and medium c48
    m128 (1.016, overlap). R/P small 1.03 to 1.09, medium 1.06 to 1.25. Against pilot 4: S, P, Q within 2%,
    R medium +3.6 to +19%. Medium cycles:u per request R 7,659, Q 7,820; libc R 455. Clearing kept
    storage (`clear_within_slack`, `truncate`) is 3.5% of R's medium cycles. Analysis: `bench/pilot/`.
15. Done 2026-10-31 (A43b at 40e9211; build 3c5e775c...1164, run 6103414c...1239; `pilot_e2e_windows.py`).
    Pilot. ctest 23/23, 264 windows, checks pass. c12 m128 bounded by ghz (97% of 12 CPUs; server 24-62%):
    arms within 1%. c1 m1 medium R/P 1.021 separated, R/Q 1.008. c12 m1 (server 98%): medium P 45.0k > S
    42.5k > R 38.8k > Q 38.1k, cause unknown (a Windows transport regime). ghz latency quantized (~0.5 ms).
16. D5 records (`sanitizer-records.md`). Dry run 1 2026-10-31 (A45 q2 at 701fe83, 57339cf8...9edd): clang ASan, TSan red (19).
    Dry run 2 2026-11-01 (A48 q2 at 230315e, af17ce75...e4d0): all five green, four-target inputs with lists, gate pass.
17. and 18. Done (A46 W, a6a2bde7...ebca; A45 q1 L, e15c8abc...bb3c; at 1b93a97). Without `-DNDEBUG`, so they fix
    no direction (superseded by 20 and 21). All cells separated; many 512 was R faster then (0.954 L, 0.969 W).
19. Done 2026-11-01 (A48 q1, c8881c94...89e8). Plain libc++ 22.1.8 at `~/opt/libcxx-22.1.8` (`libcxx-22.1.8.md`, 67aad2ef...4007):
    manifest check clean, glibc 2.44, MSan prefixes untouched; the clang ASan, TSan presets link it.
20. and 21. Done 2026-11-01 (A50 at 9a3dc92, tools a24f257; L 4fd1df21...51bd, W 7e18a113...da37). Release flags. 222
    builds valid, gates pass, self-test ok. All 36 cells separated: wide R faster (L 0.314 to 0.844, W 0.322 to 0.835); many
    R faster 16 to 256, slower 512 to 4096 (512 flipped: 1.015 L, 1.014 W). 3,523 s, 3,839 s; 10 replicates L 3.3 h, W 3.6 h.

## Lab requests

Specs for 1 to 3 (exact versions, URLs, published sha256): `bench/pins.md` (2026-10-29).

1. Done 2026-10-29: protobuf 36.2, abseil 20250512.1 (`C:\Users\alext\opt\_dl\p5-protobuf-36.2`). 4. Done 2026-10-24: MinGW zip.
2. Done 2026-10-30 (Alex: "Download all (Recommended)"): Go 1.27.2, ghz v0.121.0, `go.sum`. 3. Done: nghttp2 1.70.0, libev 4.33.

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

6. Gap block approved: Lab request 4 and Laptop request 1 (both done). 8. Go 1.27.2, as P4 and P7.
7. No `pacman` upgrade or install on the laptop's Arch until P4's Arch blocks end; a differing gcc
   means a side-by-side build under `~/opt`. 9. P4's idle day carries only read-only jobs.
10. GCC version (2026-10-29, relayed by the coordinator; question "Declare the gap, or pin 16.2.0
    on both?"): "Pin 16.2.0 on both". Linux build request: `bench/requests/gcc-16.2.0.md`.
11. Freeze proposal (2026-10-31, relayed; verbatim in `hypotheses.md` section 6), recommended each:
    Q1 freeze R, P, Q now (G later); Q2 small c1 m1 no direction; Q3 many claim; Q4 port and pilot.
    Then (a) to (e): "Accept all (Recommended)"; Windows latency, mean call cycle: "Accept (Recommended)".
12. D5 gate of the compile-curve builds (2026-10-31, relayed): "Adopt (Recommended)". `compile_curve.py` records
    `first_party_inputs` (g++ -MM, untimed); `bench/check_compile_inputs.py` matches a green GCC record's `--list`.

Open: none. Report drafts: `design/gcc-report-drafts.md` (F2 probably GCC PR 71962; Alex checks duplicates by hand).

## Never

No timing on the desktop, of any kind, anywhere (not even ctest seconds in notes). No laptop access.
No push, no remote. Never edit ctpb, frozen files, records or `writing/`. Never name the integration
library (the name guard test stores digests only). No em or en dashes.
