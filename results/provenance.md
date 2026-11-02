# Results provenance

`summary.json` and `macros.tex` are written by `analysis/analyse.py`, unchanged since
ANALYSIS_COMMIT c5a326bfee06c0966af400fd2c944d0205e53afe. The paper's numbers come only from
`macros.tex` (`hypotheses.md` section 5). `report.txt` is the script's printed report.

Code: CODE_FREEZE 9a3dc9249809f08a08f84f06f708a362b755392d. D5 records green at that commit:
five Linux records (A52, Papers lab commit 811e13f) and the Windows record (A57, 541a114).

Inputs: the campaign chain A55 (2026-11-01 and 02), on the laptop. Each archive matches its
`.sha256` file. Folders under `C:\Users\alext\lab\laptop-archive\`:

| Request | Job | Archive folder | sha256 of the `.tar.gz` |
|---|---|---|---|
| 25 build | p5-camp-e2e-build-w | gap75-w-q2-p5-camp-e2e-build-w-9b79fecf | 42a6a580d796dca249154daa6b6670755af0c02cbf02906bee9be6a358c913bc |
| 23 | p5-camp-compile-w | gap75-w-q1-p5-camp-compile-w-1fb54208 | 08e6799d8b2b56fc558d15da752202b5f40dc0f713ccdfb5d6340c8d5a78ff21 |
| 25 run | p5-camp-e2e-run-w | gap75-w-q3-p5-camp-e2e-run-w-d9824559 | d2d76112dc4f906daf17fb17424a063699a673f7d44c38f8fd43d3eb88716cab |
| 22 | p5-camp-compile-l | gap75-a-q4-p5-camp-compile-l-29a37194 | 513ef3eabd2b5ee3cea8a259b8dca19bc39f6a17cfb63810d2cb8dfabed1ea83 |
| 24 build | p5-camp-e2e-build-l | gap75-a-q5-p5-camp-e2e-build-l-b0428ebe | cf189a46e9753d80c5754be3864c827d8da12cf2be9fba8f5f56e9a00c0856a3 |
| 24 run | p5-camp-e2e-run-l | gap75-a-q6-p5-camp-e2e-run-l-2c36eb4e | 85348e56ffe12a00754a6a27d163051813fde6459b1bc4c8f64b81df5ddd29b7 |

Command (Python 3.14.5, standard library only), each folder the archive's unpacked `out`:

```
python -I analysis/analyse.py --compile-linux <22>/out --compile-windows <23>/out \
  --e2e-linux <24 run>/out --e2e-windows <25 run>/out --allocs <24 build>/out/test_allocs.txt --out results
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

Not yet included: request 26 (`functional-evidence.md`: interoperability and the
constant-evaluation counts), chain A59. When it lands, the command runs again with
`--constexpr-ops`, and every macro already here must come out unchanged.
