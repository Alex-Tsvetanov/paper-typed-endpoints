# P5 third-party pins (Lab requests 1 to 3)

Spec only, written 2026-10-29. Nothing has been downloaded. Each file is downloaded into a fresh
empty folder and checked against the sha256 below before use, as `lab/laptop/versions.md` does.
Digest sources: the GitHub release asset's published digest (API field `digest`), go.dev's
download JSON for Go, and sum.golang.org for Go modules.

## Lab request 1: protobuf v36.2 (protoc and the C++ runtime source)

Newest stable release when pinned (published 2026-09-17; v37.0 is still a release candidate).
Arm P builds the C++ runtime from source with the same compiler and flags as arm R; `protoc` is
the release binary.

| File | Bytes | sha256 | URL |
|---|---|---|---|
| protoc-36.2-linux-x86_64.zip | 3,681,545 | 121f6c7afe1d4d0e3ea6aab9432038599250134cbf4474cb1167d2c7decd4278 | https://github.com/protocolbuffers/protobuf/releases/download/v36.2/protoc-36.2-linux-x86_64.zip |
| protoc-36.2-win64.zip | 3,565,086 | f0c128dc0d8492eceece83bb459a4c0e316764b929ffbf1aa416357fd644edd3 | https://github.com/protocolbuffers/protobuf/releases/download/v36.2/protoc-36.2-win64.zip |
| protobuf-36.2.tar.gz (source) | 7,339,736 | 3d9642a662d10e68ebae5e53f14dcce5105684212d5078f8e0d47d1ab3ae6b64 | https://github.com/protocolbuffers/protobuf/releases/download/v36.2/protobuf-36.2.tar.gz |
| abseil-cpp-20250512.1.tar.gz (source) | 2,221,441 | 9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db | https://github.com/abseil/abseil-cpp/releases/download/20250512.1/abseil-cpp-20250512.1.tar.gz |

abseil-cpp 20250512.1 is the version protobuf v36.2's own CMake build declares
(`cmake/dependencies.cmake` at tag v36.2, `abseil-cpp-version "20250512.1"`); the tag does not
vendor it. Build abseil first and point protobuf at it (`protobuf_ABSL_PROVIDER=package`).

## Lab request 2: Go 1.27.2 and the Go modules

Go 1.27.2 per Alex's decision D2 (2026-10-23), the same as P4 and P7.

| File | Bytes | sha256 | URL |
|---|---|---|---|
| go1.27.2.linux-amd64.tar.gz | 70,590,635 | ecbadb99091a3f46e31f5f934b068b1864eafa7995211b39eaddf76996045fe5 | https://go.dev/dl/go1.27.2.linux-amd64.tar.gz |
| go1.27.2.windows-amd64.zip | 79,008,081 | 1314008898bd40df77af4b014f777f08873dbdfbcd3d92308728ee03304fe04f | https://go.dev/dl/go1.27.2.windows-amd64.zip |

Go modules are pinned by `go.sum` in the interop programs, checked against sum.golang.org:

| Module | Version | go.sum hashes |
|---|---|---|
| google.golang.org/grpc | v1.84.0 (2026-09-17; v1.85.0 of 2026-10-27 is too new to pin) | `h1:soMyaPJ8pAak5PIQ0DGBUir0XRo2fRoMqhNWMLlLxO0=`, go.mod `h1:ljCht0DrxQrXBDRTZp52Qxh3Ffk8CdYm2sj4O2QN2C0=` |

