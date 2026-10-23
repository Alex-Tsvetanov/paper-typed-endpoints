# P5 typed endpoints: state digest

Read this first. Keep it under 200 lines. Brief: `Papers/p5-typed-endpoints/AGENTS.md`.
Plan: `Papers/plan/merged-plan-v2.md` section 2.5 and P5's row in section 3.2.
Design: `design/design-note.md` (DRAFT, not frozen). Compiler defects: `design/compiler-findings.md`.

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
- Supported proto3: scalars, enums (as `int32_t`), nested messages (`std::optional<M>`), repeated
  (packed by default, `[packed]` honored), reserved, options without wire meaning, services with
  unary rpcs. Refused with a diagnostic: map, oneof, `optional`, `required`, groups, extensions,
  import, editions, streaming, recursive messages, `[default]`, `message_set_wire_format`.
- Not yet written: HTTP/2 transport (libnghttp2), the `protoc` arm, the benchmark server with
  runtime arm selection, `bench/gen_schema.py`, grpc-go interop programs, `bench/coverage.json`.
- Known performance items, unmeasured: nested message sizes are recomputed while writing (no
  size cache); decode dispatch is a linear chain over fields. Both wait for laptop probes.
- Constant-evaluation budget (the main design risk). GCC 16's default `-fconstexpr-ops-limit`
  is 33,554,432 operations per evaluation, and the schema tables are one evaluation.
  `bench/constexpr_ops.sh` bisects the limit to count a stage's operations (deterministic, not
  timing). Whole pipeline (parse, tables, define) per field, `wide 256`: 72k at e1907b9, 52k
  after the review fixes, 22k after two optimization passes (pointer lexer, `sv` keywords, one
  names string, O(n) duplicate checks). `many 256`: 36k per field; each message plus rpc costs
  about 80k more (parser and checker), the next target.
  Desktop pass/fail ladder under default limits (`-O2`, no timing): `wide` passes 1024 and fails
  2048 (was 256 / 512); `many` passes 512 and fails 1024 (just over: about 4 x 9.2M).
  Target: the 4096-field ladder under default limits, or a declared secondary curve with raised
  limits above the default ceiling (design note section 4).

## Build and test (desktop, functional only; no timing)

- Presets in `CMakePresets.json`, build trees under `build/<preset>` (ignored).
- Linux (`papers-dev:arch-2026-10-23`, `--cpus 4`, `-j4`): `tools/dev_linux_all.sh <presets>`.
  Run line: `docker run --rm --cpus 4 --user dev -v D:\Dev\GitHub\Papers:/d/Dev/GitHub/Papers`
  `-w /d/Dev/GitHub/Papers/p5-typed-endpoints/typed-endpoints papers-dev:arch-2026-10-23`
  `bash tools/dev_linux_all.sh linux-gcc-debug` (Git Bash: set `MSYS_NO_PATHCONV=1`).
- Windows: from a `vcvars64.bat` shell, `cmake --preset msvc-debug`, build, ctest.
- Results on 2026-10-23 (pass or fail only):
  - `linux-gcc-debug`, `linux-gcc-release`, `linux-gcc-asan`: 12 of 12 pass (schema, wire, grpc,
    reflect, endpoint, 5 compile-fail tests, name guard and its canary).
  - `linux-clang-debug`, `linux-clang-asan` (libc++, full UBSan): portable tests pass;
    `reflection_layer` is reported Skipped with the reason.
  - `msvc-debug`, `msvc-release`, `msvc-asan`: portable tests pass; `reflection_layer` Skipped.
- Tests: golden wire vectors from `protoc` (`tests/golden/README.md`: protoc 3.21.12 from the
  local `ctpb-dev` image, a development oracle only); the encoder output equals them byte for
  byte for all 15 scalar types, enums, nested and repeated fields; `#embed` reads the schemas.
- TSan: not run (no threads yet). It needs `--security-opt seccomp=unconfined` in the container.

## Toolchain facts (desktop, 2026-10-23)

