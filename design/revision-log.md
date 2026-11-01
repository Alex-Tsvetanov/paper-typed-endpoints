# P5 revision log

Changes to the design after a decision, newest last. Each entry names who decided, what changed,
and the measurement it rests on. Frozen files are never edited; their changes are entries here.

## 2026-10-30. `many` at 4096 fields is a secondary-curve cell

Decided by the coordinator (2026-10-30, under design note section 4, which allows a declared
secondary curve; Alex was not asked).

- The reflection generator compiles `many` 4096 only with a raised constant-evaluation limit.
  Its schema tables need 35.7M operations (`bench/constexpr_ops.sh many 4096 tables`, GCC 16.2.0,
  commit d7d7ab8, deterministic count, not a timing); GCC's default limit is 33,554,432. The count
  is linear in the field count: 8.91M at 1024, 17.8M at 2048.
- The cell is built with `-fconstexpr-ops-limit=67108864` (twice the default) and reported on the
  secondary curve, with that limit and the measured count stated in the paper.
- Every other cell of the ladder (both shapes from 16 to 2048 fields, `wide` at 4096) compiles
  under the default limits and is on the primary curve.
- Not done: moving tokenization into a separately evaluated stage, which would bring the cell
  under the default limit (about 25M) without shortening the compile.

## 2026-10-30. Four decisions for the end-to-end benchmark

Decided by Alex (AskUserQuestion through the coordinator, 2026-10-30). Each item gives the
question as sent and his pick verbatim.

- Q1, load generator on Windows. nghttp2's README (v1.70.0, lines 260 to 270) says a MinGW build
  compiles the library only; h2load needs Cygwin with libev. Pick: "h2load + ghz (Recommended)",
  the option "h2load on Linux and ghz (native Go) on Windows. The same generator serves all arms on
  each OS, and the comparisons are per OS. The pilot checks that ghz saturates the server." This
  replaces, for Windows, decision 4 of 2026-10-23 in STATE.md (h2load drives every timed run).
  Not taken: h2load under Cygwin; a generator of our own.
- Q2, arm P's configuration. Pick: "Both at their best (Recommended)", the option "P in protobuf's
  documented best configuration: one Arena per request with a reused initial block, and messages
  created on it. R in its own best configuration, which I engineer per D2: reused buffers, and no
  per-request heap allocation where possible." Not taken: both arms without an arena.
- Q3, Windows toolchain. Pick: "Try MinGW, else gap (Recommended)", the option "Try abseil and
  protobuf on MinGW GCC 16 first; if they fail, Windows R against P becomes a declared gap. G (gRPC
  C++) is built with MSVC on Windows, its documented best configuration; G is a reference arm, not
  same-compiler." Not taken: Linux only. Stopping rule (this session): if a pinned source needs a
  patch to build on MinGW, that is not its documented configuration, so it triggers the gap.
- Q4, download. Pick: "Download (Recommended)" for nghttp2-1.70.0.tar.xz from the GitHub release,
  checked against `bench/pins.md` before unpacking: 2,018,996 bytes, sha256 equal (2026-10-30).

## 2026-10-30. Three decisions after the fourth review

Decided by Alex (AskUserQuestion through the coordinator, 2026-10-30); his picks verbatim.

- M1, emulated thread-local storage on Windows. With winlibs MinGW-w64 GCC 16.2.0, `thread_local`
  compiles to calls of `__emutls_get_address`; protobuf's Arena reads a thread-local cache on every
  arena set-up and allocation, so arm P pays this on Windows and arm R (no `thread_local`) does not.
  Pick: "Keep MinGW, disclose (Recommended)". The paper states the caveat; Windows claims compare R
  and P within this toolchain only, and the headline R/P claims rest on Linux.
- M2, the Windows sanitizer record (D5). Under MSVC ASan every program that links protoc's
  generated code stops before main (a null call in protobuf's table-driven parser while the
  descriptor registers; protobuf's port_def.inc says MSVC cannot guarantee constant initialization
  of those tables; `bench/coverage.json`). Pick: "Transport only, gap (Recommended)": an MSVC ASan
  record of the transport (net_windows.cpp, h2_server.cpp, main.cpp) with a stub echo arm; arm_p.cpp
  and requests.cpp on Windows are a declared gap, covered by the Linux ASan record of the same
  files. Not taken: clang-cl ASan (LLVM on the laptop), protobuf as a DLL under MSVC ASan.
- libev 4.33 download for h2load on Arch (`bench/pins.md`). Pick: "Download (Recommended)".
  Fetched 2026-10-30 into a fresh folder: 569,527 bytes, sha256 equal to the pin.

## 2026-10-30. Interop downloads

Decided by Alex (AskUserQuestion through the coordinator, 2026-10-30); his pick verbatim.

