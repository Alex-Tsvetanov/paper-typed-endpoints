# Request: end-to-end pilot on the laptop's Windows (Laptop request 15)

Plain: build the end-to-end server with the pinned MinGW-w64 GCC on the laptop's Windows, with
protobuf and libnghttp2 built from source the same way, then run the design of pilot 5 (Laptop
request 14) with ghz as the load generator. It answers three questions: does ghz saturate the
server, how steady is a window, and how do R, Q and P compare on Windows. A pilot, not a result.

Why: nothing of P5's end-to-end benchmark has run on Windows. Alex's decision Q1 (2026-10-30,
`design/revision-log.md`) chose "h2load on Linux and ghz (native Go) on Windows. The same generator
serves all arms on each OS, and the comparisons are per OS. The pilot checks that ghz saturates the
server." This is that pilot. If ghz cannot load the server to its limit, the Windows comparison
measures ghz, and the paper must narrow its Windows claims to the loads where the server limits.
Review M1 applies: under this MinGW, `thread_local` is emulated, which arms P and Q pay and R does
not; Windows claims compare the arms within this toolchain only.

## Prerequisites (untimed)

1. Already on the laptop's Windows (`lab/laptop/versions.md`): MinGW-w64 GCC 16.2.0 (winlibs r3)
   at `C:\Users\alext\opt\winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3`, Git for Windows (Git Bash),
   Python 3.14.6. Its `mingw64\bin` also holds the `cmake.exe` and `ninja.exe` the build scripts and
   the preset use, as on the desktop.
2. Copy from the desktop into fresh folders on the laptop. Each build script checks the sha256 of
   its inputs before unpacking (values in `bench/pins.md`); check ghz's zip before unpacking it.
   - `C:\Users\alext\opt\_dl\p5-protobuf-36.2\{abseil-cpp-20250512.1.tar.gz, protobuf-36.2.tar.gz,
     protoc-36.2-win64.zip}` to `C:\Users\alext\lab\dl\p5-protobuf-36.2\`
   - `C:\Users\alext\opt\_dl\p5-nghttp2-1.70.0\nghttp2-1.70.0.tar.xz` to `C:\Users\alext\lab\dl\p5-nghttp2-1.70.0\`
   - `C:\Users\alext\opt\_dl\p5-ghz-0.121.0-windows\ghz-windows-x86_64.zip` (18,613,210 bytes,
     sha256 80329e20b6fe6ccf1cda7c6fe454b2611d12298d6056d067f2229b57d8b151ca) to
     `C:\Users\alext\lab\dl\p5-ghz-0.121.0-windows\`, unpacked to `C:\Users\alext\opt\ghz-0.121.0\`
     (`ghz.exe`, `ghz --version` prints `v0.121.0`). This is not a new download: Alex approved it
     on 2026-10-30 (Lab request 2, "Download all (Recommended)"), and the desktop's file has the
     pinned size and sha256.
3. The P5 repository at the commit named in the queue entry. Then, in it, from Git Bash
   (`C:\Program Files\Git\bin\bash.exe`), with `GCC=/c/Users/alext/opt/winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3/mingw64`,
   `$GCC/bin` first on `PATH` and Python's folder on `PATH` (the protobuf script unpacks protoc
   with `python -I -m zipfile`):
   ```
   bash bench/build_protobuf_mingw.sh /c/Users/alext/lab/dl/p5-protobuf-36.2 /c/Users/alext/opt/protobuf-36.2-mingw 14
   bash bench/build_nghttp2.sh /c/Users/alext/lab/dl/p5-nghttp2-1.70.0 /c/Users/alext/opt/nghttp2-1.70.0-mingw 14
   cmake --preset mingw-gcc1620-e2e-release
   cmake --build build/mingw-gcc1620-e2e-release -j 14
   ctest --test-dir build/mingw-gcc1620-e2e-release
   ```
   The protobuf script prints `libprotoc 36.2`; the nghttp2 script prints its `NGHTTP2_VERSION`
   line (1.70.0). ctest must pass 23 of 23 (no interop test: the preset leaves `TEP_INTEROP_BIN`
   empty). Its `bodies` test writes `build/mingw-gcc1620-e2e-release/server/bodies/small.pb` (34
   bytes, a Person) and `medium.pb` (953 bytes, an Order), the bodies ghz sends. On the desktop
   all of this passed, and ghz (count mode, and 1 s duration windows with
   `--duration-stop=ignore`) got every call OK through each arm and payload, the server counting
   0 failed and between 98 and 191 more calls than ghz (counts only).

## Runs (timed; the quiet check at job start, no earlier than 15 minutes after boot)

Placement, `lab/laptop/README.md`, one server core: the server on CPU 14 (`--cpus 14` pins its one
worker), its sibling 15 idle; ghz on CPUs 2 to 13 (process affinity mask `0x3FFC`) with `--cpus 12`.
One server process at a time.

The server: `build\mingw-gcc1620-e2e-release\server\tep_server.exe --arm <A> --port 50051 --cpus 14`,
with `$GCC\bin` first on `PATH` (libstdc++ is linked dynamically, finding F4), started in a new
process group (Python: `creationflags=subprocess.CREATE_NEW_PROCESS_GROUP`). It prints
`tep_server: arm <A>, port 50051, 1 worker(s)` when it listens. Windows raises no SIGTERM: stop it
with `CTRL_BREAK_EVENT` (Python: `send_signal(signal.CTRL_BREAK_EVENT)`), after which it prints its
last line `tep_server: calls ok N, failed 0` and exits 0.

One window is one ghz run of 10 s. ghz's `-c` is the number of workers, each with one call in
flight, spread evenly over `--connections`; so h2load's c connections of m streams are
`--connections c -c c*m`, the same number of calls in flight on the same number of connections:
```
G=C:\Users\alext\opt\ghz-0.121.0\ghz.exe; B=build\mingw-gcc1620-e2e-release\server
win(arm, payload, c, m, k):   # method EchoPerson for small, EchoOrder for medium
  <G> --insecure --proto server\e2e.proto --call bench.e2e.Echo.<method> -B <B>\bodies\<payload>.pb
      --connections <c> -c <c*m> -z 10s --duration-stop=ignore --cpus 12
      --disable-template-functions --disable-template-data
      -O summary -o <OUT>\ghz-<arm>-<payload>-c<c>-m<m>-<k>.txt 127.0.0.1:50051
