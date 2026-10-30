# grpc-go interoperability programs

Design note section 6; functional, never timed. `server/test_interop.cpp` runs both directions:

- `interop_client` (grpc-go) calls arms R and P of the server and checks every field of every
  reply against the request it sent; an unknown method must be UNIMPLEMENTED. It also writes
  ghz's request data (`-write-json`); ghz (Alex's decision 4) then calls both rpcs on R and P and
  must report every call OK.
- `interop_server` (grpc-go) checks each tep-encoded request field by field against the known
  messages (`expect`) and echoes it; the test decodes each reply with tep.

Pins: Go 1.27.2 and the modules in `go.sum` (grpc v1.84.0, protobuf v1.36.11), fetched once
through proxy.golang.org with GOSUMDB on (Alex, 2026-10-30, `design/revision-log.md`).

Build from the module cache alone (`GOPROXY=off`), then configure with `-DTEP_INTEROP_BIN` and
`-DTEP_GHZ` (the pinned ghz v0.121.0 binary):

```
tools/build_interop.sh <go binary> <module cache> <out dir>
```

`e2epb/e2e.pb.go` is generated from `server/e2e.proto` by the pinned protoc 36.2 and
protoc-gen-go v1.36.11 (built from this module's `go.sum`):

```
go build -o <dir>/protoc-gen-go google.golang.org/protobuf/cmd/protoc-gen-go
protoc -I ../../server --plugin=protoc-gen-go=<dir>/protoc-gen-go --go_out=e2epb \
  --go_opt=paths=source_relative --go_opt=Me2e.proto=typed-endpoints/interop/e2epb e2e.proto
```
