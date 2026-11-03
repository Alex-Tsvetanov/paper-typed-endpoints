# Request: the replication campaigns, Linux and Windows (Laptop requests 27 to 30)

Plain: the measured runs of `hypotheses-r2.md` section 3. The same experiments as the first
campaign (requests 22 to 25), run again in new seeded orders, at the commit that froze
`hypotheses-r2.md`, building exactly the first campaign's measured code. A campaign that ran is
never run again. Request 27 is the Linux compile campaign, 28 the Windows one; 29 is the Linux end
to end build job (untimed) and run job (timed, with the ghz cross-check); 30 the same on Windows.
Request 31 (`mixed-traffic.md`) runs after 29's run job, from 29's build.

Each job follows its first-campaign request exactly, `compile-campaign.md` for 27 and 28 and
`e2e-campaign.md` for 29 and 30 (the scripts of chain A55, `lab/laptop/linux/p5/` and
`lab/laptop/win/p5/`), with only the differences below.

## Differences from requests 22 to 25

1. Commit. P5 at R2_FREEZE, the commit that adds `hypotheses-r2.md` (its hash is in the coordinator
   message that queues these requests). Every job refuses any other HEAD. Before its first build,
   each job checks that the measured code is the first campaign's, with this command, which must
   exit 0:

   ```
   git diff --quiet 9a3dc9249809f08a08f84f06f708a362b755392d HEAD -- include server tests \
     CMakeLists.txt CMakePresets.json bench/compile_curve.py bench/gen_schema.py \
     bench/check_compile_inputs.py ':(exclude)server/write_bodies.cpp'
   ```

   `server/write_bodies.cpp` is excluded: it writes the request bodies, gained the smaller order at
   R2_FREEZE, and no measured target includes it (its output is checked in item 4).
2. Orders. The order files of `bench/campaign-r2/`, checked by sha256 and copied to the output
   folder before the first build or window, as before:
   - `compile-order-linux.tsv`: 5885d91d04254bb15ab39465407cc0cfd4ff9a12dbe7d03ab4a0a8b14f44f1bd
   - `compile-order-windows.tsv`: 198a0eaa7ee6f21d93bde2a6d614bd56482db7375504792b9ce126b0b4605b52
   - `e2e-order-linux.tsv`: ddca9587882483566890cb760d0ac18046d919e509680feeb888b1b67c3e6cf7
   - `e2e-order-windows.tsv`: c856033d4a94f07245e30ee54d3e3e2b77453e0db3d33e20a36a3a943e768351

   Same formats as the first campaign's (360 and 80 rows); the `#` line names the new seed.
3. Work folders and job names. New folders, which the jobs refuse if they exist:
   `~/lab/work/p5-r2-compile` (27), `~/lab/work/p5-r2-e2e` (29), and new folders beside the first
   campaign's on Windows (28, 30). Job names: `p5-r2-compile-l`, `p5-r2-compile-w`,
   `p5-r2-e2e-build-l`, `p5-r2-e2e-run-l`, `p5-r2-e2e-build-w`, `p5-r2-e2e-run-w`.
4. Gates, unchanged in kind:
   - Compile (27, 28): the compile-curve gate against the two Linux GCC record lists of A52, as the
     first campaign ran it.
   - End-to-end builds (29, 30): the inputs hash of `tep_server`, `e2e_transport`, `e2e_arms` and
     `e2e_pb` against request 16's records (on Linux the 25 files of A52's GCC lists; on Windows
     every file in the Linux lists except `net_windows.cpp`, which is in A57's list), as before. A
     difference stops the job before the run job is queued.
   - Bodies (29, 30): the sha256 of these four files of `bodies/` must equal the first campaign's
     (request 24's `SHA256SUMS`):
     - `small.bin` 9075ce9668434f2fb835116b93f3c53253d1838e63045a4f04276cda5e351626
     - `medium.bin` 7f7ce52a1fdb4be50692ac74b9fcb6f6d0f0eec4e5d5b60a31906a43be147d1a
     - `small.pb` 1eee74b54ccd7b04f61dade2d4351586b022616fbb247fea894fed8a60051ef7
     - `medium.pb` 2ad1f1cee09594e26780b00cf641a99e011aa33dad8c24469e1fb2c461d6ce7d

     The build also writes `smaller.bin` and `smaller.pb` (for request 31); they go into
     `SHA256SUMS` with the others and must have the digests at the end of this file.
   - ctest: every test passes (24 of 24 on Linux, as request 24).
5. Outputs, checks, exit codes and the rerun rules: as requests 22 to 25, file names unchanged.
   The Linux run job includes the ghz cross-check, as request 24's.

The analysis of all four is `analysis/analyse_r2.py`, with request 31's folder.

## Order of jobs

One OS at a time. On Linux: 29's build job, then 27, then 29's run job, then 31 (which needs 29's
build). On Windows: 30's build job, then 28, then 30's run job. Either OS may go first.

## Expected time

From the first campaign's `duration.json`: compile 11,281 s on Linux and 12,308 s on Windows; end
to end runs 7,390 s and 7,270 s; builds under a minute each (the dependencies are reused). Slots as
before: 5 hours for each compile job and 3.5 hours for each end-to-end run job.

## Digests of the smaller order's bodies

Written by `write_bodies` at R2_FREEZE in the papers-dev image with the pinned GCC 16.2.0 and
protobuf 36.2 (a functional build, untimed):
- `smaller.bin` (872 bytes) fd27f5623439ff99ac5219f3cd69fad0a9ac5f2bb16a160f9459e089efbc3873
- `smaller.pb` (867 bytes) 8fb0c3078cb60c178838af5f270dba9d7483814c5f97039c8fa88c9c31372bb1

The same build wrote the four first-campaign bodies with the digests of item 4, and the first
campaign's Windows build wrote the same four digests as its Linux build, so both OSes check the
same values.
