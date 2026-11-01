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