- GCC 16.2.1 20260810 (`gcc 16.2.1+r23+gd564253eb6c8-1`) in `papers-dev` implements P2996 with
  `-freflection` (`__cpp_impl_reflection` 202603L) and `#embed` (`__cpp_pp_embed` 202502L). It is a
  released compiler. GCC 16 refuses `^^std::int32_t`; reflect such types through a template.
- clang 22.1.8 rejects `-freflection`. MSVC 19.51.36246 has no P2996 and no `#embed`.
- CMake 4.3.2 (Windows) has no C++26 flag for MSVC: the MSVC build uses C++23.
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
- [x] Skeleton building; functional tests pass on Linux (container) and Windows (MSVC, portable
      layers; the reflection layer waits for Alex decides 1).
- [x] Code review by a fresh agent (Opus, cpp-reviewer, 2026-10-23): 1 CRITICAL, 1 HIGH, 4
      MEDIUM, 5 LOW groups. All CRITICAL, HIGH and MEDIUM fixed, and most LOW (commit 8764052).
      Left open: value-type checks of option values (`[deprecated = "yes"]` is accepted), `1abc`
      lexed as one floating token, no per-request decode allocation cap beyond the 4 MiB frame
      limit (memory amplification from repeated empty messages is bounded by sizeof(M) per two
      input bytes, as in libprotobuf).
- [ ] Next engineering: transport, `protoc` arm, schema generator, desktop pass/fail probe of the
      compile ladder under default GCC limits, grpc-go interop.

## Laptop requests

None run. The coordinator batches these and asks Alex for the hours.

1. Ready now. Read the GCC version on `lap-linux` (`g++ --version`, `pacman -Q gcc`) and compile
   one `define_aggregate` probe with `-std=c++26 -freflection`; also run F1's reproduction.
   Arch. Under 5 minutes. Basis: two commands and two compiles of a few lines.
2. Not ready (needs the schema generator, the pinned protobuf and the G_P build). Compile-curve
   pilot: the smallest and largest size of each shape and generator, 3 builds each, 24 builds.
   Arch. No measured basis; ctpb's laptop retake (another compiler and implementation) took 2 to
   9 s per build at 5 to 100 fields, which bounds nothing at 4096 fields.
3. Not ready (needs the transport and the arms). End-to-end pilot: window length of a gRPC
   window and the saturating load, per arm. Arch first. No measured basis for gRPC windows; the
   lab's closed-loop windows measured 6.116 s (Arch) and 6.123 s (Windows).

## Lab requests

1. Pin one protobuf release for P5: the `protoc` binary (Linux and Windows) and the runtime source
   at the same version, downloaded into a fresh folder and checked against the release asset's
   published sha256. Needed for the `protoc` arm and to regenerate the golden vectors.
2. Pin Go and grpc-go for the interop programs (go.dev publishes sha256 for Go archives; grpc-go
   through `go.sum`). Go is installed on the desktop (`C:\Program Files\Go`), version not recorded.
3. Pin nghttp2 (source) for the transport and h2load; the image has `libnghttp2` 1.70.0 only.

## Alex decides

1. Windows coverage for the reflection layer. MSVC has no P2996. (a) MinGW-w64 GCC 16 on the
   laptop's Windows for both generators, so the compiler is the same on both OSes; (b) a declared
   coverage gap for Windows compile curves, the Windows runtime comparison built by (a) or dropped.
2. D5 sanitizers for the reflection layer: no clang tool parses it; GCC gives ASan, TSan and UBSan
   without three null checks (F2); no MSan. Proposal: GCC records plus declared gaps in
   `bench/coverage.json`. Needed before any number is citable.
3. Keep the gRPC C++ reference arm G (largest engineering item), or narrow the end-to-end claim
   to reflection against `protoc` messages on the same transport (design note D-G).
4. Load generator: h2load or ghz (design note D-L).
5. Whether to report F1 (and F2) to GCC upstream. Outbound, so only Alex sends it.

## Never

No timing on the desktop, of any kind, anywhere (not even ctest seconds in notes). No laptop access.
No push, no remote. Never edit ctpb, frozen files, records or `writing/`. Never name the integration
library (the name guard test stores digests only). No em or en dashes.
