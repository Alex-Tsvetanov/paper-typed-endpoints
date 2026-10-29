# Request: GCC 16.2.0 from source on Linux (laptop's Arch and the dev image)

Alex, 2026-10-29, to "Declare the gap, or pin 16.2.0 on both?": "Pin 16.2.0 on both". Windows
already uses GCC 16.2.0 (WinLibs MinGW-w64, `STATE.md`); this request builds the same release
on Linux, so both OSes compile with one exact GCC version.

## Source

| File | Bytes | Published checksum | URL |
|---|---|---|---|
| gcc-16.2.0.tar.xz | 107,200,820 | sha512 c51c30ca7422d0cbecf504b2e0f33c3aca31e0f90a76b65217f465163fa6fa17b3f5de39e145c47e5bab90ac0ce7fff3b03c8d553ae36e01faaea5a50f8648d1 | https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz |

GNU publishes sha512, not sha256, for GCC releases: the value above is the line for
`gcc-16.2.0.tar.xz` in https://gcc.gnu.org/pub/gcc/releases/gcc-16.2.0/sha512.sum (read
2026-10-29). Check it with `sha512sum`. A GPG signature exists
(`gcc-16.2.0.tar.xz.sig`); its key is not pinned here. Download on the desktop into a fresh
empty folder, check, copy to the laptop, check again (as `lab/laptop/versions.md` does).

## Laptop's Arch

1. Prerequisites, read-only (D1: no `pacman` install or upgrade): `pacman -Q gmp mpfr libmpc isl
   zlib make binutils`. All are dependencies of the installed gcc package. If one is missing,
   stop and report; do not install.
2. Build, as the lab user, outside the source tree:

```
mkdir -p ~/opt/_src ~/opt/_build/gcc-16.2.0
tar -xf gcc-16.2.0.tar.xz -C ~/opt/_src
cd ~/opt/_build/gcc-16.2.0
~/opt/_src/gcc-16.2.0/configure \
  --prefix=$HOME/opt/gcc-16.2.0 \
  --enable-languages=c,c++ \
  --enable-bootstrap \
  --enable-checking=release \
  --disable-multilib \
  --disable-werror \
  --disable-libstdcxx-pch \
  --enable-threads=posix \
  --enable-shared \
  --enable-default-pie \
  --enable-default-ssp \
  --enable-linker-build-id \
  --with-system-zlib
make -j14 > build.log 2>&1
make install-strip > install.log 2>&1
```

Why these options:
- `--enable-bootstrap` (the default, written out): GCC builds itself in three stages and compares
  stages 2 and 3, so the result does not depend on the host GCC 16.2.1, which has defect F1.
- `--enable-checking=release`: as in releases and in Arch's gcc; extra checking would slow the
  compiler and distort the compile curves.
- C and C++ only, no multilib. libsanitizer stays, because the D5 records of the reflection layer
  use GCC's ASan, UBSan and TSan.
- PIE and SSP on by default, as in Arch's system gcc, so default code generation does not change.
- `-j14` leaves 2 of the 16 hardware threads free.

3. Checks; the job's outputs. All must pass:

```
~/opt/gcc-16.2.0/bin/g++ --version          # first line ends in 16.2.0
cd <P5 repository at the commit named in the queue entry>
~/opt/gcc-16.2.0/bin/g++ -std=c++26 -freflection -static-libstdc++ -static-libgcc \
  probes/define_aggregate_probe.cpp -o /tmp/dap && /tmp/dap
# expected: define_aggregate: ok, __cpp_impl_reflection 202603
cmake -S . -B build/gcc-16.2.0 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=$HOME/opt/gcc-16.2.0/bin/g++
cmake --build build/gcc-16.2.0 -j14 && ctest --test-dir build/gcc-16.2.0 --output-on-failure
# expected: 14 of 14 pass
```

Also run F1's reproduction (`design/compiler-findings.md`) at `-O0` and `-O2` with the new `g++`
and keep the output: it says whether Linux 16.2.0 behaves as MinGW 16.2.0 (3 at `-O2`).

4. Expected build time: no measured basis. No GCC has been built on this laptop, and the desktop
   takes no timings. The job records its own wall time, which becomes the basis for any
   rebuild. Disk use of the build tree is also unmeasured; after `install-strip`, keep
   `build.log` and `install.log` and delete `~/opt/_build/gcc-16.2.0`.

## How P5's builds find it

- Every P5 build on Arch names the compiler by absolute path:
  `-DCMAKE_CXX_COMPILER=$HOME/opt/gcc-16.2.0/bin/g++`. Nothing on PATH changes, and Arch's gcc
  stays the system compiler for everyone else.
- Timed (Release) builds link with `-static-libstdc++ -static-libgcc`. The measured program then
  carries the pinned 16.2.0 runtime, so the loader cannot pick Arch's libstdc++ (16.2.1) instead.
  Windows does the same with WinLibs, where `libstdc++-6.dll` would otherwise come from PATH,
  so both OSes measure one compiler and one runtime. rpath has no Windows counterpart.
- Sanitizer and test builds link with `-Wl,-rpath,$HOME/opt/gcc-16.2.0/lib64`, so the dynamic
  sanitizer runtimes (libasan, libubsan, libtsan) and libstdc++ come from the pinned GCC too.
- P5 adds CMake presets for these two cases when the build lands.

## Dev image (desktop, development only, no timing)

Same tarball, same checksum, same configure line with `--prefix=/home/dev/opt/gcc-16.2.0` and
`make -j4` (the desktop's cores are shared). Build it in a `papers-dev` container into a host
folder mounted at that path, so the image itself does not change:

```
docker run --rm --cpus 4 --user dev \
  -v D:\Dev\toolchains\gcc-16.2.0-linux:/home/dev/opt/gcc-16.2.0 \
  -v <folder holding the checked tarball>:/src:ro \
  papers-dev:arch-2026-10-23 bash -c '<the steps of section "Laptop's Arch", 1 to 3, with -j4>'
```

Later P5 containers mount the same folder at `/home/dev/opt/gcc-16.2.0` and use that `g++`. The
alternative, adding GCC 16.2.0 to `lab/dev/Dockerfile`, changes the shared image and is the
environment agent's call; the mounted folder needs no image change.
