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
  libFuzzer executables (desktop: 10 minutes each, no failure).
- Decoding dispatches through a field-number table and per-field functions (613d6fb; it was a
  comparison with every field, quadratic). Unmeasured: nested sizes are recomputed while writing.
- Constant-evaluation budget (the main design risk). GCC 16's default `-fconstexpr-ops-limit`
  is 33,554,432 operations per outermost evaluation; GCC counts every expression node. A variable
  template first used inside an evaluation shares its counter; forced earlier (a static_assert),
  it gets its own (define_messages forces the tables so). `bench/constexpr_ops.sh` bisects the
  limit (deterministic, not timing; MAX raises its ceiling). At d7d7ab8 (GCC 16.2.0) the tables:
  many 1024, 2048, 4096: 8.91M, 17.8M, 35.7M (linear); wide 1024 6.55M. From 30.0M and 16.8M at
  1024 the day before: one symbol table, fixed buffers instead of push_back (75 against 500
  operations), one static string for all names, a token cursor. Ladder under default limits:
  every cell except many 4096, which is the one secondary-curve cell (coordinator, 2026-10-30;
  `design/revision-log.md`). The micro-cuts stopped there: code generation dominates compiles.
- Dev-image GCC 16.2.0 (Laptop request 4, desktop part, done 2026-10-29): the checked tarball,
  the request's configure line, `-j4`, installed into `D:\Dev\toolchains\gcc-16.2.0-linux`, which
  containers mount at `/home/dev/opt/gcc-16.2.0` (the image is unchanged). `g++` 16.2.0; the probe
  prints the expected line; F1 as in MinGW 16.2.0; 14 of 14 pass at 20777ca (Release, rpath to
  its `lib64`, so the tests load its libstdc++).
- Protoc arm, dev image (2026-10-29): `bench/build_protobuf.sh` (pinned abseil and protobuf, GCC
  16.2.0, C++26 `-O2`, static) into `D:\Dev\toolchains\protobuf-36.2-linux`; `bench/compile_curve.py`
  builds one G_R or G_P cell and checks it (functional on the desktop with `--no-time`: R at 1024,
  P at 4096, both shapes, pass). Presets `linux-gcc1620-{release,debug,asan}`: 14 of 14 each.

## Build and test (desktop, functional only; no timing)

- Presets in `CMakePresets.json`, build trees under `build/<preset>` (ignored).
- Linux: `docker run --rm --cpus 4 --user dev -v D:\Dev\GitHub\Papers:/d/Dev/GitHub/Papers -v`
  `D:\Dev\toolchains\gcc-16.2.0-linux:/home/dev/opt/gcc-16.2.0:ro -w <repo> papers-dev:arch-2026-10-23`
  `bash tools/dev_linux_all.sh <presets>` (Git Bash: `MSYS_NO_PATHCONV=1`).
- Windows: from a `vcvars64.bat` shell, `cmake --preset msvc-debug`, build, ctest.
- Results at c1dd0fb (pass or fail only): `linux-gcc1620-{release,asan}` 17 of 17 (5 unit, 7
  compile-fail, 3 fuzz smoke, name guard and canary); `linux-clang-{asan,fuzz}`, `msvc-{debug,asan}`
  8 of 8 portable, `reflection_layer` Skipped with the reason. MinGW: 14 of 14 on 2026-10-23.
- Tests: golden wire vectors from `protoc` (`tests/golden/README.md`: protoc 3.21.12 from the
  local `ctpb-dev` image, a development oracle only); the encoder output equals them byte for
  byte for all 15 scalar types, enums, nested and repeated fields; `#embed` reads the schemas.
- TSan: not run (no threads yet). It needs `--security-opt seccomp=unconfined` in the container.

## Toolchain facts (desktop, 2026-10-23)

- GCC 16 (`papers-dev` 16.2.1, pinned 16.2.0): P2996 with `-freflection` (202603L), `#embed`
  (202502L). It refuses `^^std::int32_t`; reflect such types through a template.
- clang 22.1.8 rejects `-freflection`. MSVC 19.51.36246 has no P2996 and no `#embed`.
- CMake 4.3.2 (Windows) has no C++26 flag for MSVC: the MSVC build uses C++23.
- MinGW-w64 GCC 16 on the desktop (Alex decides 1, probe passed 2026-10-23):
  `winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip` from github.com/brechtsanders/
  winlibs_mingw release `16.2.0posix-23.1.3-14.0.0-ucrt-r3` (2026-10-18), 276,414,382 bytes,
  sha256 3c69305081e4a647144339a936244fcf7d3d9e558319e1ad7be2cd2886d4b6a1, equal to the release
  asset's published digest. Zip kept in `C:\Users\alext\opt\_dl\p5-winlibs-gcc16-r3`, unpacked
  with `tar.exe` to `C:\Users\alext\opt\winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3` (not on PATH).
  `g++` 16.2.0 (r3), GNU ld 2.47.20260726: `__cpp_impl_reflection` 202603L, `#embed`, constexpr
  exceptions. F1 reproduces at `-O2` (so it is in the 16.2.0 release too); F2 does not reproduce
  on this target. No ASan or UBSan on MinGW GCC.