- The interop tools of design note section 6 (`bench/pins.md`, Lab request 2 and ghz): Go 1.27.2
  for Windows and Linux, ghz v0.121.0 for Windows and Linux, and the grpc-go modules through
  proxy.golang.org with GOSUMDB on. Pick: "Download all (Recommended)". Conditions: each file in a
  fresh folder, checked against its sha256 before use; the module cache size reported; `go.sum`
  committed. Fetched 2026-10-30 under `C:\Users\alext\opt\_dl\p5-{go-1.27.2,ghz-0.121.0}-{linux,windows}`:
  all four files have the pinned size and sha256. Unpacked to `%USERPROFILE%\opt\{go-1.27.2,
  ghz-0.121.0}` and `D:\Dev\toolchains\{go-1.27.2,ghz-0.121.0}-linux`.

Correction: entries and files written earlier today said 2026-10-31 for events of 2026-10-30
(commit dates, the laptop's job logs). The dates are corrected in place.

## 2026-10-30. How the interop check of section 6 runs

Decided by this session (no new question: it carries out decision 4 of 2026-10-23 and section 6).

- Section 6 asks for a grpc-go client that checks every response field, and a grpc-go server that
  answers tep-encoded requests. Decision 4 has ghz run the grpc-go interop check. ghz reports
  status codes, not reply fields, so the check runs both: two small grpc-go programs
  (`interop/go`) compare every field in each direction, and ghz calls both rpcs on arms R and P
  and must report every call OK. ghz still also serves the one cross-check of decision 4.
- Functional and untimed, so it runs on the desktop (`server/test_interop.cpp`, CTest `interop`
  with `-DTEP_INTEROP_BIN` and `-DTEP_GHZ`).

## 2026-10-30. Message reuse in R, and a second protobuf arm

- R's medium gap (A33's profile: libc 22.5% of R's cycles against 9.0% of P's) is engineered under
  Q2 as answered ("reused buffers, and no per-request heap allocation where possible"): the
  endpoint keeps one request and one response per rpc, decodes with `parse_from`, and resets the
  response with `clear` (decision notice to the coordinator; no new question).
- Decided by Alex (AskUserQuestion through the coordinator, 2026-10-30); his pick verbatim: "Add a
  P-with-reuse arm (Recommended)", the option "Keep P with an Arena per request, and add P with
  message reuse as a second protobuf arm, so R's reuse is compared with protobuf's own reuse.
  Same cost per window; closes the D2 fairness objection." Arm Q (`server/arm_p.cpp`) follows
  protobuf's C++ tutorial, Optimization Tips: "Reuse non-arena message objects when possible.
  Messages try to keep around any memory they allocate for reuse, even when they are cleared."
  The same tips advise deleting messages that grow too big; Q and R both drop their reused
  messages after a call larger than 64 KiB (wire size, since `SpaceUsed` walks the message by
  reflection on every call). Pilots from now on run S, R, P and Q.

## 2026-10-30. R's encoder writes back to front; R and Q in mirrored order

Decided by this session under D2 (no new question; decision notice to the coordinator).

- A35's profile showed R sizing each nested message twice: once for the whole message's size,
  once more for each length prefix while writing (`byte_size` of Item, 7.4% of R's cycles on
  medium). `encode` and `encode_framed` now write back to front (`detail::encode_back`), so a
  length is known when its prefix is written, and every message is sized once, as protobuf's
  cached sizes do. The forward writer (`encode_to`) stays as the reference; the tests and
  `fuzz_decode` require the same bytes and a write that begins exactly at the buffer's start.
- A35 ran each arm once per payload; R and Q swapped places only between payloads, and the later
  arm lost R against Q in both. The next pilot runs R and Q twice per payload in mirrored order
  (`bench/requests/e2e-pilot-4.md`).

## 2026-10-30. Handler lookup in one pass

Decided by this session under D2 (no new question; decision notice to the coordinator).

- A38 measured R's many-shape compiles 24 to 27% slower than A29, after message reuse, and
  constant evaluation growing about three times from many 1024 to many 2048, in A29 and A38 alike.
  Each rpc's handler was found by listing all of the handler type's member functions, once per
  rpc in the handler check and, since the reuse commit, once more per rpc in the endpoint.
  `detail::handlers` now lists them once, sorted by name, and finds each rpc's handler by binary
  search. The diagnostics keep their text; two negative tests now cover the private and the
  overloaded handler. Generated code is unchanged (same .text and GIMPLE at many 1024).
- Its compile effect is measured on the laptop (`bench/requests/compile-handlers.md`).

## 2026-10-30. Keep-storage types; the many shape reported as measured

Decided by Alex (AskUserQuestion through the coordinator, 2026-10-30); his picks verbatim:
- Medium gap: "Keep-storage types (Recommended)".
- Many shape: "Report as measured (Recommended)".

The question: pilot 4 (A38b) had Q ahead of R on the medium payload by 6 to 15% at the saturating
loads, and its profile put the whole gap in R's 27 heap allocations per call (the copy into the
cleared response rebuilt elements that std::vector and the box freed when cleared). And message
reuse had made R's many-shape compiles slower (A38, A39).

