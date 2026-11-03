# Request: mixed traffic on Linux (Laptop request 31)

Plain: the timed runs of `hypotheses-r2.md` section 4. One server at a time serves the medium order
and the smaller order at once, from two h2load processes, so that the reflection arm's kept storage
meets requests of two sizes. A run job only (timed); it uses request 29's build (`replication.md`)
and runs after request 29's run job, in the same Linux slot. Job name `p5-r2-mixed-l`.

## Inputs

- P5 at R2_FREEZE, as request 29; the job refuses any other HEAD.
- Request 29's work folder, `~/lab/work/p5-r2-e2e`: `tep_server` and `bodies/`, checked against
  that build's `SHA256SUMS` with `sha256sum -c` (which covers `smaller.bin`), as the first
  campaign's run jobs checked theirs.
- The order file `bench/campaign-r2/mixed-order-linux.tsv`, sha256
  ab0c2e84a44a2d0af4641017d413eba974991a2ed35864ed94f124cf3efe572f, copied to the output folder
  before the first window. A `#` line naming the seed, a header row, then 40 rows `index rep arm`.

## Run job (timed; the quiet check at job start)

Placement, boost off and one server process at a time: as request 29's run job (the server's one
worker on CPU 14, `--cpus 14` under `taskset -c 14`, its sibling 15 idle).

The order file's 40 rows, in row order. Each row is one server run of that arm:
1. Start the server as request 29's run job does, and wait for its `tep_server: arm` line.
2. One warm-up mixed window (`k = warmup`), discarded.
3. 4 mixed windows (`k` 1 to 4).
4. Before stopping the server, read `VmHWM` from `/proc/<pid>/status`. Then stop it (`SIGTERM`) and
   keep its log as `server-<arm>-mix-r<rep>.log`.

One mixed window: two h2load processes started together, each as `win()` of
`lab/laptop/linux/p5/e2e-pilot5-run.sh` (`-D 10`, the gRPC headers) at 6 connections of 128 streams
with 6 threads, both on `/bench.e2e.Echo/EchoOrder`; the job waits for both:

```
taskset -c 2-7  h2load -D 10 -c 6 -m 128 -t 6 --data=<build>/bodies/medium.bin \
  -H 'content-type: application/grpc' -H 'te: trailers' http://127.0.0.1:<port>/bench.e2e.Echo/EchoOrder \
  > h2load-<arm>-mixmedium-c6-m128-r<rep>-<k>.txt 2>&1 &
taskset -c 8-13 h2load -D 10 -c 6 -m 128 -t 6 --data=<build>/bodies/smaller.bin \
  -H 'content-type: application/grpc' -H 'te: trailers' http://127.0.0.1:<port>/bench.e2e.Echo/EchoOrder \
  > h2load-<arm>-mixsmaller-c6-m128-r<rep>-<k>.txt 2>&1 &
wait for both; keep each exit status
```

Around every window (the first snapshot before both start, the last after both end), the
per-process CPU shares (`labrec.process_shares`), one line per window in `mixed-shares.jsonl`, with
the keys: `window` (`<arm>-r<rep>-<k>`, or `<arm>-r<rep>x-<k>` for a rerun), `arm`, `rep` (an
integer), `rerun` (true for a rerun), `k` (a string: `warmup`, `1` to `4`), `rc` (a map from
`medium` and `smaller` to that process's exit status), and `around` and `mid` (as request 24's
`shares.jsonl`), each a map from process name to percent of one CPU, where `tep_server` is the
server and `h2load` is the sum of the two h2load processes. `mixed-peak.jsonl`, one line per
server run: `arm`, `rep`, `rerun`, `field` (`VmHWM`) and `kib`.

Validity, per server run (`hypotheses-r2.md` section 4): each of its 10 window files reports 0
failed, 0 errored, 0 timeout and only 2xx status codes; each `rc` is 0; the server log's last line
says `failed 0`, and its `calls ok` lies between the sum S of the generators' counts over the run's
10 files (warm-up included) and S + 15,360 (2 c m = 1,536 for each of the 10 files). A server run
that fails a check is void. After row 40, each void run (rep, arm) runs once more, writing
`r<rep>x` wherever the first run writes `r<rep>`. A run void twice makes both contrasts
descriptive; the job goes on. `validate.txt` names each void run as `void <arm> mix r<rep>` (or
`r<rep>x`), one per line.

## Checks

- The executed order equals the order file, row for row.
- Counts: 400 window files (40 runs of 5 windows of 2 files), 40 server logs, 40 peak lines, 200
  shares lines, plus the reruns' files.
- `validate.txt` lists every void run and every rerun.

## Exit codes, and no rerun

As `compile-campaign.md`: 0 when the procedure ran to its end and every check passed (void runs and
reruns are part of the procedure); 1 when refused before the first window (nothing measured; it may
run again); 2 when windows ran but a check failed: inspect and report to P5, never retry with
labrun.

## Outputs

Every window file, the server logs, `mixed-shares.jsonl`, `mixed-peak.jsonl`, the copied order
file, `validate.txt`, the `sha256sum -c` output, `steps.log`, `duration.json`.

## Expected time

Each server run is 50 s of windows plus the server's start and stop and the generators' start per
window: about 55 s. 40 runs: about 0.6 h, plus any reruns. A slot of 1.5 hours.
