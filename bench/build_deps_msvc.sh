#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# MSVC builds of the end-to-end dependencies for the Windows sanitizer record (D5): abseil-cpp and
# the protobuf C++ runtime (static, no protoc build, no upb), and libnghttp2 (static, the library
# only), with the msvc-e2e-asan preset's configuration: RelWithDebInfo, the dynamic CRT (/MD), C++23
# (CMake 4.3 has no C++26 flag for MSVC), and /fsanitize=address, which both protobuf and abseil
# must share with the code that links them. Not Debug: with /Od under ASan, a program holding
# protoc's generated code called a null parser function while registering its descriptor before
# main (TcParser::TagDispatch), and MSVC documents /RTC as incompatible with ASan. Also unpacks the pinned protoc Windows binary. That
# record covers the transport and arm P; arm R cannot build with MSVC (no P2996), a declared gap in
# bench/coverage.json. Pins: bench/pins.md.
#
# MSVC options are written with a dash (-fsanitize=address), which cl accepts, because Git Bash
# rewrites an argument holding "=/..." as a Windows path.
# Run from Git Bash started inside a vcvars64 environment (cl, link and ninja on PATH), e.g.
#   cmd //c "<VS>\VC\Auxiliary\Build\vcvars64.bat && bash bench/build_deps_msvc.sh ..."
# Usage: build_deps_msvc.sh <protobuf downloads> <nghttp2 download> <protobuf prefix> <nghttp2 prefix> [jobs]
set -euo pipefail
pb_src=$(cd "$1" && pwd)
ng_src=$(cd "$2" && pwd)
pb_prefix=$(cygpath -m "$3")
ng_prefix=$(cygpath -m "$4")
jobs=${5:-4}
command -v cl > /dev/null || { echo "build_deps_msvc.sh: cl is not on PATH; run inside vcvars64" >&2; exit 1; }
command -v ninja > /dev/null || { echo "build_deps_msvc.sh: ninja is not on PATH" >&2; exit 1; }

(cd "$pb_src" && sha256sum -c --strict) <<'EOF'
9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db  abseil-cpp-20250512.1.tar.gz
3d9642a662d10e68ebae5e53f14dcce5105684212d5078f8e0d47d1ab3ae6b64  protobuf-36.2.tar.gz
f0c128dc0d8492eceece83bb459a4c0e316764b929ffbf1aa416357fd644edd3  protoc-36.2-win64.zip
EOF
(cd "$ng_src" && sha256sum -c --strict) <<'EOF'
e05cb1388eaca3830aded4ccf20044b6e1ac1a61411dcca11b0437c4285c8bc2  nghttp2-1.70.0.tar.xz
EOF

mkdir -p "$pb_prefix/logs" "$ng_prefix/logs"
trap 'echo "build_deps_msvc.sh: failed; logs in $pb_prefix/logs and $ng_prefix/logs" >&2' ERR
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
w=$(cygpath -m "$work")
common=(-G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
  "-DCMAKE_C_FLAGS=-fsanitize=address" "-DCMAKE_CXX_FLAGS=-fsanitize=address -EHsc"
  -DCMAKE_CXX_STANDARD=23 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF)
tar -xzf "$pb_src/abseil-cpp-20250512.1.tar.gz" -C "$work"
tar -xzf "$pb_src/protobuf-36.2.tar.gz" -C "$work"
tar -xJf "$ng_src/nghttp2-1.70.0.tar.xz" -C "$work"

cmake -S "$w/abseil-cpp-20250512.1" -B "$w/b-absl" "${common[@]}" "-DCMAKE_INSTALL_PREFIX=$pb_prefix" \
  -DABSL_PROPAGATE_CXX_STD=ON -DABSL_BUILD_TESTING=OFF > "$pb_prefix/logs/absl-configure.log" 2>&1
cmake --build "$w/b-absl" -j "$jobs" > "$pb_prefix/logs/absl-build.log" 2>&1
cmake --install "$w/b-absl" > "$pb_prefix/logs/absl-install.log" 2>&1

cmake -S "$w/protobuf-36.2" -B "$w/b-pb" "${common[@]}" "-DCMAKE_INSTALL_PREFIX=$pb_prefix" \
  "-DCMAKE_PREFIX_PATH=$pb_prefix" -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
  -Dprotobuf_BUILD_LIBUPB=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_MSVC_STATIC_RUNTIME=OFF \
  -Dprotobuf_ABSL_PROVIDER=package > "$pb_prefix/logs/protobuf-configure.log" 2>&1
cmake --build "$w/b-pb" -j "$jobs" > "$pb_prefix/logs/protobuf-build.log" 2>&1
cmake --install "$w/b-pb" > "$pb_prefix/logs/protobuf-install.log" 2>&1

cmake -S "$w/nghttp2-1.70.0" -B "$w/b-ng" "${common[@]}" "-DCMAKE_INSTALL_PREFIX=$ng_prefix" \
  -DENABLE_LIB_ONLY=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DENABLE_DOC=OFF \
  > "$ng_prefix/logs/nghttp2-configure.log" 2>&1
cmake --build "$w/b-ng" -j "$jobs" > "$ng_prefix/logs/nghttp2-build.log" 2>&1
cmake --install "$w/b-ng" > "$ng_prefix/logs/nghttp2-install.log" 2>&1

mkdir -p "$pb_prefix/protoc"
python -I -m zipfile -e "$(cygpath -m "$pb_src/protoc-36.2-win64.zip")" "$pb_prefix/protoc"
"$pb_prefix/protoc/bin/protoc.exe" --version
grep -h "define NGHTTP2_VERSION " "$ng_prefix/include/nghttp2/nghttp2ver.h"
