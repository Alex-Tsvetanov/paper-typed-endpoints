# Request: end-to-end campaigns, Linux and Windows (Laptop requests 24 and 25)

Plain: the measured end-to-end runs of `hypotheses.md` section 3 (frozen at 704061a). On each OS,
10 replicates; in each, both payloads, and per payload one server run of each arm (R, P, Q, S), in
the seeded order of `bench/campaign/`, at `CODE_FREEZE`. These are the campaign, not pilots: a
campaign that ran is never run again. Request 24 is the laptop's Linux, with the ghz cross-check;
request 25 its Windows. Each is a build job (untimed) and a run job (timed).

When: after request 16's records are green; the build job checks its inputs against those
records. If request 16 is red, the fix moves `CODE_FREEZE` (a revision-log entry), and these
requests run at the new commit.

## Commit and inputs

- P5 at `CODE_FREEZE` = 9a3dc9249809f08a08f84f06f708a362b755392d. Both jobs refuse any other HEAD.
- The order file of the OS, in the tools bundle; the run job checks its sha256 and copies it to the
  output folder before the first window:
  - `e2e-order-linux.tsv`: fe2023bf88627596ad18ae13e0ba6daa76a26fc22fa8bcd98b701f0fc8395031
  - `e2e-order-windows.tsv`: 9daa56f9cb02db4a846653d9bf6cd6d085ff6491b77bc002dace20fe585c7129

  Each file: a `#` line naming the seed (`bench/campaign_order.py`), a header row, then 80 rows
  `index rep payload arm`. Per replicate the two payloads come in a shuffled order and, per
  payload, the four arms in a shuffled order. Section 3 fixes the arm order as seeded; the payload
  order is seeded too (a design detail chosen here, before any run).

## Build job (untimed)

Linux (request 24):
1. Reuse `~/opt/nghttp2-1.70.0`, `~/opt/h2load-1.70.0` and `~/opt/protobuf-36.2` (checked, not
   rebuilt). A new work folder, `~/lab/work/p5-e2e-campaign`; the job refuses if it exists.
2. ghz for the cross-check: copy the desktop's
   `C:\Users\alext\opt\_dl\p5-ghz-0.121.0-linux\ghz-linux-x86_64.tar.gz` (18,743,394 bytes, sha256
   9ae3b93f2c46dac9136e29e81b4a1de8d4e56f092a6fe46884a25c9c83cb2324, `bench/pins.md`), check the
   sha256, unpack to `~/opt/ghz-0.121.0`; `ghz --version` prints `v0.121.0`. Not a new download:
   Alex approved Lab request 2 on 2026-10-30. No `pacman`.
3. `cmake --preset linux-gcc1620-e2e-release`, build, ctest: 24 of 24 (the interop test is not
   configured). Keep `test_allocs`'s output (section 4: allocations per call, from the frozen build).
4. The inputs hash (`lab/bin/inputs_hash.py`) of the four targets `tep_server`, `e2e_transport`,
   `e2e_arms` and `e2e_pb`, with its `--list` file. It must match request 16's GCC ASan+UBSan and
   TSan records: the same 25 files with the same sha256 values.

Windows (request 25):
1. Reuse `C:\Users\alext\opt\protobuf-36.2-mingw`, `nghttp2-1.70.0-mingw` and `ghz-0.121.0` (A43b).
2. `mingw-gcc1620-e2e-release` as in request 15, build, ctest: every test passes. (`test_allocs`
   is Linux only: on Windows a replaced `operator new` misses the runtime DLL's allocations.)
3. The inputs hash of the same four targets, with its `--list` file. By `bench/coverage.json` every
   first-party file must be covered: each file in request 16's Linux GCC lists with the same
   sha256, except the Windows transport source `net_windows.cpp`, which must be in the list of
   request 16's MSVC ASan record of the transport.

Both: the compiler version, `git rev-parse HEAD`, and the sha256 of `tep_server` and of the body
files.

## Run job (timed; the quiet check at job start)

Placement (`lab/laptop/README.md`): the server's one worker on CPU 14 (`--cpus 14`; on Linux also
under `taskset -c 14`), its sibling 15 idle; the load generator on CPUs 2 to 13; boost off; one
server process at a time. Windows: the boot rule (1800 s of uptime) and the leftover check first.

The order file's 80 rows, in row order. Each row is one server run of that arm and payload:
1. Start the server (Linux as `e2e-pilot5-run.sh`; Windows as request 15: `$GCC\bin` first on
   `PATH`, a new process group). Wait for its `tep_server: arm` line.
2. One warm-up window of 10 s at c12 m128 (`k = warmup`), discarded.
3. 4 windows of 10 s at c1 m1 (`k` 1 to 4). On Linux each with h2load's `--log-file`
   (`latency-<arm>-<payload>-r<rep>-<k>.tsv`).