ghz, the interop and cross-check client (Alex's decision D-L), release v0.121.0 (2025-12-07):

| File | Bytes | sha256 | URL |
|---|---|---|---|
| ghz-linux-x86_64.tar.gz | 18,743,394 | 9ae3b93f2c46dac9136e29e81b4a1de8d4e56f092a6fe46884a25c9c83cb2324 | https://github.com/bojand/ghz/releases/download/v0.121.0/ghz-linux-x86_64.tar.gz |
| ghz-windows-x86_64.zip | 18,613,210 | 80329e20b6fe6ccf1cda7c6fe454b2611d12298d6056d067f2229b57d8b151ca | https://github.com/bojand/ghz/releases/download/v0.121.0/ghz-windows-x86_64.zip |

## Lab request 3: nghttp2 v1.70.0 (source, for the transport and h2load)

Newest release (2026-07-29) and the same version as the image's `libnghttp2` 1.70.0. h2load needs
the tools build (`--enable-app`) with its dependencies (OpenSSL, libev, c-ares); the transport
links only the library.

| File | Bytes | sha256 | URL |
|---|---|---|---|
| nghttp2-1.70.0.tar.xz | 2,018,996 | e05cb1388eaca3830aded4ccf20044b6e1ac1a61411dcca11b0437c4285c8bc2 | https://github.com/nghttp2/nghttp2/releases/download/v1.70.0/nghttp2-1.70.0.tar.xz |

The release also publishes a PGP signature (`nghttp2-1.70.0.tar.xz.asc`); checking it needs the
maintainer's key, which is not pinned here.

h2load (the Linux load generator, Alex's decision Q1 of 2026-10-30) is built from this tarball with
`--enable-app`, which needs OpenSSL, zlib and libev. Arch's base system has OpenSSL and zlib; libev
is built from source into `~/opt`, so the laptop's packages stay as they are (no pacman change):

| File | Bytes | sha256 | URL |
|---|---|---|---|
| libev-4.33.tar.gz | 569,527 | 507eb7b8d1015fbec5b935f34ebed15bf346bed04a11ab82b8eee848c4205aea | https://dist.schmorp.de/libev/Attic/libev-4.33.tar.gz |

libev 4.33 is the newest release. Digest sources, checked 2026-10-30: Homebrew's formula API
(`formulae.brew.sh/api/formula/libev.json`, field `checksum`) and FreeBSD ports
(`devel/libev/distinfo`, which also gives the size). The upstream site publishes no digest.

## Lab request 5 (draft): gRPC C++ for arm G (M2) and oha (M3)

Spec only, written 2026-11-03; nothing is downloaded until Alex answers Q20261103-p5-3
(`lab/pipeline/questions.md` in Papers). Every digest below was read from its source on 2026-11-03.

gRPC v1.84.2 (tag commit 41ee9aad18b902ea75a6ff5505d71dce6082613f, not a prerelease, no release
assets) is the newest release whose `third_party/abseil-cpp` is abseil 20250512.1 (submodule
76bb24329e8bf5f39704eb10d21b9a80befa7c81, the commit of that tag), P5's pin. Its
`third_party/protobuf` is protobuf v35.1; P5 builds it against protobuf 36.2 with
`gRPC_PROTOBUF_PROVIDER=package` and `gRPC_ABSL_PROVIDER=package` (gRPC's CMake asks for
`find_package(... CONFIG)` with no version). That this builds is not yet shown; it is checked first
in papers-dev, untimed. v1.85.x needs abseil 20260526.0. OpenSSL and zlib: the laptop's Arch
packages (`gRPC_SSL_PROVIDER=package`, openssl 3.6.4-1 as recorded by request 24), as for h2load.

| File | Bytes | sha256 | URL | Digest source |
|---|---|---|---|---|
| grpc-1.84.2.tar.gz (tag archive) | not published | f1a923286c7eb1d13ca486a0c24cb6332f8fc64311d8b7c77d6dabdea0920f23 | https://github.com/grpc/grpc/archive/refs/tags/v1.84.2.tar.gz | Bazel Central Registry `modules/grpc/1.84.2/source.json` (integrity, SRI base64) |
| c-ares-1.34.9.tar.gz | 1,083,895 | 9364b70a12eb59a06830fb488d9357f90100dff0d98b23ea3593a183d43e6f2f | https://github.com/c-ares/c-ares/releases/download/v1.34.9/c-ares-1.34.9.tar.gz | release asset `digest` |
| re2-2025-11-05.tar.gz | 397,504 | 87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67 | https://github.com/google/re2/releases/download/2025-11-05/re2-2025-11-05.tar.gz | release asset `digest` |

c-ares 1.34.9 is a patch release after the 1.34.5 gRPC's submodule pins; re2 2025-11-05 declares
abseil 20250512.1 in its own `MODULE.bazel`. gRPC's `CMakeLists.txt` at v1.84.2 fetches six archives
of protocol definitions at configure time unless their folders exist, each with the sha256 it pins
(lines 404 to 463); P5 downloads them beforehand and places them in those folders:

| Folder under `third_party/` | sha256 | URL |
|---|---|---|
| cel-spec | 13583c5a312861648449845b709722676a3c9b43396b6b8e9cbe4538feb74ad2 | https://github.com/google/cel-spec/archive/refs/tags/v0.25.1.tar.gz |
| envoy-api | ed5e6c319f8ebcdf24a9491f866a599bb9a3c193b859a94ad13bd31f85b46855 | https://github.com/envoyproxy/data-plane-api/archive/6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz |
| googleapis | 07a6f700cee5ca796f5230876ed8454bf1d8cc409b3b3e634505bc4f32a9f72b | https://github.com/googleapis/googleapis/archive/2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.zip |
| grpc-proto | 5e9b520b22afbd53a662cc29017064be253c1dfa6df8958738594e7fec6ade33 | https://github.com/grpc/grpc-proto/archive/ec30f589e2519d595688b9a42f88a91bdd6b733f.tar.gz |
| protoc-gen-validate | e4718352754df1393b8792b631338aa8562f390e8160783e365454bc11d96328 | https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.2.1.tar.gz |
| xds | 49535f3c3370004309da50194c09bbfc528d4702424dd46e7d56a278a3dfc15d | https://github.com/cncf/xds/archive/ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz |

oha v1.16.0 (2026-08-23, MIT), the M3 candidate: a Rust load generator whose HTTP/2 client is hyper's
h2, built by its release workflow for x86_64-unknown-linux-musl.

| File | Bytes | sha256 | URL | Digest source |
|---|---|---|---|---|
| oha-linux-amd64 | 13,429,032 | 620bb9e16fb53eabc9a3fc45f88bdb41fefa3fee5c05e75892011ce320391716 | https://github.com/hatoo/oha/releases/download/v1.16.0/oha-linux-amd64 | release asset `digest` |

Its README lists `--http2`, `-m POST`, `-D <file>` (the body, raw bytes), `-H`, `-c` connections and
`-p` streams per connection, `-z` duration, `--worker-threads`, `--no-tui` and JSON output. It does
not read gRPC's trailers, so the server log's failed count (which counts every non-OK grpc-status)
is the check of each call.

## Not yet pinned

- gRPC C++ for the reference arm G (Alex's decision D-G: built after arms R and P): drafted as
  Lab request 5 above.
- The system OpenSSL and zlib that h2load links on Arch: their versions are recorded by the job
  (`pacman -Q openssl zlib`), not pinned. Windows runs use ghz instead (Alex, 2026-10-30,
  `design/revision-log.md`), since a MinGW build of nghttp2 is the library only.
