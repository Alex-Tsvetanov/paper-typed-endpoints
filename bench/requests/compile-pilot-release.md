# Request: compile pilots with the Release flags, Linux and Windows (Laptop requests 20 and 21)

Plain: build the 18 test cells with both generators again, on the laptop's Linux (request 20) and
on its Windows (request 21), at the commit named in the queue entry, now that `compile_curve.py`
compiles with the Release flags (`-O2 -DNDEBUG`). A pilot, not a result. These two pilots alone
fix the compile directions of `hypotheses.md`; the earlier compile pilots (requests 13, 17, 18)
compiled without `-DNDEBUG` and fix none.

Why: Alex's standing rule (2026-10-09): every timed build is the optimized Release build, NDEBUG,
pilots included. The sixth fresh review (2026-11-01) found that `compile_curve.py` compiled with
`-std=c++26 -O2` only, while the presets' Release flags are `-O2 -DNDEBUG`, so every compile pilot
so far built tep's `assert`s and protobuf's debug checks, and compiled protoc's code with other
flags than the Release libprotobuf it links. The same commit writes the generated schema and user
program with LF line ends on Windows too (they were CRLF there), so both OSes compile the same
bytes.

## Request 20, Linux (untimed prerequisites as for request 13)

`~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt. Two blocks, serial, the
`one()` function of `lab/laptop/linux/p5/compile-rerun-5.sh` (taskset -c 14, timeout 3600), each
block with a fresh work folder and its own output file:
1. The 51 builds of request 13 (`compile-rerun-5.md`), in its order: 3 replicates of 8 cells, R and
   P alternating cell by cell, the order swapped on even replicates, then the 3 `-ftime-report`
   builds of R. `--ops-limit 67108864` on `R many 4096` only.
2. The 60 builds of request 18 (`compile-gap-sizes.md`), in its order.

Checks: block 1 as `compile-rerun-5.sh` validates (51 lines, `built` and `checked` true, 48 timed,
24 per generator, 3 time-report builds all R, `ops_limit` 67108864 on `R many 4096` only); block 2
as request 18 (60 lines, `built` and `checked` true, 30 per generator, `ops_limit` 0). Added: every
built line has `deps_exit` 0 and no `deps_error`; every R line lists the same five `include/tep/`
files (`fixed_string.hpp`, `grpc.hpp`, `reflect.hpp`, `schema.hpp`, `wire.hpp`), every P line none.

## Request 21, Windows

Request 17 (`compile-pilot-windows.md`) unchanged at the new commit: the self-test first, then the
same two blocks in the same order, `--cpu 14 --timeout 3600`, the same placement, power and quiet
check, the same checks (every phase `"affinity": "0x4000"`, `wall_ns` and `peak_commit_kib` on
every timed phase, no `timed_out`), plus the added checks of request 20.

## Then (untimed, seconds): the gate on full-size builds

`include/tep/` is unchanged since 230315e, the commit of A48's second dry run. Against both GCC
lists of that dry run (`p5dry-230315eda-L-gcc-asan-...inputs.tsv` and `...gcc-tsan-...inputs.tsv`):
`python3 bench/check_compile_inputs.py --list <ASan list> --list <TSan list> <every pilot JSONL of
the OS>`, expected exit 0 with R covered on every R line and 5 gated files each. On Windows the two
lists are copied from the desktop's A48 archive. A dry-run list is never citable; this only checks
the gate on real builds before the records are made at the frozen commit.

## Outputs

As requests 13, 17 and 18: the JSON lines files, the time reports, the log of any failed build, the
compiler version, the P5 commit, the job's total seconds, the self-test's output (Windows), and the
gate check's output.

## Expected time

No build with `-DNDEBUG` is timed yet. The same builds without it: Linux block 1 took 3,056 s (A40)
and block 2 559 s (A45), about 1.0 h; Windows both blocks 3,885 s (A46), about 1.1 h. A slot of at
most 2 hours each. Peak memory without it: R many 4096 held 4,059 MiB resident on Linux and
4,416 MiB committed on Windows.
