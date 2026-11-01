# Request: compile campaigns, Linux and Windows (Laptop requests 22 and 23)

Plain: the measured compile builds of `hypotheses.md` section 2 (frozen at 704061a). On each OS,
10 build pairs (one R and one P build) of each of the 18 cells, in the seeded order of
`bench/campaign/`, at `CODE_FREEZE`. These are the campaign, not pilots: a campaign that ran is
never run again. Request 22 is the laptop's Linux, request 23 its Windows.

When: after request 16's records are green, and after the coordinator has put request 16's two GCC
`--list` files (ASan+UBSan and TSan) in the tools bundle. If request 16 is red, the fix moves
`CODE_FREEZE` (a revision-log entry), and these requests run at the new commit.

## Commit and inputs

- P5 at `CODE_FREEZE` = 9a3dc9249809f08a08f84f06f708a362b755392d. The job refuses any other HEAD.
- The order file of the OS, carried in the tools bundle (the gate-lists pattern of A50). The job
  checks its sha256, then copies it to the output folder before the first build:
  - `compile-order-linux.tsv`: 11414e03b79e7761a154321abf7c33cf94e01f4b16c8c384abba426be6d13840
  - `compile-order-windows.tsv`: 7f40b65babbd021ec40a751d268638f8e3b9228f89aa457d1327224194da2a1c

  Each file: a `#` line naming the seed (`bench/campaign_order.py`), a header row, then 360 rows
  `index rep pair shape n gen ops_limit`. Per replicate the 18 cells come in a shuffled order and
  the two builds of each pair in a shuffled order. `ops_limit` is 67108864 on R many 4096 only.
- The gate lists: request 16's GCC ASan+UBSan and TSan `--list` files, in the tools bundle. A48's
  dry-run lists are not used.

## Request 22, Linux

`~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` exist; nothing is rebuilt. One fresh work folder.
Each build is one row of the order file, in row order (index 1 to 360), run as `one()` in
`lab/laptop/linux/p5/compile-pilot-release.sh`:
```
timeout 3600 taskset -c 14 python3 -I bench/compile_curve.py --gen <gen> --shape <shape> --n <n> \
  --rep <rep> --work <work> --out <out>/campaign.jsonl --protobuf ~/opt/protobuf-36.2 \
  [--ops-limit 67108864 where the row says so]
```
No `-ftime-report` build: every build records its phases.

## Request 23, Windows

As request 21 (`lab/laptop/win/p5/compile_pilot_win_rel.py`): the self-test first (the job stops if
it fails), MinGW-w64 GCC 16.2.0 (winlibs r3), `C:\Users\alext\opt\protobuf-36.2-mingw`, `--cpu 14
--timeout 3600`, the same placement, power check and quiet check, and the Windows boot rule (1800 s
of uptime) and leftover check. The builds are the order file's rows, as on Linux, into
`campaign.jsonl`.

## Void and retry (both)

A build counts if `built` and `checked` are true and, on Windows, every phase reads back affinity
`0x4000` and none has `timed_out` (section 2, Validity). A build that does not count voids its
pair. After row 360, each voided pair (rep and cell) is built again once, both builds, in the order
its rows give, into `retry.jsonl`. A pair voided twice makes its cell descriptive; the job goes on.

## Checks (both)

- `campaign.jsonl` has 360 lines, and line k matches row k of the order file in gen, shape, n, rep
  and ops_limit. Every line has `timed` true and `time_report` false.
- Every built line has `deps_exit` 0 and no `deps_error`. Every R line lists the five
  `include/tep/` files (`fixed_string.hpp`, `grpc.hpp`, `reflect.hpp`, `schema.hpp`, `wire.hpp`);
  every P line lists none. On Windows every phase has `wall_ns` and `peak_commit_kib`.
- `retry.jsonl` holds exactly the retried pairs, two lines each, or does not exist if none.
- `validate.txt` lists the voided pairs, the retried pairs, and every cell made descriptive.
- The gate: `check_compile_inputs.py --list <ASan list> --list <TSan list> campaign.jsonl
  [retry.jsonl]` exits 0 (`gate.txt`).

## Exit codes, and no rerun

- 0: the procedure ran to its end and every check passed. Voided and retried pairs are part of the
  procedure; they are listed, not failures.
- 1: refused before the first build (quiet check, toolchain, HEAD, order file sha256, gate lists
  missing). Nothing was measured; the job may run again.
- 2: builds ran but a check failed (order, counts, gate). Inspect and report to P5. Never retry it
  with labrun: a second run would be a second campaign, and keeping one of two is a choice made
  after seeing data.

## Outputs

`campaign.jsonl`, `retry.jsonl` (if any), the copied order file, `validate.txt`, `gate.txt`, the
log of any failed build (`compile_curve.py` copies it beside the JSON lines), `gxx-version.txt`,
`p5-head.txt`, `steps.log`, `duration.json`, and on Windows `selftest.txt`.

## Expected time

From requests 20 and 21: the whole-build medians of the 36 builds of one replicate sum to 1,115 s
on Linux and 1,205 s on Windows. Ten replicates, with each pilot job's own overhead (5%, 6%):
about 3.3 h on Linux and 3.6 h on Windows, plus any retries. A slot of 5 hours each. Largest
memory: R many 4096 held 4,053 MiB resident on Linux and 4,401 MiB committed on Windows.
