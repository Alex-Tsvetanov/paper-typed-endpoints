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
- Not yet written: HTTP/2 transport (libnghttp2), the `protoc` arm, the benchmark server with
  runtime arm selection, grpc-go interop programs, libFuzzer targets for the schema parser and
  the decoder (the brief requires them; `linux-clang-asan` can build them for the portable
  layers), and the related-work survey (the brief puts it first; Sonnet gathers, Opus re-checks
  every claim). `bench/gen_schema.py` and a draft `bench/coverage.json` exist.
- Known performance items, unmeasured: nested message sizes are recomputed while writing (no
  size cache); decode dispatch is a linear chain over fields. Both wait for laptop probes.
- Constant-evaluation budget (the main design risk). GCC 16's default `-fconstexpr-ops-limit`
  is 33,554,432 operations per outermost evaluation; GCC counts every expression node. A variable
  template first used inside an evaluation shares its counter; forced earlier (a static_assert),
  it gets its own. `bench/constexpr_ops.sh` bisects the limit (deterministic, not timing).
  At d0545b3 (GCC 16.2.0), tables evaluation: many 1024 9.47M (9.2k per field; tokenize 2.8M,
  parse 3.4M, checks 2.2M, tables 1.0M), wide 1024 about 7.0M; define_messages adds 0.38M.
  From 30.0M and 16.8M this morning: one symbol table, fixed buffers instead of push_back
  (micro-benchmark: 75 against 500 operations), one static string for all names.
  Ladder under default limits (pass/fail): wide passes 4096; many passes 2048, fails 4096 (about
  37.9M projected). Options: about 13% more cuts, or three separately evaluated stages.
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
- Results on 2026-10-23 (pass or fail only):
  - `linux-gcc-debug`, `linux-gcc-release`, `linux-gcc-asan`: 14 of 14 pass (schema, wire, grpc,
    reflect, endpoint, 7 compile-fail tests, name guard and its canary).
  - `linux-clang-debug`, `linux-clang-asan` (libc++, full UBSan): portable tests pass;
    `reflection_layer` is reported Skipped with the reason.
  - `msvc-debug`, `msvc-release`, `msvc-asan`: portable tests pass; `reflection_layer` Skipped.
  - `mingw-gcc-debug`, `mingw-gcc-release` (MinGW-w64 GCC 16.2.0 first on PATH): 14 of 14 pass.
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
- Local image `ctpb-dev:latest` (bfa32b62a446): Bloomberg clang-p2996 at 2ea0a79fe and Ubuntu's
  protoc 3.21.12. P5 builds nothing in it; its protoc only made the development golden vectors.

## Status

- [x] Repository, STATE.md, design note draft, compiler findings.
- [x] Skeleton building; tests pass on Linux, MSVC (portable layers) and MinGW GCC 16 (all).
- [x] Code review by a fresh agent (Opus, cpp-reviewer, 2026-10-23): 1 CRITICAL, 1 HIGH, 4
      MEDIUM, 5 LOW groups. All CRITICAL, HIGH and MEDIUM fixed, and most LOW (commit 8764052).
      Left open: value-type checks of option values (`[deprecated = "yes"]` is accepted), `1abc`
      lexed as one floating token, no per-request decode allocation cap beyond the 4 MiB frame
      limit (memory amplification from repeated empty messages is bounded by sizeof(M) per two
      input bytes, as in libprotobuf).
- [x] Second fresh review (Opus) of the optimization diff 8764052..de304a9: no CRITICAL or HIGH;
      the MEDIUM (a backslash before a newline in a string lost the line count; now refused, as
      protoc does) and two LOW (hash-flooding fallback to a sort, capped token reservation) are
      fixed in the next commit.
- [ ] Next: per-message and per-rpc constexpr costs (in progress), `protoc` arm, transport, interop.

## Laptop requests

Since 2026-10-26 (Alex, AGENTS.md bc1a76d) a ready request goes straight to the coordinator's
queue; design, scope and pushes still go to Alex.

1. Done 2026-10-24 on the laptop's Arch: jobs `a1-p5-f1` and `a1-p5-define-aggregate`, both exit
   0 (archive `C:/Users/alext/lab/laptop-archive/gap1-a-q4-visit-a1-c0eb9960/`, sha256 files
   match). Arch's gcc is `16.2.1+r23+gd564253eb6c8-1`, the image's exact package, so the operation
   counts, the ladder ceilings and F1 transfer to the laptop. F1 reproduced: correct at `-O0`,
   "f literal=3" and "g literal=3" at `-O1` and `-O2`. The probe
   (`probes/define_aggregate_probe.cpp` at c9d22bf) printed the expected line.
2. Ready 2026-10-29: `bench/requests/compile-pilot.md`. Compile-curve pilot on Arch: protobuf built
   there first, then 30 timed builds (16 and 1024 fields, both shapes and generators, plus protoc
   at 4096; 3 replicates) and 8 `-ftime-report` builds. No measured basis for its length. Plain:
   build the smallest and largest test schemas with both generators a few times, to learn how
   long one build takes and where the compiler spends it.
3. Not ready (needs the transport and the arms). End-to-end pilot: the length of a gRPC window and
   the saturating load, per arm. Arch first. No measured basis for gRPC windows; the lab's
   closed-loop windows measured 6.116 s (Arch) and 6.123 s (Windows). Plain: run a short gRPC
   load test against each server variant, to learn how long one window takes and which load
   saturates the server.
4. Done 2026-10-29 (chain A19, job `p5-gcc-16-2-0-l`; archive `C:/Users/alext/lab/laptop-archive/
   gap40-a-q1-p5-gcc-877d16d0/`, sha256 17fac736...0beb checked): `~/opt/gcc-16.2.0` on Arch, per
   `bench/requests/gcc-16.2.0.md`. g++ 16.2.0, probe line, 14 of 14, F1 as on the desktop. Make
   -j14 2,611 s, install 15 s (the basis for a rebuild). Exit 1 was a script grep, fixed (Papers 6ec4ae3).

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