```
Per payload the arms run in pilot 5's mirrored order, `small: S R Q P Q R`, `medium: S Q R P R Q`; a
second run of an arm numbers its windows 4 to 6, its warm-up `warmup2` and its server log `-2`.
Per server run: one warm-up window at c12 m32 (`k = warmup`, discarded), 3 windows at c1 m1, then
3 windows at each of c12 m1, c12 m8, c12 m32, c12 m128, c48 m32, c48 m128 (22 windows).

Around every window, the per-process CPU shares (`labrec.process_shares`, as the harnesses that
time windows already record): the shares of `tep_server` and `ghz` are the saturation check. A
server near 100% of its one CPU at the high loads means the server limits; a server well below it,
with ghz high, means ghz limits.

No profile (perf is Linux only).

## Checks

- Every ghz file: exit status 0, `Status code distribution:` lists `[OK]` only, and no `Error
  distribution:` section.
- Every server log's last line: `failed 0`, and `calls ok` between the sum S of the `Count:` values
  over that run's ghz files (the warm-up included) and S plus the sum over those windows of 2 c m.
  With `--duration-stop=ignore`, ghz leaves out of its report the calls still in flight when a
  window ends, though the server may have answered them; at most c m are in flight, and the factor
  2 matches the Linux pilots' bound.
- Counts: 264 ghz files (`ghz-*-c*-m*-*.txt`; 12 server runs of 22 windows), 12 server logs.

## Outputs

`ghz-*.txt` (264), `server-*.log` (12), the process shares of every window, the build logs under
`C:\Users\alext\opt\protobuf-36.2-mingw\logs` and `C:\Users\alext\opt\nghttp2-1.70.0-mingw\logs`,
the ctest output, `g++ --version` (winlibs), `ghz --version`, `git -C <P5 repository> rev-parse
HEAD`, the sha256 of `tep_server.exe` and of the four body files, and the DLLs `tep_server.exe`
imports (`objdump -p tep_server.exe`, the `DLL Name` lines).

## Expected time

Runs: 12 server runs x 22 windows x 10 s = 2,640 s, plus server starts and ghz's start per window
(it parses `e2e.proto` each time): about 50 minutes. Builds: no measured basis on the laptop's
Windows. The laptop's Linux built the protobuf runtime in 91 s (noted in `e2e-pilot.md`); the MinGW
build adds abseil and runs under Windows. A cap of 90 minutes bounds the builds. The slot: about 2.5 hours
at most.
