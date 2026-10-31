# Request: compile pilot of the unmeasured sizes, Linux (Laptop request 18)

Plain: build the test schemas at the five sizes no pilot has built (32, 64, 128, 256 and 512
fields), both shapes, both generators, on the laptop's Linux, at the commit named in the queue
entry. A pilot, not a result. Short builds.

Why: the draft `hypotheses.md` (section 2) gives a compile cell a direction only where its OS's
pilot built it and every R build is on one side of every P build; any other cell is reported with
its interval, untested. Alex's Q3 of 2026-10-31 ("Claim direction (Recommended)": state the
pilot's direction beforehand) needs data for these sizes. The pilots built only 16, 1024, 2048 and
4096 fields. Laptop request 17 builds the same sizes on Windows.

## Prerequisites

As for Laptop request 13: `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt.

## Builds

The `one()` function of `lab/laptop/linux/p5/compile-rerun-5.sh` (taskset -c 14, timeout 3600),
with the work folder `~/lab/work/p5-gap` and the output `$O/gap/gap.jsonl`:

```
for rep in 1 2 3; do
  for cell in "wide 32" "many 32" "wide 64" "many 64" "wide 128" "many 128" "wide 256" "many 256" "wide 512" "many 512"; do
    set -- $cell
    if [ $((rep % 2)) = 1 ]; then one R "$1" "$2" $rep; one P "$1" "$2" $rep
    else one P "$1" "$2" $rep; one R "$1" "$2" $rep; fi
  done
done
```

Count: 60 timed builds, 30 per generator. No `-ftime-report` builds, no `--ops-limit`.

## Checks

60 lines, `built` and `checked` true on each, 30 per generator, `ops_limit` 0 on every line.

## Outputs

`gap.jsonl`, the log of any failed build, `~/opt/gcc-16.2.0/bin/g++ --version`,
`git -C <P5 repository> rev-parse HEAD`, the job's total seconds.

## Expected time

A bound from Laptop request 13 (A40, 2026-10-31): each build at most its generator's and shape's
median whole build at 1024 fields (R wide 23.9 s, R many 37.5 s, P wide 26.8 s, P many 33.2 s), so
one replicate is at most 5 x 121.4 = 607 s and the run at most 1,821 s (30 minutes).
