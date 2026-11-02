# Results provenance

`summary.json` and `macros.tex` are written by `analysis/analyse.py`, unchanged since
ANALYSIS_COMMIT c5a326bfee06c0966af400fd2c944d0205e53afe. The paper's numbers come only from
`macros.tex` (`hypotheses.md` section 5). `report.txt` is the script's printed report.

Code: CODE_FREEZE 9a3dc9249809f08a08f84f06f708a362b755392d. D5 records green at that commit:
five Linux records (A52, Papers lab commit 811e13f) and the Windows record (A57, 541a114).

Inputs: the campaign chain A55 (2026-11-01 and 02) and request 26 (chain A59, 2026-11-02), on the
laptop. Each archive matches its `.sha256` file. Folders under `C:\Users\alext\lab\laptop-archive\`:

| Request | Job | Archive folder | sha256 of the `.tar.gz` |
|---|---|---|---|
| 25 build | p5-camp-e2e-build-w | gap75-w-q2-p5-camp-e2e-build-w-9b79fecf | 42a6a580d796dca249154daa6b6670755af0c02cbf02906bee9be6a358c913bc |
| 23 | p5-camp-compile-w | gap75-w-q1-p5-camp-compile-w-1fb54208 | 08e6799d8b2b56fc558d15da752202b5f40dc0f713ccdfb5d6340c8d5a78ff21 |
| 25 run | p5-camp-e2e-run-w | gap75-w-q3-p5-camp-e2e-run-w-d9824559 | d2d76112dc4f906daf17fb17424a063699a673f7d44c38f8fd43d3eb88716cab |
| 22 | p5-camp-compile-l | gap75-a-q4-p5-camp-compile-l-29a37194 | 513ef3eabd2b5ee3cea8a259b8dca19bc39f6a17cfb63810d2cb8dfabed1ea83 |
| 24 build | p5-camp-e2e-build-l | gap75-a-q5-p5-camp-e2e-build-l-b0428ebe | cf189a46e9753d80c5754be3864c827d8da12cf2be9fba8f5f56e9a00c0856a3 |
| 24 run | p5-camp-e2e-run-l | gap75-a-q6-p5-camp-e2e-run-l-2c36eb4e | 85348e56ffe12a00754a6a27d163051813fde6459b1bc4c8f64b81df5ddd29b7 |
| 26 | p5-functional-l | gap75-a-q7-p5-functional-l-f57e1f08 | 3aaaebef356a10f7328e195181cb0a3ae44b13ff84ccf0aa6fcb8ec2ca1b3ef8 |

Command (Python 3.14.5, standard library only), each folder the archive's unpacked `out`:

```
python -I analysis/analyse.py --compile-linux <22>/out --compile-windows <23>/out \
  --e2e-linux <24 run>/out --e2e-windows <25 run>/out --allocs <24 build>/out/test_allocs.txt \
  --constexpr-ops <26>/out/constexpr-ops.txt --out results
```

Checked before the analysis, beyond what the script checks:
- Every job ran at 9a3dc92 and exited 0. No build pair and no server run was void, so nothing ran again.
- Both compile jobs passed the compile-curve gate against the two Linux GCC record lists.
- The Linux release build's 25 entries equal both GCC record lists (target, path, sha256). The
  Windows build's 19 files are in the Linux lists, except `server/net_windows.cpp`, which is in
  the Windows record's list.
- The run jobs measured the binaries the build jobs hashed: on Linux by `sha256sum -c` against
  the build's `SHA256SUMS`; on Windows, `tep_server.exe` 0e4576fd... in the run's log equals the build's.
- Boost off and the quiet check passed for each timed job. Windows uptime was past 1800 s at each Windows job's start.
- Two runs of the command gave byte-identical `summary.json` and `macros.tex`.
- The Linux ghz cross-check: all 10 window files and both server logs are present. In each server
  run, the server's count of OK calls lies within the pilots' bound over its 5 windows. The run
  job's own note that the check is invalid comes from a job check that wanted 9 files per arm
  instead of 5; it voids nothing.

Request 26 (`functional-evidence.md`, untimed), checked before it was added:
- The job ran at 9a3dc92 with GCC 16.2.0 and exited 0, with 0 failed checks.
- Go 1.27.2 came from the pinned tarball (the job's `steps.log`: sha256 ecbadb99..., as
  `bench/pins.md`). The module cache on the laptop had the desktop's tree sha256 (335fdee4...,
  5731 files) before the job: the chain's staging step, `C:\Users\alext\lab\labrun-queues\chain-a59.log`.
  Go built with `GOPROXY=off` and `-mod=readonly`, so every module was checked against `go.sum`.
- `tools/build_interop.sh` built `interop_client` and `interop_server`. CTest passed 25 of 25.
  In `interop`, the grpc-go client and ghz called arms R, P and Q with 0 failures. The P5 client's
  6 replies from the grpc-go server decoded to the requests. ghz carried both bodies' exact bytes.
- The logs give one verdict per call, not per field. "Every field checked" rests on the code at
  9a3dc92: the grpc-go client compares each reply with its request by `proto.Equal`
  (`interop/go/interop_client/main.go:76`); the grpc-go server compares each request with the
  known messages the same way (`interop/go/interop_server/main.go:58`); the P5 side decodes each
  reply and compares it member by member (`server/test_interop.cpp:238`, the equality at
  `include/tep/reflect.hpp:1533`).
- `constexpr-ops.txt` holds the 18 count lines in cell order, none above the ceiling. Each count
  is the bisection's upper bound: the script prints `ops <= N (within 0.5%)`, and the `Ops` macro
  keeps N. The text must say "at most N, within 0.5%", not an exact count.
- D5: the counts compile `include/tep/` at 9a3dc92 (`-fsyntax-only`, `bench/constexpr_ops.sh`)
  with a generated schema. That is the commit of the green GCC records, whose file lists match it.
  Unlike the compile curves, request 26 has no per-file gate against those lists.

Adding `--constexpr-ops` changed nothing already here (the run of 2026-11-02, compared with
commit d748546):
- No macro was removed or changed. The 18 `Ops` macros were appended, and the old `macros.tex`
  is an exact prefix of the new one.
- In `summary.json` only `constexpr_ops` differs: it was null and now holds 18 counts.
- `report.txt` is byte-identical. Two runs gave byte-identical output.

Interoperability is functional and has no macro. Its evidence is the archive's `ctest.log` and
`ctest-interop.log`.