- Generated types, an API change: repeated message, string and bytes fields are
  `tep::repeated<T>`, a vector-like sequence that keeps the elements it loses, reset to their
  default, and reuses them, as protobuf's repeated fields keep cleared elements. `tep::box` keeps
  its storage when cleared and stays one pointer (presence is the pointer's low bit). Repeated
  scalar fields stay `std::vector<T>`.
- `parse_from` is `clear` plus merge, as protobuf's ParseFromArray is. The replace mode (a parse
  state per message, overwriting by arrival index, truncation at the end) is removed.
- Storage kept for reuse holds no data from an earlier message: every kept element and the storage
  of every absent nested message equals its default, at every depth (`tests/kept_storage.hpp`,
  checked in `test_reflect`, in the endpoint's handlers and in `fuzz_decode` after every
  `parse_from`). Memory: what a reused message holds is what this message and the one before it
  decoded into, within twice each container's size plus a little slack; each clear frees what the
  previous message left unused (kept elements, the storage of an absent nested message). A fresh
  review (Opus) of the first version found that kept elements could carry kept elements of their
  own through nesting, so old messages decided retained memory, and that `resize` broke two
  `std::vector` guarantees (a value aliasing an element; elements moved by a shrink); both were
  fixed before any measurement.
- R medium allocates nothing per call in a steady run of identical requests (`test_allocs`, a count,
  was 27), as Q. The many-shape compile cost is reported as measured, with reuse on.

## 2026-10-31. Pre-freeze review

Decided by this session (no new question; decision notice to the coordinator). A fresh review
(Opus) of 1e4b25d..88e5b4c, the code since the fourth review: no CRITICAL or HIGH, 2 MEDIUM, 5 LOW,
all acted on.

- M1: nothing checked the bytes ghz sends with `--binary-file`, as the Windows runs do (ghz reads
  such a file as one message or as count-prefixed messages, and an echoing arm cannot tell). The
  interop test now sends `small.pb` and `medium.pb` through ghz to an arm that answers OK only to
  the file's exact bytes: 200 of 200 calls each, with ghz v0.121.0 on Windows and on Linux. The
  Windows pilot's binary and bodies are unchanged.
- M2: out-of-memory tests through singular nested messages, nested repeated ones and the
  endpoint's reused slot (`test_growth`): each allocation fails once in turn; kept storage stays
  default and the next call returns the request whole.
- L1: the precondition of `box`'s `*` and `->` is documented: a release build returns the kept
  storage of an absent box.
- L2: reuse follows the previous call's shape. `tep::repeated` and `tep::box` keep only what the
  previous message used, and copy-assigning into the cleared response is a second shrink, so
  requests that alternate between more and fewer elements, or with and without a nested message,
  allocate on each swing back up; protobuf keeps every cleared object. The entry of 2026-10-30
  ("as protobuf's repeated fields keep cleared elements") holds for a steady run of identical
  requests only. `test_allocs` prints the alternating case (the medium Order and one with an item
  fewer and no customer): R 4, P 30, Q 0 allocations per call. The measured runs send one fixed
  body per window; the paper states this as a threat.
- L3: the transport maps an exception from an arm to RESOURCE_EXHAUSTED (`bad_alloc`) or UNKNOWN,
  as the endpoint does, and clears the response on any status but OK.
- L4: wording for the manuscript: kept storage holds no earlier value, but cleared strings and
  vectors still hold old bytes in their capacity; the 64 KiB reuse limit is on wire size, and
  empty elements decode to about sizeof(element) per 2 bytes.
- L5: move assignment of `box` and `repeated` takes the source before freeing its own storage
  (reachable only with a recursive user type; `test_reflect` covers it under ASan).

## 2026-10-31. Alex's answers to the freeze proposal; hypotheses drafted

Alex answered `design/freeze-proposal.md` Q1 to Q4 (AskUserQuestion, relayed by the coordinator),
the recommended option each time: freeze R, P and Q now (G a later campaign); small payload at
c1 m1 without a direction; the many-shape compile direction claimed (R slower from 1024 fields);
`compile_curve.py` ported to Windows and piloted. `hypotheses.md` is written new from the proposal
(DRAFT, frozen after Laptop requests 15, 17 and 18). Choices made there within the answers:
- A cell without a direction is descriptive (ratio and interval), not a two-sided test. Only
  directional contrasts are tested, one Holm family per experiment over both OSes, as the
  proposal showed Alex (compile m at most 36, end to end at most 12; 2^-10 passes both).
- A direction comes only from a pilot that built the cell on that OS with every R build on one
  side of every P build (`bench/pilot/directions.py`). The sizes 32 to 512 were never built, so
  Laptop request 18 (Linux) and block 2 of request 17 (Windows) build them, 3 replicates each.
- The compile measure is the whole build, every phase to the linked program, as the design note
  defines it. The pilot ratios of the proposal left out the link; on A40's builds the directions
  and separations are the same.
