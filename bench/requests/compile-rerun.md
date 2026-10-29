# Request: compile-curve rerun at the front-end commit (Laptop request 5)

Plain: build the reflection generator's test schemas again on the laptop's Linux, now that its
compile-time work is about a third of what the first pilot saw, and build the sizes it can now
reach, to learn how much faster it got and how it grows with schema size.

Why: the pilot (A23, `compile-pilot.md`) ran at c525595. Since then the schema front end needs
about a third of the constant-evaluation operations (`many 1024`: 30.0M to 9.47M), and 2048 fields
(both shapes) and 4096 fields (`wide`) now compile under GCC's default limits. The pilot's
`-ftime-report` put constant evaluation at 11% (`wide 1024`) and 21% (`many 1024`) of the
reflection build and code generation at 62% to 78%; this rerun shows the new split and how the
build grows past 1024 fields. The protoc generator gets 2048 fields, the one size it lacks.

## Prerequisites

- GCC 16.2.0 at `~/opt/gcc-16.2.0` and the protobuf runtime at `~/opt/protobuf-36.2`, both built
  by earlier jobs (A19, A23). Nothing is rebuilt; the job refuses to start if either is missing.
- The P5 repository at the commit named in the queue entry (cf3dbad or later).

## Builds

As in the pilot: one build at a time, pinned to the single server core, nothing else running,
each build one call of `bench/compile_curve.py`, with a 3600 s cap.

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-rerun
one() { timeout 3600 taskset -c 14 python3 -I "$R/bench/compile_curve.py" --gen "$1" --shape "$2" --n "$3" \
          --rep "$4" --work "$W" --out "$OUT/rerun.jsonl" "${@:5}"; echo "$1 $2 $3 $4 exit $?"; }
for rep in 1 2 3; do
  for cell in "wide 16" "many 16" "wide 1024" "many 1024"; do set -- $cell; one R $1 $2 $rep; done
  for shape in wide many; do
    if [ $((rep % 2)) = 1 ]; then one R $shape 2048 $rep; one P $shape 2048 $rep
    else one P $shape 2048 $rep; one R $shape 2048 $rep; fi
  done
  one R wide 4096 $rep
done
# Phase split (replicate 0, not timed samples).
for cell in "wide 1024" "many 1024" "wide 2048" "many 2048" "wide 4096"; do
  set -- $cell; one R $1 $2 0 --time-report
done
```

Count: 27 timed builds (21 reflection, 6 protoc at 2048) and 5 `-ftime-report` builds.

## Outputs

- `rerun.jsonl` (32 lines; `built` and `checked` true on every line).
- `*.time-report.log` (5 files) and the log of any failed build.
- `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

Measured basis from A23 for the sizes it ran (medians): reflection 2.3 s and 2.5 s at 16 fields,
28.3 s and 34.8 s at 1024; protoc 26.8 s and 33.2 s at 1024, 116 s and 167 s at 4096. No
measured basis at 2048, or for the reflection generator at 4096. Each build has a 3600 s cap;
the job records its own wall time.
