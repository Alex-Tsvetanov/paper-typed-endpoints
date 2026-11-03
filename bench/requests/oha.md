# Request: a second load generator on Linux, oha (Laptop requests 32 and 33)

Plain: the runs of `hypotheses-m3.md`. Request 32 is a short pilot that decides whether oha
saturates the server; request 33 is the campaign, run only if the pilot's gate passed. Both are run
jobs (timed) on the laptop's Linux, from request 29's build (`replication.md`), after request 31, in
the same Linux slot. Job names `p5-m3-pilot-l` and `p5-m3-oha-l`.

## Inputs (both jobs)

- P5 at M3_FREEZE, the commit that adds `hypotheses-m3.md` (its hash is in the coordinator message
  that queues these requests). Each job refuses any other HEAD and runs the measured-code guard of
  `replication.md` item 1, which must exit 0.
- Request 29's work folder, `~/lab/work/p5-r2-e2e`: `tep_server` and `bodies/`, checked against that
  build's `SHA256SUMS` with `sha256sum -c`.
- oha: the desktop's `C:\Users\alext\opt\_dl\p5-oha-1.16.0\oha-linux-amd64` (13,429,032 bytes, sha256
  620bb9e16fb53eabc9a3fc45f88bdb41fefa3fee5c05e75892011ce320391716, `bench/pins.md` Lab request 5;
  Alex's yes of 2026-11-03), copied to `~/opt/oha-1.16.0/oha`, checked by sha256 there, made
  executable; `oha --version` prints 1.16.0. No `pacman`.

## One window (both jobs)

As `hypotheses-m3.md` section 1, with `<body>` the payload's `.bin` and `<method>` `EchoPerson`
(small) or `EchoOrder` (medium):

```
taskset -c 2-13 ~/opt/oha-1.16.0/oha --http2 -c 12 -p 128 -z 10s --no-tui --output-format json \
  --worker-threads 12 -m POST -D <build>/bodies/<payload>.bin \
  -H 'content-type: application/grpc' -H 'te: trailers' \
  http://127.0.0.1:<port>/bench.e2e.Echo/<method> > oha-<arm>-<payload>-c12-m128-<tag>-<k>.json
```

The server is started and stopped as request 29's run job does (`--cpus 14` under `taskset -c 14`,
waiting for its `tep_server: arm` line; `SIGTERM`; its log kept as
`server-<arm>-<payload>-<tag>.log`). Placement, boost off and the quiet check at job start: as
request 29's run job. Around every window, the per-process CPU shares (`labrec.process_shares`), one
line per window in `oha-shares.jsonl` with the keys `file`, `arm`, `payload`, `rep` (an integer, 0 in
the pilot), `rerun`, `k` (a string), `rc` (oha's exit status), `around` and `mid` (process name to
percent of one CPU; `tep_server` and `oha`), as request 24's `shares.jsonl`.

Validity of a server run (`hypotheses-m3.md` section 4): every window's JSON has an empty
`errorDistribution` and a `statusCodeDistribution` with the single key `"200"`; each `rc` is 0; the
server log's last line says `failed 0`, and its `calls ok` lies between the sum S of the windows'
200 counts (warm-up included) and S + 3,072 per window.

## Request 32: the pilot and its gate

Eight server runs, in this order: medium R, P, Q, S, then small R, P, Q, S. Each: a warm-up window
(`k = warmup`) and 2 windows (`k` 1, 2), with `<tag>` = `pilot`. No reruns.

The gate: `pass` if all eight runs are valid and, for every run, the mean of `tep_server`'s `mid`
share over windows 1 and 2 is at least 95.0; otherwise `fail`. The job writes the one word to
`gate.txt`, and exits 0 either way when the procedure ran to its end.

## Request 33: the campaign, only after a passed gate

Before its first window, the job reads request 32's `gate.txt` and refuses (exit 1, nothing measured)
unless it says `pass`.

The order file `bench/campaign-m3/oha-order-linux.tsv` (sha256
20faa8f1d7e52c2e420917b2ebb91953da5d1400cc66a36bfd4f780382424d62), checked and copied to the output
folder before the first window: a `#` line naming the seed, a header row, then 80 rows
`index rep payload arm`. Each row is one server run: a warm-up window and 4 windows (`k` 1 to 4),
with `<tag>` = `r<rep>`. After row 80, each void run runs once more with `<tag>` = `r<rep>x`; a run
void twice makes its payload's contrasts descriptive, and the job goes on. `validate.txt` names each
void run as `void <arm> <payload> r<rep>` (or `r<rep>x`), one per line.

Checks: the executed order equals the order file row for row; 400 window files, 80 server logs and
400 shares lines, plus the reruns' files.

## Exit codes, and no rerun

As `compile-campaign.md`: 0 when the procedure ran to its end and every check passed (a failed gate,
void runs and reruns are part of the procedure); 1 when refused before the first window; 2 when
windows ran but a check failed: inspect and report to P5, never retry with labrun.

## Outputs

Every window file, the server logs, `oha-shares.jsonl`, `gate.txt` (32), the copied order file and
`validate.txt` (33), the `sha256sum -c` outputs, `oha --version`, `steps.log`, `duration.json`. The
analysis is `analysis/analyse_m3.py --pilot-linux <32>/out [--oha-linux <33>/out]`.

## Expected time

32: 8 runs of 30 s of windows, about 6 minutes. 33: 80 runs of 50 s of windows, about 1.2 h, plus
any reruns. Slots of 0.5 h and 2 h.
