#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# Builds the pinned abseil-cpp and protobuf C++ runtime (static, no protoc build, no upb) with the
# pinned GCC 16.2.0 and the reflection arm's flags (C++26, no extensions, -O2), and unpacks the
# pinned protoc release binary next to it. Design note section 3: the protoc arm links
# libprotobuf built with the same compiler and flags. Pins: bench/pins.md.
#
# Usage: build_protobuf.sh <folder with the downloads> <install prefix> [jobs]
# The compiler is $GCC/bin/g++ (default ~/opt/gcc-16.2.0). Logs go to <prefix>/logs.
set -euo pipefail
src=$(cd "$1" && pwd)
prefix=$2
jobs=${3:-4}
gcc_root=${GCC:-$HOME/opt/gcc-16.2.0}

# Each download must match its pin before anything is unpacked.
(cd "$src" && sha256sum -c --strict) <<'EOF'
9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db  abseil-cpp-20250512.1.tar.gz
3d9642a662d10e68ebae5e53f14dcce5105684212d5078f8e0d47d1ab3ae6b64  protobuf-36.2.tar.gz
121f6c7afe1d4d0e3ea6aab9432038599250134cbf4474cb1167d2c7decd4278  protoc-36.2-linux-x86_64.zip
EOF

mkdir -p "$prefix/logs"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export CC=$gcc_root/bin/gcc CXX=$gcc_root/bin/g++
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG"
  "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" -DCMAKE_CXX_STANDARD=26 -DCMAKE_CXX_STANDARD_REQUIRED=ON
  -DCMAKE_CXX_EXTENSIONS=OFF -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix")
tar -xzf "$src/abseil-cpp-20250512.1.tar.gz" -C "$work"
tar -xzf "$src/protobuf-36.2.tar.gz" -C "$work"

cmake -S "$work/abseil-cpp-20250512.1" -B "$work/b-absl" "${common[@]}" -DABSL_PROPAGATE_CXX_STD=ON \
  -DABSL_BUILD_TESTING=OFF > "$prefix/logs/absl-configure.log" 2>&1
cmake --build "$work/b-absl" -j "$jobs" > "$prefix/logs/absl-build.log" 2>&1
cmake --install "$work/b-absl" > "$prefix/logs/absl-install.log" 2>&1

cmake -S "$work/protobuf-36.2" -B "$work/b-pb" "${common[@]}" -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_LIBUPB=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF \
  -Dprotobuf_ABSL_PROVIDER=package > "$prefix/logs/protobuf-configure.log" 2>&1
cmake --build "$work/b-pb" -j "$jobs" > "$prefix/logs/protobuf-build.log" 2>&1
cmake --install "$work/b-pb" > "$prefix/logs/protobuf-install.log" 2>&1

mkdir -p "$prefix/protoc"
python3 -I -m zipfile -e "$src/protoc-36.2-linux-x86_64.zip" "$prefix/protoc"
chmod +x "$prefix/protoc/bin/protoc"
"$prefix/protoc/bin/protoc" --version
PKG_CONFIG_PATH=$prefix/lib/pkgconfig pkg-config --modversion protobuf
