#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# MSVC build of the end-to-end transport's one dependency for the Windows sanitizer record (D5):
# libnghttp2 (static, the library only) with the msvc-e2e-asan preset's configuration:
# RelWithDebInfo, the dynamic CRT (/MD), and /fsanitize=address. That record covers the transport
# with arm S and no protobuf: under MSVC ASan every program that links protoc's generated code
# stops before main (bench/coverage.json; Alex, 2026-10-30, design/revision-log.md).
#
# MSVC options are written with a dash (-fsanitize=address), which cl accepts, because Git Bash
# rewrites an argument holding "=/..." as a Windows path.
# Run from Git Bash started inside a vcvars64 environment (cl, link and ninja on PATH), e.g.
#   cmd //c "<VS>\VC\Auxiliary\Build\vcvars64.bat && bash bench/build_deps_msvc.sh ..."
# Usage: build_deps_msvc.sh <nghttp2 download folder> <nghttp2 prefix> [jobs]
set -euo pipefail
ng_src=$(cd "$1" && pwd)
mkdir -p "$2"
ng_prefix=$(cd "$2" && pwd -W)
jobs=${3:-4}
command -v cl > /dev/null || { echo "build_deps_msvc.sh: cl is not on PATH; run inside vcvars64" >&2; exit 1; }
command -v ninja > /dev/null || { echo "build_deps_msvc.sh: ninja is not on PATH" >&2; exit 1; }

(cd "$ng_src" && sha256sum -c --strict) <<'EOF'
e05cb1388eaca3830aded4ccf20044b6e1ac1a61411dcca11b0437c4285c8bc2  nghttp2-1.70.0.tar.xz
EOF

mkdir -p "$ng_prefix/logs"
trap 'echo "build_deps_msvc.sh: failed; logs in $ng_prefix/logs" >&2' ERR
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
w=$(cygpath -m "$work")
tar -xJf "$ng_src/nghttp2-1.70.0.tar.xz" -C "$work"

cmake -S "$w/nghttp2-1.70.0" -B "$w/b-ng" -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
  "-DCMAKE_C_FLAGS=-fsanitize=address" "-DCMAKE_INSTALL_PREFIX=$ng_prefix" \
  -DENABLE_LIB_ONLY=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DENABLE_DOC=OFF \
  > "$ng_prefix/logs/nghttp2-configure.log" 2>&1
cmake --build "$w/b-ng" -j "$jobs" > "$ng_prefix/logs/nghttp2-build.log" 2>&1
cmake --install "$w/b-ng" > "$ng_prefix/logs/nghttp2-install.log" 2>&1
grep -h "define NGHTTP2_VERSION " "$ng_prefix/include/nghttp2/nghttp2ver.h"
