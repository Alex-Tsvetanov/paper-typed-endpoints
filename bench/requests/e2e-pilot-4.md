# Request: fourth end-to-end pilot on the laptop's Arch (Laptop request 11)

Plain: run the pilot of Laptop request 9 (`bench/requests/e2e-pilot-3.md`, A35) at the commit
named in the queue entry, with arms R and Q run twice per payload in mirrored order. A pilot, not
a result.

Why: two things changed since A35.
1. Arm R's encoder now writes each message back to front, so it sizes every nested message once
   (A35's profile: sizing Item again was 7.4% of R's cycles on medium). Output bytes are unchanged
   (`test_reflect`, `test_arms` and `fuzz_decode` compare them with the forward writer and with
   protobuf). Heap allocations per call are unchanged: R medium 27, P 32, Q 0, every arm 0 on small.
2. A35 ran each arm once per payload, and R and Q swapped places only between payloads (small S R
   P Q, medium S Q P R), so the arm that ran later lost R against Q in both payloads. Here R and Q
   each run twice per payload, in mirrored order, so each has the same mean position.

## Prerequisites (untimed)

As in Laptop request 9: reuse `~/opt/nghttp2-1.70.0` and `~/opt/h2load-1.70.0`; the P5 repository
at the commit named in the queue entry; then `cmake --preset linux-gcc1620-e2e-release`, build,
ctest (22 of 22), and keep `build/linux-gcc1620-e2e-release/server/test_allocs`'s output.

## Runs (timed; the quiet check at job start)

As in `bench/requests/e2e-pilot.md` (placement, windows, loads, warm-up, latency windows), with
these server runs per payload, S first each time:

```
small:  S R Q P Q R
medium: S Q R P R Q
```

The second run of an arm numbers its windows 4 to 6 instead of 1 to 3 (`h2load-R-small-c12-m8-4.txt`,
`latency-R-small-4.tsv`), names its warm-up window `warmup2` and its server log
`server-R-small-2.log`. The first run's names are as in A35.

## Profile (after the timed runs; not timed results)

As in Laptop request 9, for arms R and Q only, on both payloads, in the order R Q on small and Q R
on medium (4 server runs). Run perf as the job's own user, as in A35.

## Checks

As in `bench/requests/e2e-pilot.md`, per server run (the server bound uses that run's own windows,
its warm-up included). Counts: 264 h2load window files (`h2load-*-c*-m*-*.txt`; 12 server runs of
22 windows), 36 latency logs, 12 server logs.

## Outputs

As in Laptop request 9.

## Expected time

A35's 8 server runs took 1,869 s with the profile and checks (its job record). Here 12 server runs
of 22 windows of 10 s are 2,640 s of windows, plus server starts and 4 profile runs of about 16 s:
about 47 minutes. The build: the server and tests only, a few minutes.
