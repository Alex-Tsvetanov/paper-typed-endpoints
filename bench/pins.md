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

## Not yet pinned

- gRPC C++ for the reference arm G (Alex's decision D-G: built after arms R and P).
- libev, c-ares and OpenSSL for the h2load tools build on Arch: the system packages; pin them when
  the h2load build is scripted. Windows runs use ghz instead (Alex, 2026-10-30,
  `design/revision-log.md`), since a MinGW build of nghttp2 is the library only.
