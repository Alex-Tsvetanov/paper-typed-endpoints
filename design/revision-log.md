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
