#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# Builds the pinned libnghttp2 (the library only, static, -O2) for the end-to-end transport, on
# Linux with the pinned GCC 16.2.0 or, from Git Bash, with the pinned MinGW-w64 GCC 16.2.0. Every
# build of the transport links this one, so both OSes serve HTTP/2 with the same code. A MinGW
# build of nghttp2 is the library only (its README); the Windows runs use ghz as the generator.
# Pins: bench/pins.md, Lab request 3.
#
# Usage: build_nghttp2.sh <folder with the download> <install prefix> [jobs]
# The compiler is $GCC/bin/gcc (default ~/opt/gcc-16.2.0; on Windows the winlibs mingw64 folder).
# Logs go to <prefix>/logs. Installs nghttp2Config.cmake with the target nghttp2::nghttp2_static.
set -euo pipefail
src=$(cd "$1" && pwd)
prefix=$2
jobs=${3:-4}
gcc_root=${GCC:-$HOME/opt/gcc-16.2.0}

(cd "$src" && sha256sum -c --strict) <<'EOF'
e05cb1388eaca3830aded4ccf20044b6e1ac1a61411dcca11b0437c4285c8bc2  nghttp2-1.70.0.tar.xz
EOF

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tool=()
case "$(uname -s)" in
  MINGW* | MSYS*)
    prefix=$(cygpath -m "$prefix")
    export PATH="$gcc_root/bin:$PATH"
    tool=(-G Ninja "-DCMAKE_MAKE_PROGRAM=$(cygpath -m "$gcc_root/bin/ninja.exe")"
      "-DCMAKE_C_COMPILER=$(cygpath -m "$gcc_root/bin/gcc.exe")")
    w=$(cygpath -m "$work")
    ;;
  *)
    tool=(-G Ninja "-DCMAKE_C_COMPILER=$gcc_root/bin/gcc")
    w=$work
    ;;
esac
mkdir -p "$prefix/logs"
tar -xJf "$src/nghttp2-1.70.0.tar.xz" -C "$work"

cmake -S "$w/nghttp2-1.70.0" -B "$w/b" "${tool[@]}" -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_LIB_ONLY=ON \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DENABLE_DOC=OFF \
  "-DCMAKE_INSTALL_PREFIX=$prefix" > "$prefix/logs/nghttp2-configure.log" 2>&1
cmake --build "$w/b" -j "$jobs" > "$prefix/logs/nghttp2-build.log" 2>&1
cmake --install "$w/b" > "$prefix/logs/nghttp2-install.log" 2>&1
grep -h "define NGHTTP2_VERSION " "$prefix/include/nghttp2/nghttp2ver.h"
