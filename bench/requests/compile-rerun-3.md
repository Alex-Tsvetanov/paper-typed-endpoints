# Request: compile rerun after the hybrid decoder, with both generators (Laptop request 7)

Plain: build the test schemas once more on the laptop's Linux with both generators, after a
change that keeps small messages on the old decoder, to learn whether the many-small-messages
schemas build as fast as before while the one-large-message schemas keep their gain; and to get,
for the first time, both generators measured in the same run.

Why: A28 (`compile-rerun-2.md`, at 614c9f4) showed the table decoder halving the build at
`wide 4096` (246.90 to 126.62 s against A27) but slowing `many` by about 3% at 1024 and 2048 fields:
each 8-field message paid an out-of-line function per field and two static arrays. The hybrid
decoder (this commit) compares inline for messages of at most 16 fields and uses the tables above
that. On the desktop, untimed, it cuts the `many 1024` object's code from 549,573 to 489,597 bytes
and its defined functions from 2,660 to 1,634, and leaves `wide 1024` byte-identical. The protoc
generator has only been measured in other runs (A23, A27); interleaving it here puts both
generators under the same conditions.

## Prerequisites

As for A27 and A28: `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt. The P5
repository at the commit named in the queue entry (the reviewed commit or later).

## Builds

As in A23, A27 and A28: one build at a time, `taskset -c 14`, the quiet check, a 3600 s cap. In each
replicate the two generators alternate cell by cell.

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-rerun3; PB=~/opt/protobuf-36.2
one() { timeout 3600 taskset -c 14 python3 -I "$R/bench/compile_curve.py" --gen "$1" --shape "$2" --n "$3" \
          --rep "$4" --work "$W" --out "$OUT/rerun3.jsonl" --protobuf "$PB" "${@:5}"; echo "$1 $2 $3 $4 exit $?"; }
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

- `rerun3.jsonl` (51 lines; `built` and `checked` true on every line; `ops_limit` 67108864 on the
  `R many 4096` lines and 0 elsewhere).
- `*.time-report.log` (3 files) and the log of any failed build.
- `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

Basis, medians per build: A28 for the reflection generator (2.1 and 2.3 s at 16 fields, 22.8 and
33.9 s at 1024, 53.6 and 73.9 s at 2048, 126.6 s at `wide 4096`, 185.5 s at `many 4096`), A23 and
A27 for the protoc generator (7.5 s at 16, 26.6 and 33.0 s at 1024, 58.9 and 59.6 s at 2048, 167.2
and 116.1 s at 4096). About 500 s per replicate for each generator, so about 50 minutes of builds
plus about 2 minutes of time reports. The cap bounds each build at 3600 s; the job records its own
wall time.
