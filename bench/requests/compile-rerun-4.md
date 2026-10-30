# Request: compile rerun after message reuse, with both generators (Laptop request 10)

Plain: build the test schemas once more on the laptop's Linux with both generators, exactly as
Laptop request 7 (`bench/requests/compile-rerun-3.md`, A29) did, at the commit named in the queue
entry. A pilot, not a result.

Why: since A29 the reflection generator's endpoint reuses one request (and, for handlers that fill
it, one response) per rpc, and its decoder has a replace mode with storage bounds (45a79f8). The
compile cells instantiate an endpoint with one rpc per message, so this code is in every cell. On
the desktop, untimed, the object's code grew against 60eff0e: `many 1024` .text 489,597 to 564,189
bytes (+15.2%), `wide 1024` 431,608 to 442,064 bytes (+2.4%). A29 had R against P at 1.00 (`many
1024`) and 1.20 (`many 2048`). This run measures what the reuse costs in compile time, with P
interleaved in the same run, before the many-shape trade-off goes to Alex.

## Prerequisites

As for A29: `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt. The P5
repository at the commit named in the queue entry.

## Builds

As in `bench/requests/compile-rerun-3.md`, with the work folder and the output file renamed:

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-rerun4; PB=~/opt/protobuf-36.2
one() { timeout 3600 taskset -c 14 python3 -I "$R/bench/compile_curve.py" --gen "$1" --shape "$2" --n "$3" \
          --rep "$4" --work "$W" --out "$OUT/rerun4.jsonl" --protobuf "$PB" "${@:5}"; echo "$1 $2 $3 $4 exit $?"; }
for rep in 1 2 3; do
  for cell in "wide 16" "many 16" "wide 1024" "many 1024" "wide 2048" "many 2048" "wide 4096"; do
    set -- $cell; one R $1 $2 $rep; one P $1 $2 $rep
  done
  one R many 4096 $rep --ops-limit 67108864; one P many 4096 $rep
done
# Phase split (replicate 0, not timed samples).
for cell in "many 1024" "many 2048" "wide 1024"; do set -- $cell; one R $1 $2 0 --time-report; done
```

Count: 48 timed builds (24 per generator) and 3 `-ftime-report` builds of the reflection generator.

## Outputs

As in Laptop request 7: `rerun4.jsonl` (51 lines; `built` and `checked` true on every line;
`ops_limit` 67108864 on the `R many 4096` lines and 0 elsewhere), the time reports, the log of any
failed build, `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

A29 recorded 3,043 s for the same 51 builds. With the reflection generator's code a few percent to
15% larger, about 55 minutes; the cap bounds each build at 3600 s, and the job records its own
wall time.
