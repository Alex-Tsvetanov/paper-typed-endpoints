# Request: interoperability and constant-evaluation counts on the laptop's Arch (Laptop request 26)

Plain: two untimed things that `hypotheses.md` section 4 reports, made on the laptop at
`CODE_FREEZE` (9a3dc9249809f08a08f84f06f708a362b755392d), because desktop data never appears as a
result: (1) the interoperability test with grpc-go and ghz, every field checked; (2) the
constant-evaluation operation counts of the schema tables for the 18 cells. Nothing is timed; no
quiet check is needed. It can run in any free slot after request 24's build job, which installs
ghz on the laptop's Arch.

## 1. Interoperability (functional)

1. Go 1.27.2: reuse one already on the laptop's Arch if it is the pinned build (`go version`
   prints `go1.27.2 linux/amd64` and its tarball's sha256 is the one below); otherwise copy the
   desktop's `C:\Users\alext\opt\_dl\p5-go-1.27.2-linux\go1.27.2.linux-amd64.tar.gz` (70,590,635
   bytes, sha256 ecbadb99091a3f46e31f5f934b068b1864eafa7995211b39eaddf76996045fe5, `bench/pins.md`),
   check it, and unpack it to `~/opt/go-1.27.2`. Not a new download: Lab request 2, approved by Alex
   on 2026-10-30. No `pacman`.
2. The module cache: copy the desktop's `C:\Users\alext\opt\_dl\p5-go-modcache` (117 MB) to
   `~/lab/work/p5-modcache`. `tools/build_interop.sh` builds with `GOPROXY=off` and
   `GOFLAGS=-mod=readonly`, so Go checks every module against `interop/go/go.sum` and downloads
   nothing.
3. `tools/build_interop.sh ~/opt/go-1.27.2/bin/go ~/lab/work/p5-modcache <work>/interop-bin`.
4. Configure `linux-gcc1620-e2e-release` in a new build folder with
   `-DTEP_INTEROP_BIN=<work>/interop-bin -DTEP_GHZ=$HOME/opt/ghz-0.121.0/ghz`, build, and run the
   whole CTest suite: 25 of 25, `interop` included. Keep its output, and `ctest -V -R interop`'s
   for the per-field lines.

## 2. Constant-evaluation operation counts (deterministic)

For each of the 18 cells (wide and many at 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 fields):
```
MAX=67108864 CXX=$HOME/opt/gcc-16.2.0/bin/g++ CXXFLAGS="-O2 -DNDEBUG" PYTHON=python3 \
  bash bench/constexpr_ops.sh <shape> <n> tables
```
Each prints `<shape> <n> tables: ops <= <count> (within 0.5%)`; keep the 18 lines in
`constexpr-ops.txt`. `-O2 -DNDEBUG` are the campaigns' Release flags. `MAX` raises the bisection's
ceiling to the limit R many 4096 builds with, so that cell is counted too. The script bisects the
smallest `-fconstexpr-ops-limit` under which GCC accepts the tables (`-fsyntax-only`), so its
result does not depend on the machine's speed.

## Outputs

The Go and ghz versions, the sha256 checks, `build_interop.sh`'s output, the configure, build and
CTest logs, `constexpr-ops.txt`, `git rev-parse HEAD`, `g++ --version`.

## Expected time

Interop: copying and building take a few minutes; the test runs in seconds. The counts: each cell
is one check at the ceiling and about 8 bisection steps, each a `-fsyntax-only` compile of the
tables; the small cells take seconds and many 4096 the longest. No laptop measurement exists; a
slot of 2 hours bounds both parts.