4. 4 windows of 10 s at c12 m128 (`k` 1 to 4).
5. Before stopping the server, read its peak memory: on Linux `VmHWM` from `/proc/<pid>/status`;
   on Windows `PeakWorkingSetSize` (`GetProcessMemoryInfo`). Then stop it (Linux `SIGTERM`, Windows
   `CTRL_BREAK_EVENT`) and keep its log.

Files: `h2load-<arm>-<payload>-c<c>-m<m>-r<rep>-<k>.txt` on Linux,
`ghz-<arm>-<payload>-c<c>-m<m>-r<rep>-<k>.txt` on Windows, and `server-<arm>-<payload>-r<rep>.log`.
One window: on Linux h2load as `win()` in `lab/laptop/linux/p5/e2e-pilot5-run.sh` (`-D 10`, `-t`
min(c, 12), the framed body, the gRPC headers); on Windows ghz as `win()` in request 15
(`--connections c -c c*m -z 10s --duration-stop=ignore --cpus 12`, process affinity `0x3FFC`).
Around every window, the per-process CPU shares (`labrec.process_shares`), in `shares.jsonl`, one
line per window in the format of request 15's `shares.jsonl` (`lab/laptop/win/p5/e2e_win_run.py`),
with these keys on both OSes: `file`, `arm`, `payload`, `c`, `m`, `k` (a string: `warmup`, `1` to
`4`), `rep` (an integer), `rerun` (true for a rerun), `rc` (the generator's exit status), and
`around` (start to end) and `mid` (from 20% to 80% of the window), each a map from process name
(`tep_server`, and `h2load` or `ghz`) to percent of one CPU. Section 3 reports the server's `mid`
share per window. `peak.jsonl`, one line per server run: `arm`, `payload`, `rep`, `rerun`,
`field` (`VmHWM` or `PeakWorkingSetSize`) and `kib` (KiB: `VmHWM` as `/proc` gives it;
`PeakWorkingSetSize` in bytes divided by 1024).

Validity, per server run, as the pilots check it (requests 14 and 15): every window file reports
0 failed, 0 errored, 0 timeout and only 2xx status codes (h2load), or `[OK]` only and no
`Error distribution:` (ghz); the server log's last line says `failed 0`, and its `calls ok` lies
between the sum S of the generator's counts over that run's windows (the warm-up included) and S
plus the sum of 2 c m over those windows. A server run that fails a check is void. After row 80, each void run (rep, payload,
arm) runs once more; a rerun writes `r<rep>x` wherever the first run writes `r<rep>`
(`h2load-R-small-c1-m1-r3x-2.txt`, `latency-R-small-r3x-2.tsv`, `server-R-small-r3x.log`). A run
void twice makes the cells it touches descriptive (section 3); the job goes on. `validate.txt`
names each void run as `void <arm> <payload> r<rep>` (or `r<rep>x`), one per line.

Linux only, after the replicates and any reruns: the ghz cross-check (Alex's decision 4 of
2026-10-23). Medium payload at c12 m128, arms R then P, one server run each: a 10 s warm-up, then 4
windows of 10 s, ghz under `taskset -c 2-13` with `--cpus 12` and request 15's flags at the Linux
paths (`cross-ghz-<arm>-medium-c12-m128-<k>.txt`, `server-cross-<arm>.log`). It checks the load
generator and is never a contrast; its failure voids nothing else.

## Checks

- The executed order equals the order file, row for row.
- Counts. Linux: 720 window files (80 runs of 9 windows), 320 latency logs, 80 server logs, 80 peak
  memory readings, a shares line for every window, plus the reruns' files and the cross-check's 10
  ghz files and 2 server logs. Windows: 720 ghz files, 80 server logs, 80 peak memory readings, a
  shares line for every window, plus the reruns' files.
- `validate.txt` lists every void run, every rerun, and every cell made descriptive.

## Exit codes, and no rerun

As `compile-campaign.md`: 0 when the procedure ran to its end and every check passed (void runs and
reruns are part of the procedure); 1 when refused before the first window (nothing measured; it may
run again); 2 when windows ran but a check failed: inspect and report to P5, never retry with
labrun.

## Outputs

Build job: the configure, build and ctest logs, `test_allocs`'s output, the inputs hash and its
list file, the versions, HEAD, the sha256 of `tep_server` and the bodies. Run job: every window
file, the latency logs (Linux), the server logs, `shares.jsonl`, the peak memory readings
(`peak.jsonl`, one line per server run), the copied order file, `validate.txt`, `steps.log`,
`duration.json`.

## Expected time

Each server run is 90 s of windows plus the server's start and stop and the generator's start per
window: about 95 s. 80 runs: about 2.1 h per OS, plus any reruns; the Linux cross-check adds about
2 minutes. The builds take a few minutes (only P5 is built). A slot of 3.5 hours for each run job.
The Linux latency logs: pilot 5's 36 c1 m1 logs held 240 MB; 320 logs here, about 2.1 GB before
compression.
