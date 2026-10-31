# Request: an uninstrumented libc++ 22.1.8 for the clang records (Laptop request 19)

Plain: put a plain (not sanitizer-instrumented) libc++ 22.1.8 under `~/opt/libcxx-22.1.8` on the
laptop's Arch, so that the clang ASan and TSan records of Laptop request 16 can build. Untimed. No
`pacman` (decision 7), no download, no build.

Why: the dry run of Laptop request 16 (A45, 2026-10-31, at 701fe83) made the clang ASan and TSan
records red with `build_exit 1` after about 1 s. CMake's compiler check failed at link: the presets
pass `-stdlib=libc++`, and the laptop has no `/usr/lib/libc++.so*` (Arch's `clang` package does not
ship libc++; the only libc++ on the laptop is the MSan-instrumented one in `~/opt/libcxx-msan-gcc`,
which ASan and TSan builds cannot use). The GCC ASan, GCC TSan and clang MSan records were green.

## The libc++

Arch's packages `libc++ 22.1.8-1` and `libc++abi 22.1.8-1` (built by Arch on 2026-06-21 from LLVM
22.1.8, the release of the laptop's `clang 22.1.8-1`), as installed in the desktop's dev image
`papers-dev:arch-2026-10-23`. That is the libc++ every desktop clang build of P5 has used.
`pacman -Qkk` in the image reports 0 altered files for both packages. The files are copied as they
are into a prefix:

- `include/c++/v1/` (owned by libc++ and libc++abi only), `lib/` (`libc++.so`, a linker script
  `INPUT(libc++.so.1 -lc++abi)`, `libc++.so.1 -> libc++.so.1.0`, `libc++abi.so -> libc++abi.so.1 ->
  libc++abi.so.1.0`, the static archives, `libc++.modules.json`), `share/libc++/v1/`, the licenses;
- `PROVENANCE.txt` (the packages' `pacman -Qi` fields, the `pacman -Qkk` result, the image's glibc)
  and `MANIFEST.sha256` (sha256 of each of the 1,824 files).

Runtime needs: `libc.so.6` and `libgcc_s.so.1` only; the highest glibc symbol version is
`GLIBC_2.38` (`libc++.so.1.0`).

On the desktop: `C:\Users\alext\opt\_dl\p5-libcxx-22.1.8\libcxx-22.1.8-arch-1.tar.xz`, 1,671,512 bytes,
sha256 `67aad2ef8f4510700668df10b7325fa0e328a68aa9be1cd6d6ed800905324007` (also in the `.sha256`
file next to it). Made by `tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner | xz -9`.
The tarball holds three symbolic links, so it is extracted on Linux only (Windows tar refuses them).

## Steps (laptop's Arch, untimed)

1. Copy the tarball and check its sha256 against the value above.
2. Check `ldd --version` reports glibc 2.38 or newer, and `~/opt/libcxx-22.1.8` does not exist.
3. `tar -C ~/opt -xJf libcxx-22.1.8-arch-1.tar.xz`, then `(cd ~/opt/libcxx-22.1.8 && sha256sum -c --quiet
   MANIFEST.sha256)` prints nothing and exits 0.

Never touch `~/opt/libcxx-msan-gcc`, `~/opt/libcxx-msan` or anything under `~/src`: the MSan records
of P1 to P4 and P5 use them.

## The presets

`linux-clang-asan` and `linux-clang-tsan` (and `linux-clang-fuzz`, which inherits ASan) now name the
prefix as `linux-clang-msan` names its own (P5 commit of this request): `-isystem
$HOME/opt/libcxx-22.1.8/include/c++/v1` when compiling, `-L$HOME/opt/libcxx-22.1.8/lib -Wl,-rpath,...
-lc++abi` when linking. On the desktop, in the dev image with its own libc++ removed and this
tarball extracted under the user's `~/opt`, both presets configured, built, and passed 8 of 8 tests
(`reflection_layer` the declared skip); `test_wire`'s RUNPATH is the prefix's `lib`, and `ldd`
resolves `libc++.so.1` and `libc++abi.so.1` there.

TSan with an uninstrumented libc++ is the usual practice and what the presets have done on the
desktop; libc++'s own code is not checked by these records, only P5's.

## Then: a second dry run of the clang records

After this request, run Laptop request 16's dry run again for the three clang records only
(`linux-clang-asan`, `linux-clang-tsan`, `linux-clang-msan`), at the newest P5 commit, before the
freeze, so a first finding under clang ASan or TSan on the laptop shows while it can be fixed.
Pass: all three green, 8 tests each with `reflection_layer` the only skip, 0 reports, and each with
a non-null `inputs_hash_file` from the clang inputs rule of `sanitizer-records.md`.

## Expected time

Minutes: a copy, a check and an extraction. The second dry run: clang MSan took 18 s on the
laptop in A45; ASan and TSan build and run the same 8 tests.
