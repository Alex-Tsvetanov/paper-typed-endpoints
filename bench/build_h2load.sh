#!/usr/bin/env bash
# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
#
# Builds h2load, the Linux load generator (Alex's decision Q1 of 2026-10-30), from the pinned
# nghttp2 1.70.0 with its applications: libev 4.33 first, static, into <prefix>/libev, then nghttp2's
# applications against it and against the system's OpenSSL and zlib, with the pinned GCC 16.2.0 and
# a static libstdc++. Everything goes under <prefix>; no system package is installed or changed
# (bench/pins.md). The system packages it links are recorded in <prefix>/logs/packages.txt.
#
# Usage: build_h2load.sh <nghttp2 download folder> <libev download folder> <install prefix> [jobs]
# The compiler is $GCC/bin/gcc and g++ (default ~/opt/gcc-16.2.0). Logs go to <prefix>/logs.
set -euo pipefail
ng_src=$(cd "$1" && pwd)
ev_src=$(cd "$2" && pwd)
prefix=$3
jobs=${4:-4}
gcc_root=${GCC:-$HOME/opt/gcc-16.2.0}

(cd "$ng_src" && sha256sum -c --strict) <<'EOF'
e05cb1388eaca3830aded4ccf20044b6e1ac1a61411dcca11b0437c4285c8bc2  nghttp2-1.70.0.tar.xz
EOF
(cd "$ev_src" && sha256sum -c --strict) <<'EOF'
507eb7b8d1015fbec5b935f34ebed15bf346bed04a11ab82b8eee848c4205aea  libev-4.33.tar.gz
EOF

mkdir -p "$prefix/logs"
trap 'echo "build_h2load.sh: failed; logs in $prefix/logs" >&2' ERR
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pacman -Q openssl zlib > "$prefix/logs/packages.txt"

tar -xzf "$ev_src/libev-4.33.tar.gz" -C "$work"
(cd "$work/libev-4.33" &&
  CC="$gcc_root/bin/gcc" CFLAGS="-O2" ./configure --prefix="$prefix/libev" --disable-shared --enable-static \
    > "$prefix/logs/libev-configure.log" 2>&1 &&
  make -j "$jobs" > "$prefix/logs/libev-build.log" 2>&1 &&
  make install > "$prefix/logs/libev-install.log" 2>&1)

tar -xJf "$ng_src/nghttp2-1.70.0.tar.xz" -C "$work"
# nghttp2 links every optional library it finds into its applications; none is looked for, so
# h2load depends on OpenSSL, zlib and libev only, whatever else the system has.
no_optional=()
for pkg in Libcares Libbrotlienc Libbrotlidec Libngtcp2 Libnghttp3 Libbpf Systemd Jansson Libevent LibXml2 \
  Jemalloc WolfSSL Libngtcp2_crypto_quictls Libngtcp2_crypto_libressl Libngtcp2_crypto_wolfssl \
  Libngtcp2_crypto_ossl; do
  no_optional+=("-DCMAKE_DISABLE_FIND_PACKAGE_$pkg=ON")
done
cmake -S "$work/nghttp2-1.70.0" -B "$work/b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_COMPILER=$gcc_root/bin/gcc" "-DCMAKE_CXX_COMPILER=$gcc_root/bin/g++" \
  "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" \
  "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" \
  -DENABLE_APP=ON -DENABLE_HPACK_TOOLS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_HTTP3=OFF -DENABLE_DOC=OFF \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DWITH_LIBXML2=OFF -DWITH_JEMALLOC=OFF \
  "-DLIBEV_INCLUDE_DIR=$prefix/libev/include" "-DLIBEV_LIBRARY=$prefix/libev/lib/libev.a" \
  "${no_optional[@]}" "-DCMAKE_INSTALL_PREFIX=$prefix" > "$prefix/logs/nghttp2-apps-configure.log" 2>&1
cmake --build "$work/b" -j "$jobs" --target h2load > "$prefix/logs/nghttp2-apps-build.log" 2>&1
install -D -m 755 "$work/b/src/h2load" "$prefix/bin/h2load"
"$prefix/bin/h2load" --version
