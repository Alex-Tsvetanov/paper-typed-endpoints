# Request: compile rerun after the decoder change (Laptop request 6)

Plain: build the reflection generator's test schemas once more on the laptop's Linux, after a
change that splits the message decoder into one small function per field, to learn whether that
makes the build faster; and build the largest schema once with the raised compiler limit it needs.

Why: the pilot (A23) put 62% to 78% of the reflection build at 1024 fields in code generation. The
decoder used to compare each field number with every field, in one function as large as the
message (143,951 bytes of code at `wide 1024`); it now dispatches through a table to per-field
functions (613d6fb). A27 (`compile-rerun.md`, at 4d205b4) measures the front-end cuts before this
change; this rerun, at the reviewed commit, measures after it, on the same cells. It also builds
`many 4096`, the one secondary-curve cell (`design/revision-log.md`, 2026-10-30), with
`--ops-limit 67108864`.

## Prerequisites

As for A27: `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt. The P5
repository at the commit named in the queue entry (the reviewed commit or later).

## Builds

As in A23 and A27: one build at a time, `taskset -c 14`, the quiet check, a 3600 s cap.

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-rerun2
one() { timeout 3600 taskset -c 14 python3 -I "$R/bench/compile_curve.py" --gen "$1" --shape "$2" --n "$3" \
          --rep "$4" --work "$W" --out "$OUT/rerun2.jsonl" "${@:5}"; echo "$1 $2 $3 $4 exit $?"; }
for rep in 1 2 3; do
  for cell in "wide 16" "many 16" "wide 1024" "many 1024" "wide 2048" "many 2048" "wide 4096"; do
    set -- $cell; one R $1 $2 $rep
  done
  one R many 4096 $rep --ops-limit 67108864
done
# Phase split (replicate 0, not timed samples).
for cell in "wide 1024" "many 1024" "wide 2048" "many 2048" "wide 4096"; do set -- $cell; one R $1 $2 0 --time-report; done
one R many 4096 0 --time-report --ops-limit 67108864
```

Count: 24 timed builds and 6 `-ftime-report` builds, all of the reflection generator.

## Outputs

- `rerun2.jsonl` (30 lines; `built` and `checked` true on every line; `ops_limit` 67108864 on the
  `many 4096` lines and 0 elsewhere).
- `*.time-report.log` (6 files) and the log of any failed build.
- `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

Basis: A23 medians (reflection 2.3 s and 2.5 s at 16 fields, 28.3 s and 34.8 s at 1024) and A27
when it lands. No measured basis at 2048 or 4096 fields for the reflection generator. The cap
bounds each build at 3600 s; the job records its own wall time.