- Peak memory is named per measure: the peak resident set on Linux, the peak committed memory
  (compile, job object) and the peak working set (end to end) on Windows.
`compile_curve.py` on Windows: each phase in its own job object (started suspended), `--cpu`
pins the tree, `--timeout` ends it, `peak_commit_kib` from `PeakProcessMemoryUsed`; P's libraries
from the prefix's `.pc` files; dynamic runtime (F4). `--self-test` passed on the desktop.
A Sonnet review of the port (2026-10-31) found no CRITICAL or HIGH defect. Fixed: the check now
runs the program by absolute path (a relative `--work` failed on Linux; every campaign script
passes an absolute one, so no result is affected); on Windows the error mode suppresses crash
dialogs, the check has the phase timeout, a failed start reports its NTSTATUS and never leaves a
suspended process; `directions.py` pools per OS and `ops_limit`; `compare_runs.py` reads Windows
records.
Alex accepted the draft's choices (a) to (e) the same day (AskUserQuestion, relayed by the
coordinator): "Accept all (Recommended)" (quoted in `hypotheses.md` section 6). Laptop plan:
request 18 and request 16's dry run as chain A45 on Linux after P4's set 1; request 17 at the next
Windows slot after A43b's protobuf build.

## 2026-10-31. Windows end-to-end pilot (Laptop request 15, A43b)

A43b ran at 40e9211 (before 701fe83's exception handling in `respond()`, which every arm shares;
the rule of `hypotheses.md` section 3 accepts any pilot). Build: ctest 23 of 23. Run: 264 ghz
windows, every status OK, server counts within the bound. `bench/pilot/pilot_e2e_windows.py`.
- c12 m128: ghz used about 1,170% of its 12 CPUs, the server 24% to 62% of one; every arm, S
  included, within 1%. The rule makes the four Windows E1 contrasts descriptive (not separated,
  server below 95%). `hypotheses.md` section 3 now says those rows show ghz's limit.
- c1 m1 medium: R/P 1.021, every R window above every P window (E2 R vs P: hypothesis); R/Q
  1.008, overlap (E2 R vs Q: descriptive). The Windows end-to-end family becomes 1 contrast, the
  whole family 7, filled at the freeze (section 7, item 3).
- ghz's latency percentiles on Windows are quantized: at c1 m1 every window reports 0 ns up to
  p50 and 0.52 to 0.55 ms above, on calls averaging about 0.25 ms. The Windows latency measure is
  now the mean call cycle, 1/(requests per second), which includes ghz's own time per call.
  Alex accepted this the same day (relayed by the coordinator): "Accept (Recommended)" (quoted
  in `hypotheses.md` section 6).
- c12 m1, the one Windows load that saturates the server (98% to 99%): medium P 45.0k > S 42.5k >
  R 38.8k > Q 38.1k requests per second, every window stable; small S 76.8k > R 75.2k > Q 73.4k >
  P 71.4k. P above S, the transport with no codec, is not a codec cost; Linux shows no such
  inversion. Cause unknown; reported as a Windows transport regime in the threats. Not added as a
  cell: the accepted rule fixed the cells before this pilot.

## 2026-10-31. Linux compile pilot of sizes 32 to 512 (Laptop request 18, A45)

A45's first job ran at 1b93a97 (archive sha256 e15c8abc...bb3c): 60 builds, every one built and
checked, 30 per generator, `ops_limit` 0; 559 s, well inside the request's bound of 1,821 s.
`bench/pilot/directions.py`, pooled with Laptop request 13: all ten new cells R faster, every R
build ahead of every P build. Whole-build R/P, wide 32 to 512: 0.331, 0.395, 0.495, 0.627, 0.764;
many: 0.372, 0.472, 0.572, 0.759, 0.954. On Linux every one of the 18 cells now has a direction
(section 7, item 1, filled at the freeze): wide all R faster, many 16 to 512 R faster, many 1024
to 4096 R slower. The crossover of the many shape lies between 512 and 1024 fields. Many 512 has
the smallest margin (R 19.1 s, P 19.8 s medians), so its paired test is the one most likely to
fail; the rule is applied as written. Summed pilot medians put ten Linux replicates of the 18
cells at about 3.2 h of builds.

## 2026-10-31. Dry run of the D5 records (Laptop request 16, A45) and the clang libc++

A45's second job dry-ran the five Linux records at 701fe83 (archive sha256 57339cf8...9edd). GCC
ASan+UBSan green (24 tests, `allocs` the declared skip; the fuzz smoke; 8 load runs, every arm and
body, 0 failed checks), GCC TSan green (24 tests, 8 load runs on two workers), clang MSan green
(8 tests, `reflection_layer` the declared skip). Clang ASan and TSan red in about 1 s: CMake's
compiler check failed at link, since the laptop has no libc++ but the MSan-instrumented one.