- GCC 16.2.0 on every host since 2026-10-29 (Alex decides 10; Laptop request 4; dev image above).
- Compiler findings (details in `design/compiler-findings.md`):
  - F1: GCC 16.2.1 miscompiles `string_view::find` from an offset when a constexpr function
    receives a string literal. The portable layer avoids `find` in constexpr code.
  - F2: GCC 16.2.1 UBSan checks `null`, `nonnull-attribute`, `returns-nonnull-attribute` break
    constant evaluation of the reflection layer; the GCC sanitizer preset drops them.
  - F3: clang 22 with libstdc++ 16 cannot build `std::string` in constant evaluation; clang
    presets use libc++.
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
- [ ] Next: the Windows transport under MinGW; per-message compile cost in many (R loses there);
      interop with grpc-go; D5 records with the instrumented protobuf (Laptop request).

## Laptop requests

Since 2026-10-26 (Alex, AGENTS.md bc1a76d) a ready request goes straight to the coordinator's
queue; design, scope and pushes still go to Alex.

1. Done 2026-10-24 on Arch (archive `gap1-a-q4-visit-a1-c0eb9960`, sha256 files match): Arch's
   gcc is the image's exact package; F1 reproduced at `-O1` and `-O2`; the probe printed its line.
2. Done 2026-10-29 (A23, pilot, not a result; archive `gap44-a-q1-p5-pilot-c2670a4d`, sha256
   4b01a015...cc82; at c525595). Medians: R 2.3/2.5 s at 16 (wide/many), 28.3/34.8 s at 1024;
   P 7.7/7.8 s, 26.8/33.2 s, 167/116 s at 4096. Replicates within 0.3%. At 1024, R spends 62 to
   78% in code generation, 11/21% in constant evaluation. Protobuf build on the laptop 91 s.
3. Not ready (needs the transport and the arms). End-to-end pilot: the length of a gRPC window and
   the saturating load, per arm. Arch first. No measured basis for gRPC windows; the lab's
   closed-loop windows measured 6.116 s (Arch) and 6.123 s (Windows). Plain: run a short gRPC
   load test against each server variant, to learn how long one window takes and which load
   saturates the server.
4. Done 2026-10-29 (A19, archive `gap40-a-q1-p5-gcc-877d16d0`, sha256 17fac736...0beb):
   `~/opt/gcc-16.2.0` on Arch; 14 of 14, F1 as on the desktop; make -j14 2,611 s.
5. Done 2026-10-31 (A27 at 4d205b4, archive `gap48-a-q1-p5-rerun-0763609d`, sha256 22574257...4710).
6. Done 2026-10-31 (A28 at 614c9f4, archive `gap49-a-q1-p5-rerun2-2228616a`, sha256 b90f1c76...e749).
   Pilot, not a result. R compile medians A27 to A28 (3 replicates each): wide 1024 26.43 to 22.81 s,
   2048 75.62 to 53.58 s, 4096 246.90 to 126.62 s (code generation 229 to 106 s in the time
   report); many 1024 32.84 to 33.94 s, 2048 71.68 to 73.88 s (+3%, ranges apart); 16 fields equal.
   many 4096 at 67108864: 185.48 s, 5,520 MiB. A28 also holds 2542f6f, d7d7ab8, 6a0d8c9. Against
   P (other runs): R wins wide at every size, loses many at 2048 (73.9 against 59.6 s) and 4096.

## Lab requests

Specs for 1 to 3 (exact versions, URLs, published sha256): `bench/pins.md` (2026-10-29).

1. Done 2026-10-29 on the desktop: protobuf 36.2 (protoc Linux and Windows, runtime) and abseil
   20250512.1 in `C:\Users\alext\opt\_dl\p5-protobuf-36.2`, sha256 equal to `bench/pins.md`.
2. Pin Go 1.27.2 (Alex, D2: the same as P4 and P7) and grpc-go for the interop programs (go.dev publishes sha256 for Go archives; grpc-go
   through `go.sum`). Go is installed on the desktop (`C:\Program Files\Go`), version not recorded.
3. Pin nghttp2 (source) for the transport and h2load; the image has `libnghttp2` 1.70.0 only.
4. Done 2026-10-24 (job `p5-mingw-w`, exit 0, per the coordinator): the MinGW-w64 GCC 16.2.0 zip
   copied to the laptop's Windows, sha256 checked, unpacked to the desktop's path.

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
