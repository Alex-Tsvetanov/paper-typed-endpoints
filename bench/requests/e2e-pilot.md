# Request: end-to-end pilot on the laptop's Arch (Laptop request 3)

Plain: build the gRPC benchmark server and the load generator on the laptop's Linux, then run
short load tests against each server variant, to learn how steady one measurement window is and
which load saturates the server. A pilot, not a result.

Why: design note section 5 plans 480 windows per system and has no measured basis for gRPC
windows over HTTP/2, nor for the saturating number of connections and streams. The pilot measures,
per arm and payload: requests per second over consecutive windows (how steady a window is), the
throughput at each of several loads (where it stops rising), and that every call returned
grpc-status 0 (the server's own count, since h2load sees HTTP 200 for every gRPC reply).

Arms: R (reflection), P (protoc, one Arena per request; Alex, 2026-10-30), and S, a stub that
echoes the request bytes with no codec, as the transport's lower bound (a reference, not a
compared arm). Payloads: small (Person, 34 bytes) and medium (Order, 953 bytes).

## Prerequisites (untimed)

1. `~/opt/gcc-16.2.0` and `~/opt/protobuf-36.2` (done). No pacman change of any kind.
2. Copy from the desktop into fresh folders on the laptop (each script checks its sha256 before
   unpacking; values in `bench/pins.md`):
   - `C:\Users\alext\opt\_dl\p5-nghttp2-1.70.0\nghttp2-1.70.0.tar.xz` to `~/lab/dl/p5-nghttp2-1.70.0/`
   - `C:\Users\alext\opt\_dl\p5-libev-4.33\libev-4.33.tar.gz` to `~/lab/dl/p5-libev-4.33/`
3. The P5 repository at the commit named in the queue entry. Then, in it:
   ```
   bash bench/build_nghttp2.sh ~/lab/dl/p5-nghttp2-1.70.0 ~/opt/nghttp2-1.70.0 14
   bash bench/build_h2load.sh ~/lab/dl/p5-nghttp2-1.70.0 ~/lab/dl/p5-libev-4.33 ~/opt/h2load-1.70.0 14
   cmake --preset linux-gcc1620-e2e-release
   cmake --build build/linux-gcc1620-e2e-release -j 14
   ctest --test-dir build/linux-gcc1620-e2e-release
   ```
   `build_h2load.sh` prints `h2load nghttp2/1.70.0` and writes `~/opt/h2load-1.70.0/logs/packages.txt`
   (`pacman -Q openssl zlib`). ctest must pass 20 of 20; its `bodies` test writes the request
   bodies `build/linux-gcc1620-e2e-release/server/bodies/small.bin` (39 bytes) and `medium.bin`
   (958 bytes). On the desktop's dev image all of this passed, and h2load got 2000 of 2000 calls
   through each arm and payload with the server counting 2000 ok and 0 failed.

## Runs (timed; the quiet check at job start)

Placement, `lab/laptop/README.md`, one server core: the server on CPU 14 (one worker,
`--cpus 14`), its sibling 15 idle; h2load on CPUs 2 to 13. One server process at a time.

```
B=build/linux-gcc1620-e2e-release/server; H=~/opt/h2load-1.70.0/bin/h2load; OUT=<job output folder>
win() {  # arm payload c m k [extra h2load args]: one 10 s window
  local method=EchoPerson; [ "$2" = medium ] && method=EchoOrder
  local t=$(( $3 < 12 ? $3 : 12 ))
  taskset -c 2-13 "$H" -D 10 -c "$3" -m "$4" -t "$t" --data="$B/bodies/$2.bin" \
    -H 'content-type: application/grpc' -H 'te: trailers' "${@:6}" \
    "http://127.0.0.1:50051/bench.e2e.Echo/$method" > "$OUT/h2load-$1-$2-c$3-m$4-$5.txt" 2>&1
}
for arm in S R P; do
  for payload in small medium; do
    taskset -c 14 "$B/tep_server" --arm "$arm" --port 50051 --cpus 14 > "$OUT/server-$arm-$payload.log" 2>&1 &
    pid=$!   # wait until the log holds "tep_server: arm"
    win "$arm" "$payload" 12 32 warmup                       # discarded
    for k in 1 2 3; do win "$arm" "$payload" 1 1 "$k" --log-file="$OUT/latency-$arm-$payload-$k.tsv"; done
    for cm in "12 1" "12 8" "12 32" "12 128" "48 32" "48 128"; do
      set -- $cm; for k in 1 2 3; do win "$arm" "$payload" "$1" "$2" "$k"; done
    done
    kill -TERM "$pid"; wait "$pid"   # its last line: "tep_server: calls ok N, failed 0"
  done
done
```

Count: 6 server runs; per run one warm-up window, 3 latency windows (one connection, one stream,
per-request latencies logged) and 18 throughput windows (6 loads, 3 each), all 10 s.

## Checks

- Every h2load file: `0 failed, 0 errored, 0 timeout` and all status codes 2xx.
- Every server log's last line: `failed 0`, and `calls ok` equal to the sum of `succeeded` over
  that run's h2load files.

## Outputs

`h2load-*.txt` (132 files), `latency-*.tsv` (18), `server-*.log` (6), the build logs under
`~/opt/nghttp2-1.70.0/logs` and `~/opt/h2load-1.70.0/logs` (with `packages.txt`), the ctest
output, `~/opt/gcc-16.2.0/bin/g++ --version`, `git -C <P5 repository> rev-parse HEAD`.

## Expected time

Runs: 6 x 22 windows x 10 s = 1,320 s, plus server starts, about 23 minutes. Builds: no measured
basis on the laptop for libnghttp2, libev or h2load; the server build compiles arm R (A23: about
2.5 s for a 16-field schema, and e2e.proto is smaller). The protobuf build took 91 s there. A cap
of one hour for the builds bounds them.