Fix (Laptop request 19, `bench/requests/libcxx-22.1.8.md`): Arch's `libc++` and `libc++abi`
22.1.8-1, the same version as the laptop's clang, copied from the desktop's dev image into a
prefix tarball (sha256 67aad2ef...4007, every file in a manifest; `pacman -Qkk` 0 altered files).
No download and no build. `linux-clang-asan` and `linux-clang-tsan` now name the prefix
`~/opt/libcxx-22.1.8` as `linux-clang-msan` names its own. In the dev image with its own libc++
removed, both presets built and passed 8 of 8 against the extracted tarball, and the tests load
libc++ from the prefix. A clang-only dry run follows before the freeze.

The dry run also showed that the inputs hash named too little. `inputs_hash.py` hashes a target's
own translation units, and `tep_server`'s is `main.cpp` (4 files, 1 translation unit); the measured
code is in `e2e_transport`, `e2e_arms` and `e2e_pb`. `sanitizer-records.md` now names those four
targets for the GCC and MSVC records and the measured release builds, and the five portable
targets (`test_schema`, `test_wire`, `test_grpc`, the two fuzz smokes) for the clang records,
which build no `tep_server`. On the desktop the lab's tool hashed the clang targets: `schema.hpp`,
`wire.hpp`, `grpc.hpp` with their tests and fuzz drivers. On a `linux-gcc1620-e2e` build it hashed
the four server targets: 25 files, every `server/` source, the generated `e2e.pb.cc` and `e2e.pb.h`,
and `include/tep/` with `schema.hpp`, `wire.hpp` and `grpc.hpp`, so the clang records cover headers
the measured server compiles. The second dry run (request 19) covers all five records.

## 2026-10-31. D5 gate of the compile-curve builds

`bench/compile_curve.py` builds are not CMake builds, so `lab/bin/inputs_hash.py` cannot hash
them. Alex, relayed by the coordinator, on the question "Adopt this as the lab rule for those
builds?": "Adopt (Recommended)". The rule: each build records the first-party `include/tep/`
files its compiles list, and each must appear with the same sha256 in a green GCC sanitizer
record's file list.

`compile_curve.py` now, after a build and untimed, runs `g++ -MM` on each compile phase's
translation unit with that phase's flags, and records each listed file under `include/tep/` as
`first_party_inputs` (`tep/include/tep/<file>`: sha256 with CRLF read as LF, as `inputs_hash.py`
names and hashes), with `deps_exit`. The timed commands and the `phases` list are unchanged, and
the step runs after the timed phases, so it warms nothing they read. `bench/check_compile_inputs.py`
matches the records against `inputs_hash.py --list` files: exit 0 if every file matches, 1 if one
is missing or differs, 2 if a built build has no inputs. Records must therefore keep `--list`
(`sanitizer-records.md`; the first dry run kept none). Generator P's compiles include no
`include/tep/` file: an empty map that passes. The per-build generated files are declared in
`coverage.json` as in no record.

Checked without timing (`--no-time`). In the dev image, R wide 8 and many 8 each recorded the
five headers (`fixed_string.hpp`, `grpc.hpp`, `reflect.hpp`, `schema.hpp`, `wire.hpp`), P wide 8
and many 8 none; against the `--list` of a `linux-gcc1620-e2e` build's four server targets the
check passed, with one sha256 changed it exited 1, with a failed dependency step 2. MinGW builds on
the desktop recorded the same five sha256 values and passed against the Linux list. A45 and A46
(1b93a97) predate the step; the campaign's compile jobs run at the frozen commit.

## 2026-11-01. Windows compile pilot (Laptop request 17, A46)

A46 ran on the laptop's Windows at 1b93a97 (archive sha256 a6a2bde7...ebca) with MinGW-w64 GCC
16.2.0 (winlibs r3) and `--cpu 14`. The self-test passed (grandchild memory counted, grandchild
pinned, timeout ends the tree). 111 builds, block 1 in request 13's order and block 2 in request
18's, every one built and checked; all 330 phase records read back affinity `0x4000`, none timed
out, and every check of the request passed. 3,885 s, inside the request's slot of 4 hours.

`bench/pilot/directions.py` (whole builds, link included): all 18 Windows cells separated, each in
the direction Linux shows. Wide, R faster, 16 to 4096 fields: 0.313, 0.340, 0.403, 0.494, 0.629,
0.767, 0.900, 0.886, 0.726. Many, R faster from 16 to 512: 0.332, 0.375, 0.472, 0.572, 0.768,
0.969; R slower from 1024: 1.129, 1.290, 1.438. With Linux, all 36 compile cells carry a
direction (section 7, items 1 and 2, filled at the freeze), so the compile family has m = 36, and
2^-10 = 0.00098 < 0.05/36 = 0.00139. Many 512 has the smallest margin on both OSes (on Windows R
20.48 s against P 20.94 s, medians; ranges 20.46 to 20.57 and 20.88 to 21.04).

