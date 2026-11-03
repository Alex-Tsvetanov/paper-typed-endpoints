#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# Builds the gRPC C++ library of arm G (M2, bench/pins.md Lab request 5) into one prefix, with the
# pinned GCC 16.2.0 and the flags of build_protobuf.sh (C++26, no extensions, -O2 -DNDEBUG, static):
# abseil-cpp 20250512.1, protobuf 36.2 (the same runtime as arm P's, here with libprotoc, which
# grpc_cpp_plugin links), c-ares 1.34.9, re2 2025-11-05, then gRPC 1.84.2 with every dependency
# taken from the prefix ("package" providers) and OpenSSL and zlib from the system. gRPC's six
# archives of protocol definitions are unpacked into its third_party folders beforehand, so its
# configure step downloads nothing; the build runs without network.
#
# Usage: build_grpc.sh <gRPC download folder> <protobuf download folder> <install prefix> [jobs]
# The compiler is $GCC/bin/gcc and g++ (default ~/opt/gcc-16.2.0). Logs go to <prefix>/logs.
# EXTRA_FLAGS (the sanitizer flags of a preset) is added to every compile and link, as in
# build_protobuf.sh: every library must be built with the sanitizer flags of the code linking it.
set -euo pipefail
dl=$(cd "$1" && pwd)
pb=$(cd "$2" && pwd)
mkdir -p "$3"
prefix=$(cd "$3" && pwd)
jobs=${4:-4}
gcc_root=${GCC:-$HOME/opt/gcc-16.2.0}

(cd "$dl" && sha256sum -c --strict) <<'EOF'
f1a923286c7eb1d13ca486a0c24cb6332f8fc64311d8b7c77d6dabdea0920f23  grpc-1.84.2.tar.gz
9364b70a12eb59a06830fb488d9357f90100dff0d98b23ea3593a183d43e6f2f  c-ares-1.34.9.tar.gz
87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67  re2-2025-11-05.tar.gz
13583c5a312861648449845b709722676a3c9b43396b6b8e9cbe4538feb74ad2  cel-spec-v0.25.1.tar.gz
ed5e6c319f8ebcdf24a9491f866a599bb9a3c193b859a94ad13bd31f85b46855  data-plane-api-6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz
07a6f700cee5ca796f5230876ed8454bf1d8cc409b3b3e634505bc4f32a9f72b  googleapis-2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.zip
5e9b520b22afbd53a662cc29017064be253c1dfa6df8958738594e7fec6ade33  grpc-proto-ec30f589e2519d595688b9a42f88a91bdd6b733f.tar.gz
e4718352754df1393b8792b631338aa8562f390e8160783e365454bc11d96328  protoc-gen-validate-v1.2.1.tar.gz
49535f3c3370004309da50194c09bbfc528d4702424dd46e7d56a278a3dfc15d  xds-ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz
EOF
(cd "$pb" && sha256sum -c --strict) <<'EOF'
9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db  abseil-cpp-20250512.1.tar.gz
3d9642a662d10e68ebae5e53f14dcce5105684212d5078f8e0d47d1ab3ae6b64  protobuf-36.2.tar.gz
121f6c7afe1d4d0e3ea6aab9432038599250134cbf4474cb1167d2c7decd4278  protoc-36.2-linux-x86_64.zip
EOF

mkdir -p "$prefix/logs"
trap 'echo "build_grpc.sh: failed; logs in $prefix/logs" >&2' ERR
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export CC=$gcc_root/bin/gcc CXX=$gcc_root/bin/g++
extra=${EXTRA_FLAGS:-}
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG"
  "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" "-DCMAKE_CXX_FLAGS=$extra" "-DCMAKE_C_FLAGS=$extra"
  "-DCMAKE_EXE_LINKER_FLAGS=$extra" "-DCMAKE_SHARED_LINKER_FLAGS=$extra" -DCMAKE_CXX_STANDARD=26 -DCMAKE_CXX_STANDARD_REQUIRED=ON
  -DCMAKE_CXX_EXTENSIONS=OFF -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DBUILD_SHARED_LIBS=OFF "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix")
