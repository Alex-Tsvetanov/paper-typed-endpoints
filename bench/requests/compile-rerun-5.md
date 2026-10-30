# Request: compile rerun with the keep-storage types, both generators (Laptop request 13)

Plain: build the test schemas once more on the laptop's Linux with both generators, exactly as
Laptop request 10 (`bench/requests/compile-rerun-4.md`, A38 step 1) did, at the commit named in the
queue entry. A pilot, not a result.

Why: three changes since A38's commit (30818a5) reach every compile cell. The handler lookup is one
pass (15aaa6a; A39 measured R many 1024 to 4096 4.9 to 24.3% faster). The generated types keep
storage for reuse (Alex, 2026-10-30: "Keep-storage types (Recommended)"), and `parse_from` is clear
plus merge, without the replace mode. Alex chose to report the many-shape compile cost as measured,
with reuse on, so this run gives both shapes' current curves. Untimed size proxy against 15aaa6a:
`many 1024` 206,261 against 202,527 GIMPLE statements (+1.8%), functions 730 to 699, .text +2.8%
(freeing the storage of a nested message that stays absent); `wide 1024` unchanged.

## Prerequisites

As for A38: `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt. The P5
repository at the commit named in the queue entry.

## Builds

As in `bench/requests/compile-rerun-4.md`, with the work folder and the output file renamed, and
A38's swap of the R and P order on even replicates kept:

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-rerun5; PB=~/opt/protobuf-36.2
one() { timeout 3600 taskset -c 14 python3 -I "$R/bench/compile_curve.py" --gen "$1" --shape "$2" --n "$3" \
          --rep "$4" --work "$W" --out "$OUT/rerun5.jsonl" --protobuf "$PB" "${@:5}"; echo "$1 $2 $3 $4 exit $?"; }
```

Count: 48 timed builds (24 per generator) and 3 `-ftime-report` builds of the reflection generator.

## Outputs

As in Laptop request 10: `rerun5.jsonl` (51 lines; `built` and `checked` true on every line;
`ops_limit` 67108864 on the `R many 4096` lines and 0 elsewhere), the time reports, the log of any
failed build, `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

A38 recorded 3,284 s for the same 51 builds; A39 measured R many 1024, 2048 and 4096 faster at
15aaa6a, so about 52 minutes. The cap bounds each build at 3600 s.
