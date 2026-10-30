#!/usr/bin/env bash
# Builds the grpc-go interop programs (interop/go) into OUT, from the module cache alone:
# GOPROXY=off, so nothing is downloaded, and go.sum pins every module. The modules were fetched
# once through proxy.golang.org with GOSUMDB on (Alex, 2026-10-30, design/revision-log.md).
#
#   tools/build_interop.sh <go binary> <module cache> <out dir>
#
# Then configure with -DTEP_INTEROP_BIN=<out dir>; CTest runs them as the test "interop".
set -euo pipefail
go=$1
export GOMODCACHE=$2
out=$3
here=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
export GOPROXY=off GOFLAGS=-mod=readonly GOTOOLCHAIN=local CGO_ENABLED=0
export GOCACHE=${GOCACHE:-$out/.gocache}
cd "$here/interop/go"
"$go" version
"$go" build -trimpath -o "$out/" ./interop_client ./interop_server
ls "$out"