pacman -Q openssl zlib > "$prefix/logs/packages.txt"

step() {  # name source-dir cmake-args...: configure, build and install one project into the prefix
  local name=$1 dir=$2
  shift 2
  cmake -S "$dir" -B "$work/b-$name" "${common[@]}" "$@" > "$prefix/logs/$name-configure.log" 2>&1
  cmake --build "$work/b-$name" -j "$jobs" > "$prefix/logs/$name-build.log" 2>&1
  cmake --install "$work/b-$name" > "$prefix/logs/$name-install.log" 2>&1
}

tar -xzf "$pb/abseil-cpp-20250512.1.tar.gz" -C "$work"
step absl "$work/abseil-cpp-20250512.1" -DABSL_PROPAGATE_CXX_STD=ON -DABSL_BUILD_TESTING=OFF
tar -xzf "$pb/protobuf-36.2.tar.gz" -C "$work"
step protobuf "$work/protobuf-36.2" -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_PROTOC_BINARIES=ON \
  -Dprotobuf_BUILD_LIBUPB=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_ABSL_PROVIDER=package
tar -xzf "$dl/c-ares-1.34.9.tar.gz" -C "$work"
step cares "$work/c-ares-1.34.9" -DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_BUILD_TOOLS=OFF -DCARES_BUILD_TESTS=OFF
tar -xzf "$dl/re2-2025-11-05.tar.gz" -C "$work"
step re2 "$work/re2-2025-11-05" -DRE2_BUILD_TESTING=OFF

tar -xzf "$dl/grpc-1.84.2.tar.gz" -C "$work"
g=$work/grpc-1.84.2
unpack() {  # archive third_party-folder: one archive's top folder becomes that folder
  local tmp=$work/unpack-$2
  mkdir -p "$tmp"
  case $1 in *.zip) python3 -I -m zipfile -e "$1" "$tmp" ;; *) tar -xzf "$1" -C "$tmp" ;; esac
  rm -rf "${g:?}/third_party/$2"
  mv "$tmp"/* "$g/third_party/$2"
}
unpack "$dl/cel-spec-v0.25.1.tar.gz" cel-spec
unpack "$dl/data-plane-api-6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz" envoy-api
unpack "$dl/googleapis-2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.zip" googleapis
unpack "$dl/grpc-proto-ec30f589e2519d595688b9a42f88a91bdd6b733f.tar.gz" grpc-proto
unpack "$dl/protoc-gen-validate-v1.2.1.tar.gz" protoc-gen-validate
unpack "$dl/xds-ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz" xds
plugins=()
for lang in CSHARP NODE OBJECTIVE_C PHP PYTHON RUBY; do plugins+=("-DgRPC_BUILD_GRPC_${lang}_PLUGIN=OFF"); done
step grpc "$g" -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DgRPC_BUILD_CODEGEN=ON -DgRPC_BUILD_GRPC_CPP_PLUGIN=ON \
  "${plugins[@]}" -DgRPC_BUILD_CSHARP_EXT=OFF -DgRPC_USE_SYSTEMD=OFF -DgRPC_ABSL_PROVIDER=package \
  -DgRPC_PROTOBUF_PROVIDER=package -DgRPC_CARES_PROVIDER=package -DgRPC_RE2_PROVIDER=package \
  -DgRPC_SSL_PROVIDER=package -DgRPC_ZLIB_PROVIDER=package

mkdir -p "$prefix/protoc"
python3 -I -m zipfile -e "$pb/protoc-36.2-linux-x86_64.zip" "$prefix/protoc"
chmod +x "$prefix/protoc/bin/protoc"
"$prefix/protoc/bin/protoc" --version | tee "$prefix/logs/versions.txt"
"$prefix/bin/grpc_cpp_plugin" --version >> "$prefix/logs/versions.txt" 2>&1 || true
ls "$prefix/lib" | grep -E "^lib(grpc\+\+|grpc|protobuf|cares|re2)\.a$" | tee -a "$prefix/logs/versions.txt"