The `-ftime-report` builds of R split as on Linux (`bench/pilot/phase_split.py`): opt and generate
72%, 74% and 77% of TOTAL for many 1024, many 2048 and wide 1024 (Linux 72%, 74%, 78%). Largest
peak committed memory: R many 4096, 4,416 MiB (median of 3). Summed pilot medians put ten Windows
replicates of the 18 cells at about 3.4 h of builds (Linux 3.2 h).

## 2026-11-01. Plain libc++ on the laptop and second dry run of the D5 records (A48)

A48's first job (Laptop request 19, archive sha256 c8881c94...89e8) copied the libc++ tarball
(67aad2ef...4007, 1,671,512 bytes), found glibc 2.44 (2.38 needed), extracted it to
`~/opt/libcxx-22.1.8`, and `sha256sum -c` over the manifest's 1,824 files printed nothing and
exited 0. The modification times of `~/opt/libcxx-msan` and `~/opt/libcxx-msan-gcc` were the same
before and after.

The second job dry-ran the five Linux records at 230315e (archive sha256 af17ce75...e4d0), reusing
the sanitizer protobuf prefixes of the first dry run; 328 s. All five green, 0 sanitizer reports:
- GCC ASan+UBSan: 24 tests, `allocs` the declared skip; the fuzz smoke (300,000 runs); 8 load runs,
  every arm and body, each server count within the bound, every server exit 0.
- GCC TSan: 24 tests, `allocs` skipped; 8 load runs on two workers, all passing.
- Clang ASan+UBSan, TSan and MSan: 8 tests each, `reflection_layer` the declared skip. Clang ASan
  and TSan built against the new prefix; the first dry run's failure is gone.
Inputs: every record wrote its hash file and its `--list` file. The GCC records hash `tep_server`,
`e2e_transport`, `e2e_arms` and `e2e_pb` (25 entries: every `server/` source, the generated
`e2e.pb.cc` and `e2e.pb.h`, and the five `include/tep/` headers, with the sha256 values the desktop
recorded); ASan and TSan list the same paths and hashes. The clang records hash the five portable
targets (`schema.hpp`, `wire.hpp`, `grpc.hpp`, their tests and fuzz drivers), the same in all
three. The compile-curve gate at the laptop's paths: R wide 8 recorded the five headers, P wide 8
none, and `check_compile_inputs.py` against the GCC ASan list printed `R: covered 1` (5 entries),
`P: covered 1` (0) and `pass`. Nothing blocks the freeze on the records' side; the records proper
run at the frozen commit.

## 2026-11-01. Sixth fresh review (Opus, 88e5b4c..ce2413c)

A fresh Opus review of the code changed since the fifth review (the measured `reflect.hpp` and
`h2_server.cpp` changes, the Windows port of `compile_curve.py`, the D5 gate, the presets, the new
tests and the pilot scripts): no CRITICAL or HIGH; 2 MEDIUM, 14 LOW, and one note outside the diff.

Outside the diff (N1): `compile_curve.py` compiled with `-std=c++26 -O2` and no `-DNDEBUG`, while
the presets' Release flags are `-O2 -DNDEBUG`. Alex's standing rule (2026-10-09): every timed
build is the optimized Release build, NDEBUG, pilots included. So no compile pilot so far was a
Release build: every one built tep's `assert`s and protobuf's debug checks, and compiled protoc's
code with other flags than the Release libprotobuf it links (both prefixes are built with
`-O2 -DNDEBUG`). Fixed: both generators compile with `-O2 -DNDEBUG`. The user programs check with
explicit returns, not `assert`, so the check still means what it did. The end-to-end pilots built
the release presets (`linux-gcc1620-e2e-release`, `mingw-gcc1620-e2e-release`) and stand. Laptop
requests 20 (Linux) and 21 (Windows, `bench/requests/compile-pilot-release.md`) build the same
111 builds as requests 13 with 18, and 17, at the fixing commit; the draft `hypotheses.md` takes
the compile directions from those two alone, written before they run. The earlier values stay in
section 2 as seen in pilots that fix no direction.

Fixed:
- M1. An R build that recorded no gated file passed the gate. Now an R build must list
  `reflect.hpp` and all R builds the same files, or the check exits 2.
- M2. The gate merged all lists into one, so a stale list could vouch for a file. Now a file must
  be in every list given with that sha256, and the lists of both GCC records (ASan+UBSan and TSan)
  are passed (`sanitizer-records.md`, `coverage.json`). The dry-run script passed the ASan list
  alone; the records proper pass both.
- L2. A failing dependency step keeps its error output (`deps_error`).
- L3. `make_prerequisites` reads GCC's `\#` and `$$` escapes back.
- L4. A failed `TerminateJobObject` falls back to killing the child (closing the job ends the
  rest); the self-test now also checks that no grandchild is left running and that the timeout
  ended the tree within 15 s.
- L6. Each phase's time includes starting its process, so P's four phases carry four process
  starts against R's two: by the reviewer, milliseconds per phase, far below the smallest pilot
  margin (many 512). Section 2 of the draft now says so.
