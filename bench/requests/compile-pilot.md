# Request: compile-curve pilot on the laptop's Arch (Laptop request 2)

Plain: build the smallest and the largest test schemas with both generators a few times on the
laptop's Linux, to learn how long one build takes, how much memory it needs, and where the compiler
spends its time.

Why: design note section 4 plans 360 timed builds and has no measured basis for their length. The
phase split (`-ftime-report`) also decides how P5 raises the reflection generator's size ceiling:
if constant evaluation dominates the compile, cutting its operations is the lever; if not,
splitting it into separately evaluated stages is.

## Prerequisites

1. GCC 16.2.0 at `~/opt/gcc-16.2.0` (Laptop request 4, done 2026-10-29).
2. The pinned protobuf runtime, built on the laptop with that GCC (it is not on the laptop yet):
   - copy `C:\Users\alext\opt\_dl\p5-protobuf-36.2\` from the desktop: `abseil-cpp-20250512.1.tar.gz`,
     `protobuf-36.2.tar.gz`, `protoc-36.2-linux-x86_64.zip` (sha256 in `bench/pins.md`);
   - `bash bench/build_protobuf.sh <copied folder> ~/opt/protobuf-36.2 14`. The script checks the
     three sha256 values before it unpacks anything, builds abseil and the static protobuf
     runtime with C++26 and `-O2`, unpacks the release `protoc`, and prints `libprotoc 36.2` and
     the pkg-config version. Logs: `~/opt/protobuf-36.2/logs/`. Keep the job's own wall time for
     this step; it is reported once, separately (design note section 4).
3. The P5 repository at the commit named in the queue entry. Tools: `python3`, `pkg-config`,
   `cmake`, `ninja` (all were present for A19).

## Builds

One build at a time, nothing else running, placement per `lab/laptop/README.md` for a single
process job. Each build is one call of `bench/compile_curve.py`, which writes the schema and the
user program, then runs and times each phase (wall time, peak RSS of the process tree), runs the
program once untimed and checks it, and appends one JSON line.

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-pilot
one() { timeout 3600 python3 -I "$R/bench/compile_curve.py" --gen "$1" --shape "$2" --n "$3" \
          --rep "$4" --work "$W" --out "$OUT/pilot.jsonl" "${@:5}"; echo "$1 $2 $3 $4 exit $?"; }
# Timed: 3 replicates; within each pair the order alternates (R first on odd replicates).
for rep in 1 2 3; do
  for cell in "wide 16" "wide 1024" "many 16" "many 1024"; do
    set -- $cell
    if [ $((rep % 2)) = 1 ]; then one R $1 $2 $rep; one P $1 $2 $rep; else one P $1 $2 $rep; one R $1 $2 $rep; fi
  done
  for shape in wide many; do one P $shape 4096 $rep; done
done
# Phase split: one extra build per cell with -ftime-report (replicate 0; not a timed sample).
for cell in "wide 16" "wide 1024" "many 16" "many 1024"; do
  set -- $cell; one R $1 $2 0 --time-report; one P $1 $2 0 --time-report
done
```

Count: 24 timed builds at 16 and 1024 fields, 6 timed protoc builds at 4096 fields (the reflection
generator does not compile 4096 fields under default limits yet), 8 `-ftime-report` builds.

Why these sizes: 16 is the ladder's bottom. 1024 is the largest size both shapes compile under
GCC's default constant-evaluation limits at the current commit (desktop pass/fail ladder, no
timing). The protoc generator has no such ceiling, so it also gets the ladder's top, 4096.

## Outputs

- `pilot.jsonl` (38 lines; `built` and `checked` must be true on every line).
- `*.time-report.log` (8 files) and the log of any failed build.
- `~/opt/protobuf-36.2/logs/` and the wall time of the protobuf build.
- `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

No measured basis on this laptop. ctpb's retake on the laptop (another compiler and implementation)
took 2 to 9 s per build at 5 to 100 fields, which bounds nothing at 1024 or 4096 fields. The A19
GCC build took 2,611 s at `-j14`; the protobuf runtime is far smaller, but it was not built on the
laptop before. Each build has a 3600 s cap. The job records its own wall time per step.
