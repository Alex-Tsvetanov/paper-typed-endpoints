# Request: fifth end-to-end pilot on the laptop's Arch (Laptop request 14)

Plain: run the pilot of Laptop request 11 (`bench/requests/e2e-pilot-4.md`, A38b) again at the
commit named in the queue entry: arms S, R, Q and P, with R and Q twice per payload in mirrored
order, and the R and Q profiles. A pilot, not a result.

Why: pilot 4 put Q's 6 to 15% lead over R on the medium payload, at the saturating loads, entirely
in R's 27 heap allocations per call. Alex chose keep-storage types (2026-10-30, "Keep-storage types
(Recommended)"): the generated types now keep cleared elements and nested messages for reuse, as
protobuf's do, and on the desktop `test_allocs` counts R medium at 0 allocations per call, as Q
(P 32, every arm 0 on small). Output bytes are unchanged (`test_arms` against P and Q, the golden
vectors, interop, `fuzz_decode`). This pilot measures what that does to R against Q and P.

## Prerequisites (untimed)

As in Laptop request 11, with the work folder `~/lab/work/p5-e2e5`: reuse `~/opt/nghttp2-1.70.0`
and `~/opt/h2load-1.70.0`; the P5 repository at the commit named in the queue entry; then
`cmake --preset linux-gcc1620-e2e-release`, build, ctest (24 of 24: two negative tests were added
since pilot 4), and keep `test_allocs`'s output, which now asserts R medium 0.

## Runs, profile, checks, outputs

As in `bench/requests/e2e-pilot-4.md`: per payload `small: S R Q P Q R`, `medium: S Q R P R Q`; a
second run of an arm numbers its windows 4 to 6, its warm-up `warmup2` and its server log `-2`; the
profile of R and Q on both payloads after the timed runs (R Q on small, Q R on medium), as the job's
own user; 264 window files, 36 latency logs, 12 server logs.

## Expected time

A38b's run took 2,727 s with the profile and checks: about 46 minutes. The build: a few minutes.