- L7. On Windows the schema and user program were written with CRLF line ends, so the two OSes
  compiled different bytes (R `#embed`s the schema). Both are now written with LF everywhere.
- L8. The comment in `respond()` claimed more than the code guards; corrected (comment only).
- L9, L10. `pilot_e2e_windows.py` now requires the CPU share for every arm and payload before any
  E1 contrast, and leaves windows with a status other than OK out. On A43b's archive every verdict
  is unchanged (E1 descriptive, E2 R vs P hypothesis, E2 R vs Q descriptive; 0 bad windows).
- L11. `directions.py` leaves out Windows builds that section 2 does not count (a phase not on CPU
  14, or timed out). A46 had none.
- L12. `test_growth.cpp`'s header comment named only vectors and short strings; corrected.
Accepted:
- L1. The inputs are hashed after the build. Nothing edits `include/tep/` during a laptop job; the
  checkout is the frozen commit.
- L5. A phase's `affinity` is the job's limit read back, not the processes' mask. The self-test,
  first in every Windows compile job, checks the mask a grandchild actually runs with.
- L13. The clang presets add the plain libc++ with `-isystem` and no `-nostdinc++`. The laptop has
  no system libc++, decision 7 forbids installing one during P4's Arch blocks, and the MSan preset
  works the same way. To revisit if a system libc++ appears.
- L14. `test_interop` writes its own bodies; they are byte for byte those `write_bodies` writes
  (the reviewer checked).
- N2. The end-to-end pilots (A40 at db1f31c, A43b at 40e9211) ran before 701fe83, which added the
  try block in `respond()` (no cost when nothing is thrown) and `reflect.hpp` changes off R's hot
  path, by the reviewer. No end-to-end pilot is repeated.
Checked without timing (`--no-time`). In the dev image and with MinGW on the desktop, R and P at
wide and many 16 and 1024, and R many 4096 with the raised limit, built with `-O2 -DNDEBUG` and
passed their check; every R build listed the five headers and every P build none, and the gate
passed against both GCC lists of A48's dry run. The gate exited 2 for an R build with an empty list
and for R builds with different lists, and 1 when one list lacked `wire.hpp`. On Windows the
generated files have no CRLF, every phase read back its pinned CPU, and the self-test passed its
three checks. `make_prerequisites` reads `\ `, `\#`, `$$` and a continuation line back. A fresh
`linux-gcc1620-e2e` build passed 24 of 24 tests, `allocs` the declared skip.

## 2026-11-01. Compile pilots with the Release flags (Laptop requests 20 and 21, A50)

A50 ran both at 9a3dc92 with lab tools a24f257. Linux (archive sha256 4fd1df21...51bd): 111
builds, block 1 in request 13's order and block 2 in request 18's, every one built and checked,
every check of the request passed, 3,523 s. Windows (7e18a113...da37): the self-test passed its
three checks, then the same 111 builds; all 330 phase records read back affinity `0x4000`, none
timed out, every check passed, 3,839 s. On both OSes every built line has `deps_exit` 0 and no
`deps_error`, every R line lists the five `include/tep/` files and every P line none, and the gate
against both GCC lists of A48's dry run passed (R covered 57, 285 gated entries; P 54). The
`-ftime-report` logs show `-DNDEBUG` on the compile line.

`bench/pilot/directions.py` on each OS's builds alone: all 36 cells separated. Linux, wide, R
faster, 16 to 4096 fields: 0.314, 0.343, 0.409, 0.505, 0.628, 0.741, 0.837, 0.844, 0.778; many, R
faster from 16 to 256: 0.332, 0.383, 0.490, 0.596, 0.804; R slower from 512: 1.015, 1.217, 1.391,
1.563. Windows, wide: 0.322, 0.350, 0.413, 0.502, 0.629, 0.741, 0.835, 0.812, 0.725; many: 0.338,
0.387, 0.489, 0.591, 0.804; then 1.014, 1.200, 1.379, 1.554.

Many 512 changed side: R faster without `-DNDEBUG` (0.954 Linux, 0.969 Windows), R slower now,
with the smallest margin of any cell. Linux R 18.75 s against P 18.47 s (medians; ranges 18.69 to
18.75 and 18.44 to 18.49); Windows 20.10 s against 19.82 s (19.99 to 20.15 and 19.72 to 19.85). By
section 2's rule it carries the pilot's direction, R slower. It lies outside C2 (from 1024
fields), so nothing is contradicted, and no rule is changed for it. Against the pilots without
`-DNDEBUG`, on both OSes, P's median builds are 3% to 10% faster in every many cell, up to 4%
faster in wide 16 to 256 and 4096, and 2% to 8% slower in wide 512 to 2048; R's are 0% to 3% faster
(descriptive; the cause is not studied).

