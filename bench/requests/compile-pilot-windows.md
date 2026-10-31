# Request: Windows compile pilot, both generators (Laptop request 17)

Plain: build the test schemas on the laptop's Windows with both generators, with the pinned
MinGW-w64 GCC 16.2.0 and the ported `bench/compile_curve.py`, at the commit named in the queue
entry. A pilot, not a result. It checks the port on the laptop, gives the Windows compile campaign
its hours, and shows whether R and P separate on Windows as they do on Linux.

Why: Alex's Q4 of 2026-10-31 (freeze proposal, relayed by the coordinator): "Port and pilot
(Recommended)". His decision D-W (2026-10-23) planned both generators on Windows.

## The port

`compile_curve.py` on Windows (`os.name == "nt"`):
- each phase runs in its own job object, started suspended and resumed inside the job, so the
  compiler driver and every process it starts (cc1plus, as, collect2, ld) are in the job;
- `--cpu 14` pins the job to CPU 14 (`JOB_OBJECT_LIMIT_AFFINITY`), as `taskset -c 14` pins the
  Linux builds; each phase record carries the affinity read back (`"affinity": "0x4000"`);
- `--timeout S` ends a phase's whole process tree after S seconds (`TerminateJobObject`);
- peak memory is the job's `PeakProcessMemoryUsed`, the peak committed memory of the largest
  process, recorded as `peak_commit_kib`. Linux records the peak resident set (`maxrss_kib`); the
  two measures are compared only within one OS;
- arm P's libraries come from the protobuf prefix's `.pc` files, read by the tool (Windows has no
  pkg-config). In the desktop's dev image the reader names the same 82 libraries and the same
  `-DNOMINMAX` as `pkg-config --static --libs protobuf` on the Linux prefix, in another order; on
  the MinGW prefix it names 84 (with `advapi32` and `dbghelp`, which abseil's files add on
  Windows), and on the desktop that order links every MinGW P build tried (below). On Windows P compiles with
  `-DNOMINMAX`, which the package's `Cflags` name;
- both generators link the C++ runtime dynamically (finding F4: the static libstdc++ of MinGW
  defines an exception destructor twice against abseil built as C++26), and the tool puts the
  compiler's `bin` folder first on PATH for the phases and the check.

`compile_curve.py --self-test --cpu 14` checks the job object untimed: a grandchild's 256 MiB
counts in the peak, the grandchild runs on the pinned CPU, and `--timeout` ends the tree. It passed
on the desktop (with `--cpu 2`). Untimed desktop builds (`--no-time`) of R and P at wide and many
16 and 1024, many 2048 (with `--cpu 2`, every phase reading back `0x4`) and many 4096 (R with
`--ops-limit 67108864`) all built and passed their check; `--timeout 1` on R wide 4096 recorded
`timed_out` and left no compiler process.

## Prerequisites (untimed)

1. Already there: `C:\Users\alext\opt\winlibs-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3` (`lab/laptop/versions.md`).
2. `C:\Users\alext\opt\protobuf-36.2-mingw`, built by Laptop request 15's build job (A43) with
   `bench/build_protobuf_mingw.sh`. If A43 has not run, run its protobuf prerequisite step
   (`bench/requests/e2e-pilot-windows.md`, Prerequisites 1 and 2) first. Nothing else is built.
3. The P5 repository at the commit named in the queue entry; the lab's Python (`LAB_PYTHON`).

## Builds

Two blocks, serial, in this order; the work folder `C:\Users\alext\lab\work\p5-wcc` (under the
Defender exclusion):
1. The 51 builds of Laptop request 13 (`bench/requests/compile-rerun-5.md`, script
   `lab/laptop/linux/p5/compile-rerun-5.sh`), in the same order: 3 replicates of 8 cells, R and P
   alternating cell by cell, the order swapped on even replicates, then 3 `-ftime-report` builds
   of R.
2. The 60 builds of Laptop request 18 (`bench/requests/compile-gap-sizes.md`), in the same order:
   sizes 32, 64, 128, 256 and 512, both shapes, 3 replicates, the order swapped on even
   replicates. The draft `hypotheses.md` (section 2) gives a Windows compile cell a direction only
   where this pilot built it and its builds separate.
Each build, with `G` the generator and `S N K` the shape, size and replicate:

```
%LAB_PYTHON% -I <P5>\bench\compile_curve.py --gen G --shape S --n N --rep K ^
  --work C:\Users\alext\lab\work\p5-wcc --out <job output>\wcc\wcc.jsonl ^
  --cpu 14 --timeout 3600 [--ops-limit 67108864 | --time-report]
```

`--cxx` and `--protobuf` default to the two folders above. `--ops-limit 67108864` on `R many 4096`
only, as on Linux. Before the first build, the job runs the self-test (`--self-test --cpu 14`) and
stops if it fails, and records `g++.exe --version` and `git -C <P5> rev-parse HEAD`.

Placement, power and quiet: as `lab/laptop/README.md` sets them and A40's compile entry
(`lab/laptop/queues/gap62-a-q1-p5-rerun5.json`) ran: the build on CPU 14, its sibling 15 idle;
`"boost": false` (boost mode 0, off); the job starts after the quiet check.

## Checks

Block 1, the validation of `compile-rerun-5.sh` unchanged: 51 lines, `built` and `checked` true on
each, 48 timed (24 per generator) and 3 time-report builds (all R), `ops_limit` 67108864 on
`R many 4096` only. Block 2, as Laptop request 18: 60 lines, `built` and `checked` true on each,
30 per generator, `ops_limit` 0. Added for Windows: every phase record has `"affinity": "0x4000"`,
every timed phase has `wall_ns` and `peak_commit_kib`, and no record has `timed_out`.

## Outputs

`wcc.jsonl`, the time reports, the log of any failed build, the self-test's output, the compiler
version, the P5 commit, the job's total seconds (`duration.json`, as on Linux).

## Expected time

No measured basis on Windows. On the laptop's Linux, block 1 took 3,056 s (Laptop request 13), and
block 2 is bounded by 1,821 s (Laptop request 18's bound): 4,877 s, 1.4 h. MinGW's compiles and its
linking of the 82 static libraries are not timed anywhere yet; a slot of at most 4 hours, the job
recording its own time. Peak memory: the largest Linux build (R many 4096) held 4,059 MiB resident.
