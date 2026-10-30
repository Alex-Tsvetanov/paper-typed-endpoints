#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# Windows counterpart of build_protobuf.sh, run from Git Bash: builds the pinned abseil-cpp and the
# protobuf C++ runtime (static, no protoc build, no upb) with the pinned MinGW-w64 GCC 16.2.0 and
# arm R's flags (C++26, no extensions, -O2), and unpacks the pinned protoc Windows binary next to
# it. Alex's decision Q3 (2026-10-30, design/revision-log.md): try MinGW; a build that fails, or
# that needs a patch to a pinned source, makes the Windows comparison of the arms a declared gap.
# MinGW GCC has no sanitizers, so there is no instrumented variant. Pins: bench/pins.md.
#
# Usage: build_protobuf_mingw.sh <folder with the downloads> <install prefix> [jobs]
# The compiler is $GCC/bin/g++.exe (GCC is the winlibs mingw64 folder). Logs go to <prefix>/logs.
set -euo pipefail
src=$(cd "$1" && pwd)
prefix=$(cygpath -m "$2")
jobs=${3:-4}
gcc_root=${GCC:?set GCC to the winlibs mingw64 folder}

(cd "$src" && sha256sum -c --strict) <<'EOF'
9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db  abseil-cpp-20250512.1.tar.gz
3d9642a662d10e68ebae5e53f14dcce5105684212d5078f8e0d47d1ab3ae6b64  protobuf-36.2.tar.gz
f0c128dc0d8492eceece83bb459a4c0e316764b929ffbf1aa416357fd644edd3  protoc-36.2-win64.zip
EOF

mkdir -p "$prefix/logs"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export PATH="$gcc_root/bin:$PATH"
common=(-G Ninja "-DCMAKE_MAKE_PROGRAM=$(cygpath -m "$gcc_root/bin/ninja.exe")"
  "-DCMAKE_C_COMPILER=$(cygpath -m "$gcc_root/bin/gcc.exe")" "-DCMAKE_CXX_COMPILER=$(cygpath -m "$gcc_root/bin/g++.exe")"
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG"
  -DCMAKE_CXX_STANDARD=26 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix")
tar -xzf "$src/abseil-cpp-20250512.1.tar.gz" -C "$work"
tar -xzf "$src/protobuf-36.2.tar.gz" -C "$work"
w=$(cygpath -m "$work")

cmake -S "$w/abseil-cpp-20250512.1" -B "$w/b-absl" "${common[@]}" -DABSL_PROPAGATE_CXX_STD=ON \
  -DABSL_BUILD_TESTING=OFF > "$prefix/logs/absl-configure.log" 2>&1
cmake --build "$w/b-absl" -j "$jobs" > "$prefix/logs/absl-build.log" 2>&1
cmake --install "$w/b-absl" > "$prefix/logs/absl-install.log" 2>&1

cmake -S "$w/protobuf-36.2" -B "$w/b-pb" "${common[@]}" -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_LIBUPB=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF \
  -Dprotobuf_ABSL_PROVIDER=package > "$prefix/logs/protobuf-configure.log" 2>&1
cmake --build "$w/b-pb" -j "$jobs" > "$prefix/logs/protobuf-build.log" 2>&1
cmake --install "$w/b-pb" > "$prefix/logs/protobuf-install.log" 2>&1

mkdir -p "$prefix/protoc"
python -I -m zipfile -e "$(cygpath -m "$src/protoc-36.2-win64.zip")" "$prefix/protoc"
"$prefix/protoc/bin/protoc.exe" --version
