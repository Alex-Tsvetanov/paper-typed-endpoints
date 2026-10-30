# Request: third end-to-end pilot on the laptop's Arch (Laptop request 9)

Plain: run the pilot of Laptop requests 3 and 8 (`bench/requests/e2e-pilot.md`, A31, A33) at the
commit named in the queue entry, with four arms instead of three. A pilot, not a result.

Why: arm R now reuses its request and response storage across calls (the endpoint keeps one per
rpc), under Alex's Q2 answer. Alex chose (2026-10-30, `design/revision-log.md`) to add arm Q,
protobuf with reused non-arena messages as its C++ tutorial advises, next to arm P (one Arena per
request), so that R's reuse is compared with protobuf's own. Heap allocations per medium call
(`test_allocs`, a count): R 27 (it was 62 in A33), P 32, Q 0; 0 for every arm on small.

## Prerequisites (untimed)

1. Reuse A31's `~/opt/nghttp2-1.70.0` and `~/opt/h2load-1.70.0` as they are; no rebuild.
2. The P5 repository at the commit named in the queue entry. Then:
   ```
   cmake --preset linux-gcc1620-e2e-release
   cmake --build build/linux-gcc1620-e2e-release -j 14
   ctest --test-dir build/linux-gcc1620-e2e-release
   build/linux-gcc1620-e2e-release/server/test_allocs
   ```
   ctest must pass 22 of 22 (the interop test needs the Go programs and is not configured here).
   Keep `test_allocs`'s output; on the desktop's dev image it prints R medium 27, P medium 32 and
   Q medium 0, and 0 for every arm on small.

## Runs (timed; the quiet check at job start)

As in `bench/requests/e2e-pilot.md` (placement, windows, loads, warm-up, latency windows), with the
arms S, R, P and Q (`tep_server --arm Q`). The order rotates between payloads, S first each time:

```
for payload in small medium; do
  arms="S R P Q"; [ "$payload" = medium ] && arms="S Q P R"
  ...
done
```

## Profile (after the timed runs; not timed results)

As in `bench/requests/e2e-pilot-2.md`, for the arms R, P and Q on both payloads (6 server runs).
Run perf as the job's own user, not under sudo: `cycles:u` on the server's own pid needs nothing
more than the default `perf_event_paranoid` (2), and the output files stay readable by labrun.

## Checks

As in `bench/requests/e2e-pilot.md`, with these counts: 176 h2load window files
(`h2load-*-c*-m*-*.txt`; 8 server runs of 22 windows), 24 latency logs, 8 server logs.

## Outputs

As in Laptop request 8 (`bench/requests/e2e-pilot-2.md`), with the fourth arm.

## Expected time

Runs: 8 server runs of 22 windows of 10 s, 1,760 s, plus server starts: about 30 minutes (A33's
six runs took 1,395 s). Profile: 6 server runs of about 16 s. Build: the server and tests only, a
few minutes.