Peak memory of R many 4096: 4,053 MiB resident on Linux, 4,401 MiB committed on Windows (medians
of 3). Summed pilot medians put ten replicates of the 18 cells at 3.1 h of builds on Linux and
3.35 h on Windows; with each job's own overhead (5%, 6%), about 3.3 h and 3.6 h, against the 3.2 h
and 3.4 h stated before. No defect was found, so `CODE_FREEZE` stays 9a3dc92.

## 2026-11-01. Hypotheses frozen (704061a); CODE_FREEZE 9a3dc92

`hypotheses.md` was frozen by 704061a, which changed only its status paragraph and section 7. The
values follow the rules of sections 2 and 3, written before requests 20 and 21 ran: all 36 compile
cells directional (requests 20 and 21; many 512 R slower, outside C2's range), the four Windows E1
contrasts and Windows E2 R against Q descriptive, Windows E2 R against P a hypothesis (request
15, its analysis rerun at 9a3dc92 with the same output). Families: compile m = 36, end to end
m = 7. The commit before it, 89e3072, replaced only section 2's descriptive bullet that quoted the
pilots without `-DNDEBUG` with the Release-flag pilots' values; no rule and no hypothesis changed.
`CODE_FREEZE` is 9a3dc92: since it only `hypotheses.md`, `STATE.md` and this log have changed.
Request 16 (the D5 records) runs at it, and so do the campaigns' builds.

## 2026-11-01. The campaign analysis, written before any campaign ran (ANALYSIS_COMMIT)

`analysis/` (standard library, Python 3.14.5): `stats.py` (the exact one-sided sign test, Holm at
alpha 0.05 per family, the 95% BCa interval from a clustered bootstrap with 10,000 resamples),
`analyse.py` (reads the out folders of Laptop requests 22 to 25, and request 24's `test_allocs`
output and request 26's counts; writes `results/summary.json` and `results/macros.tex`),
`macros.py`, and `test_analyse.py` (17 tests on synthetic folders written in the requests'
formats with the real order files; no timing). The directions and hypotheses are section 7's, held
as tables. The BCa follows the earlier papers' conventions (ties half in z0, the share clipped,
the jackknife acceleration over clusters, inverted-cdf ends); on 40 cases, ties included, it gave
the same z0, acceleration and ends as P3's NumPy code to 8e-15 (desktop check, no timing). A fresh
Opus review found 2 HIGH, 4 MEDIUM and 5 LOW; all fixed before this commit:
- HIGH. `test_allocs` alternates R, P and Q only (11 counts, not 12).
- HIGH. "The cells it touches" (section 3) is read as written: a cell is a payload at a load, so a
  server run void twice makes both loads of its payload on that OS descriptive, every contrast in
  them, whatever the run's arm (the first draft had dropped only the contrasts of that arm).
- MEDIUM. A missing window or server log voids the run instead of stopping (a server that never
  started); the Linux cross-check is reported and stops nothing; shares and peak lines must hold
  their keys and never repeat; the generator's exit status rule is written into
  `e2e-campaign.md` (request 15 checked ghz's; the job scripts void on it, coordinator, same day).
- LOW. Retry lines get the campaign lines' checks; a null share is left out and counted; R many
  4096 and the growth rows that end at it are labelled the secondary curve; seeds print as
  identifiers; share macros say percent; the report prints m beside section 7's 36 and 7.
Choices the hypotheses leave open, fixed here before any campaign data exists, are listed in
`analyse.py`'s docstring: growth intervals clustered by replicate, the `mid` CPU share, the Linux
latency summary (per replicate, then the median over replicates), the Windows mean call cycle
1e6 / mean requests per second, the payload order seeded with the arm order, and the bootstrap
seeds from sha256 of labels naming each cell. The campaign archives are opened only after this
commit; any later change to `analysis/` is a revision-log entry with its reason.

## 2026-11-01. Request 16's Windows record red on its script's test rule; rerun at the same commit

A52's five Linux records were green at 9a3dc92 and are filed (lab 811e13f); every first-party file in
their lists matches the commit. The Windows record (A54, `msvc-e2e-asan` on the laptop's Windows,
MSVC 19.51.36248) came back red with every step at exit 0: 9 tests, 0 failed, `reflection_layer`
skipped, 0 sanitizer reports, and its 8 listed files equal to the commit's. The lab script
required 9 tests with no skip. The request's own rule, written before the run, is "every test
passes or is a declared skip", and the reflection layer on Windows is a declared gap
(`bench/coverage.json`): `tests/CMakeLists.txt` registers `reflection_layer` as a skip (exit 77)
on every compiler without P2996, unchanged since 60eff0e, so the desktop's run at 60eff0e skipped it
too. The request's table row said "9 of 9", which read as no skip; it now names the skip. The script
was corrected to require exactly that skip list (`["reflection_layer"]`; any other skip, or none,
stays red), and the record is made again under a new job id at 9a3dc92. `CODE_FREEZE` does not
move. A54's archive (sha256 87f4540c...) keeps the red record; it is not filed.
