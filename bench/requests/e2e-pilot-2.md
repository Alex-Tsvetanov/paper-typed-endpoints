# Request: second end-to-end pilot on the laptop's Arch (Laptop request 8)

Plain: run the first pilot again (Laptop request 3, `bench/requests/e2e-pilot.md`, A31) at the
commit named in the queue entry, with one change of order. A pilot, not a result.

Why: in A31 arm R lost to arm P on the medium payload (R/P 0.79 to 0.87 from 12 connections and
32 streams up) and won on the small one. Counting heap allocations per call (`test_allocs`, a
count, not a timing) found R at 93 per medium call and P at 32 (both 0 on small). Repeated fields
now reserve for the elements that arrive together (R: 62 per medium call). This run shows how much
of the medium gap that closes, and repeats S and P, whose code did not change, as a first look at
run-to-run variation between pilots.

## Prerequisites (untimed)

1. Reuse A31's `~/opt/nghttp2-1.70.0` and `~/opt/h2load-1.70.0` as they are; no rebuild.
2. The P5 repository at the commit named in the queue entry. Then:
   ```
   cmake --preset linux-gcc1620-e2e-release
   cmake --build build/linux-gcc1620-e2e-release -j 14
   ctest --test-dir build/linux-gcc1620-e2e-release
   build/linux-gcc1620-e2e-release/server/test_allocs
   ```
   ctest must pass 22 of 22. `test_allocs` prints per-call allocation counts; they are
   deterministic for a build, and on the desktop's dev image they are R medium 62 and P medium 32
   (S 0, small 0 for every arm). Keep its output.

## Runs (timed; the quiet check at job start)

As in `bench/requests/e2e-pilot.md` (placement, windows, loads, warm-up, latency windows), with
one change: R and P swap places in each payload, so that each of them runs last once per payload
across the two pilots:

```
for payload in small medium; do
  arms="S P R"; [ "$payload" = medium ] && arms="S R P"
  ...
done
```

## Profile (after the timed runs; not timed results)

Where R and P spend their cycles at the knee, to explain the medium gap. perf is already on the
laptop's Arch (`/usr/local/bin/perf`, package perf 7.2.6-1, per the coordinator); nothing is
installed. User-space cycles of the server process only (`cycles:u` on its own pid), which the
default `perf_event_paranoid` allows. Separate server runs, after the last timed window, with
their own file names, so none of the checks above sees them:

```
perf --version > "$OUT/perf-version.txt"; cat /proc/sys/kernel/perf_event_paranoid >> "$OUT/perf-version.txt"
for payload in medium small; do
  for arm in R P; do
    method=EchoPerson; [ "$payload" = medium ] && method=EchoOrder
    taskset -c 14 "$B/tep_server" --arm "$arm" --port 50051 --cpus 14 > "$OUT/profile-server-$arm-$payload.log" 2>&1 &
    pid=$!
    for _ in $(seq 100); do grep -q "^tep_server: arm" "$OUT/profile-server-$arm-$payload.log" && break; kill -0 "$pid" || break; sleep 0.1; done
    taskset -c 2-13 "$H" -D 15 -c 12 -m 128 -t 12 --data="$B/bodies/$payload.bin" \
      -H 'content-type: application/grpc' -H 'te: trailers' \
      "http://127.0.0.1:50051/bench.e2e.Echo/$method" > "$OUT/profile-h2load-$arm-$payload.txt" 2>&1 &
    h=$!
    sleep 3
    taskset -c 0 perf record -e cycles:u -F 2999 -p "$pid" -o "$OUT/perf-$arm-$payload.data" -- sleep 10 \
      > "$OUT/perf-record-$arm-$payload.log" 2>&1 || echo "perf record failed: $arm $payload" >> "$OUT/perf-version.txt"
    wait "$h"
    kill -TERM "$pid"; wait "$pid"
    perf report -i "$OUT/perf-$arm-$payload.data" --stdio --no-children --sort dso,symbol --percent-limit 0.3 \
      > "$OUT/perf-$arm-$payload.txt" 2>&1 || true
  done
done
```

A perf failure is reported in `perf-version.txt` and does not fail the job; the timed runs stand
on their own. The release build has no debug information, so inlined functions count toward their
callers; symbol names are enough here (allocator against codec against protobuf's parser).

## Checks

As in `bench/requests/e2e-pilot.md`. Count only the window files in the h2load file check
(`h2load-*-c*-m*-*.txt`, 132), not `h2load-version.txt` and not the `profile-*` files.

## Outputs

As in the first pilot, plus the output of `test_allocs`, and `perf-version.txt`, `perf-*.data`,
`perf-*.txt`, `perf-record-*.log`, `profile-*` from the profile step.

## Expected time

Runs: about 23 minutes, as A31's (1,333 s), plus the profile: 4 server runs of about 16 s each.
Build: the server and tests only, a few minutes (no measured basis on the laptop for this build
alone).
