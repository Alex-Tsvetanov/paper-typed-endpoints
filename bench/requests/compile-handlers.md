# Request: compile pilot of the one-pass handler table (Laptop request 12)

Plain: build the reflection generator's many-shape cells at two commits of the P5 repository, the
one A38 measured (30818a5) and the commit named in the queue entry, interleaved in one run, to see
what the new handler lookup saves in compile time. A pilot, not a result.

Why: A38 (`bench/requests/compile-rerun-4.md` at 30818a5) found R's many-shape builds 24 to 27%
slower than A29's, and constant evaluation growing about three times from many 1024 to many 2048
(A29 5.10 to 15.15 s, A38 6.24 to 18.87 s), so part of it grows faster than the number of rpcs.
The endpoint looked up each rpc's handler by listing all of the handler type's member functions,
once per rpc in the handler check and once more per rpc in the endpoint since the reuse commit.
The commit in the queue entry lists them once, sorted, and finds each rpc's handler by binary
search (`detail::handlers`). Generated code is unchanged: `many 1024` has the same .text (567,221
bytes), functions and optimized GIMPLE statements (202,527) as 30818a5 (desktop, untimed).

## Prerequisites

As for A38's compile rerun: `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is
rebuilt. The P5 repository at the commit named in the queue entry; its history contains 30818a5.

## Builds

Both commits' `include/` and `bench/` are taken from git without a checkout, so each build uses its
own commit's header and generator (`compile_curve.py` takes the header from the folder it runs in):

```
R=<P5 repository>; OUT=<job output folder>; W=~/lab/work/p5-handlers; PB=~/opt/protobuf-36.2
mkdir -p "$W/base" "$W/new"
git -C "$R" archive 30818a5 include bench | tar -x -C "$W/base"
git -C "$R" archive HEAD include bench | tar -x -C "$W/new"
one() { t=$1; shift; timeout 3600 taskset -c 14 python3 -I "$W/$t/bench/compile_curve.py" --gen "$1" --shape "$2" \
          --n "$3" --rep "$4" --work "$W/work-$t" --out "$OUT/$t.jsonl" --protobuf "$PB" "${@:5}"; echo "$t $1 $2 $3 $4 exit $?"; }
for rep in 1 2 3; do
  order="base new"; [ $((rep % 2)) = 0 ] && order="new base"
  for n in 1024 2048; do for t in $order; do one $t R many $n $rep; done; done
  one base P many 1024 $rep
done
for t in base new; do one $t R many 4096 1 --ops-limit 67108864; done
for t in base new; do for n in 1024 2048; do one $t R many $n 0 --time-report; done; done
```

Count: 12 timed R builds at many 1024 and 2048 (3 per commit and cell, the commits' order swapped
on replicate 2), 2 at many 4096 (one per commit, at A38's raised limit), 3 timed P builds (drift
against A38), 4 `-ftime-report` R builds (replicate 0, not timed samples).

## Outputs

`base.jsonl` (12 lines) and `new.jsonl` (9 lines), `built` and `checked` true on every line; the
time reports; the log of any failed build; `~/opt/gcc-16.2.0/bin/g++ --version`;
`git -C <P5 repository> rev-parse HEAD`.

## Expected time

From A38 (R many 1024 41 s, 2048 91 s, 4096 224 s; P many 1024 33 s), with the new commit no
slower: 8 x 41 + 8 x 91 + 2 x 224 + 3 x 33 s, about 1,600 s, or 27 minutes. The cap bounds each
build at 3600 s.
