# P5 related work survey

Status: checked claim by claim on 2026-10-30 by a second agent (model Opus); see the change log at the end.

Gathered on 2026-10-30 by an AI agent (Claude Code, model Sonnet 5.5). The second agent opened every source again, corrected the file in place, resolved what it could, and added the items its own searches found. Added items say so in their heading.

Paper: P5, typed endpoints. C++26 static reflection (P2996, GCC 16 with `-freflection`) generates protobuf message types, the binary codec and unary gRPC endpoints at compile time from `.proto` text read with `#embed`, with no protoc run.

## How to read this file

- Every source listed was opened on 2026-10-30 by the gatherer, except where an item says it was not opened, and re-opened or queried through an API equivalent (GitHub REST API, raw file, doi.org metadata, OpenAlex or Semantic Scholar) by the checker. Each item lists its URLs with that read date.
- Each item ends with a line "Checked 2026-10-30". It says what the checker confirmed and names the change-log entries that touch the item (C = correction, R = removal, A = addition, M = unverified marker resolved).
- GitHub facts (license, release, tag, push date) come from the GitHub REST API, for example `https://api.github.com/repos/OWNER/REPO` (license, pushed_at), `.../releases/latest` and `.../tags`. "Latest release" means the GitHub releases entry. "No GitHub release" means `releases/latest` returned 404; then the latest tag and the date of its tagged commit are given. That commit date is not a release date. The checker re-queried every repository on 2026-10-30.
- "Self-reported" marks a number or claim taken from the vendor's own README, docs or paper. Neither agent reproduced it.
- The marker "unverified" in round brackets marks a claim that no opened source confirms. After the check, 5 such markers remain. Count them with `grep -o "(unverifie[d])" related-work-survey.md | wc -l`; that command line does not match itself.
- Paraphrase is the default. A few short quotes (under 15 words, one per source at most) are marked with quotation marks.
- DOIs: the gatherer found each DOI in a Crossref record (`https://api.crossref.org/works/<doi>`) whose title, authors and venue match. The checker resolved each DOI at `https://doi.org/` (all returned a redirect to the publisher) and compared the CSL metadata returned by doi.org with the title and authors given here. Abstracts were read through OpenAlex (`https://api.openalex.org/works/doi:<doi>`) or, where OpenAlex held none or only part, through Semantic Scholar (`https://api.semanticscholar.org/graph/v1/paper/DOI:<doi>`). Items without a DOI say "DOI: not found".
- Search was by GitHub search, GitHub code search, web search and Crossref. A negative result is "not found in searches on 2026-10-30", never "does not exist". The queries of both agents are listed in 7.4.
- The predecessor repository Alex-Tsvetanov/Compile-time-Protobuf (ctpb) surfaced in code search and is excluded by the task.

---

## 1. C++ protobuf implementations and alternatives

### 1.1 Google protobuf (C++ code generation and TcParser)

- Sources (read 2026-10-30): https://github.com/protocolbuffers/protobuf ; https://api.github.com/repos/protocolbuffers/protobuf/releases/latest ; https://github.com/protocolbuffers/protobuf/blob/main/LICENSE ; https://github.com/protocolbuffers/protobuf/blob/main/src/google/protobuf/generated_message_tctable_decl.h ; https://github.com/protocolbuffers/protobuf/blob/main/src/google/protobuf/generated_message_tctable_impl.h ; https://blog.reverberate.org/2021/04/21/musttail-efficient-interpreters.html
- What it is: the reference implementation. The `protoc` compiler reads `.proto` files and emits C++ (`.pb.h`, `.pb.cc`) through a code generator.
- How it works: the header `generated_message_tctable_decl.h` says it holds declarations needed in generated headers for messages that use tail-call table parsing, and that everything in it is internal. `generated_message_tctable_impl.h` describes "table driven, tail call optimized" parse functions and uses `PROTOBUF_MUSTTAIL` to dispatch from one field parser to the next. A source comment says the field-to-field call is only safe when the compiler makes it a true tail call, that Clang and GCC do so in optimized mode but not in debug mode, and that the enclosing parse loop serves as a trampoline otherwise.
- Reads `.proto`: yes, through protoc. Hand-written declarations: no.
- gRPC: protobuf itself has no RPC transport. See 1.7 for gRPC C++.
- Latest release: v36.2, published 2026-09-17T17:33:01Z (GitHub API). A tag v37.0-rc1 also exists.
- License: the LICENSE file text is a BSD 3-clause style license (it contains the "Neither the name of Google Inc." clause). The GitHub API reports `NOASSERTION` for the repository. Both facts are stated here on purpose.
- Published measurements: no published compile-time measurement of generated `.pb.cc` files was found. Josh Haberman's post (2021-04-21) reports protobuf parsing above 2 GB/s with Clang `musttail` tail calls. The post says several techniques produced the speedup, not tail calls alone. It says the parser was submitted to upb, the C implementation, and had not then been implemented in the C++ version of protobuf (self-reported).
- Relevance to P5: this is the baseline arm. Its parser is table-driven (the header defines `TcFieldData` with constexpr constructors and a message-level parse table base class for the tail-call parser; whether generated tables are `constinit` was not checked (unverified)), so P5's reflection-generated codec should be compared against TcParser speed, not against an unoptimized parser. The v36.2 release notes also list Rust work (see 3.1).
- Checked 2026-10-30: release, tag, license text, both header comments and the Haberman post confirmed. Wording on the post refined (C1). The `constinit` marker stays: a grep of the checked-in `descriptor.pb.cc` found no parse-table declaration to inspect.

### 1.2 nanopb

- Sources (read 2026-10-30): https://github.com/nanopb/nanopb ; https://api.github.com/repos/nanopb/nanopb/releases/latest ; https://github.com/nanopb/nanopb/blob/master/README.md ; https://github.com/nanopb/nanopb/blob/master/docs/index.md
- What it is: a Protocol Buffers implementation in ANSI C for microcontrollers and other memory-restricted systems.
- How it works: code generation. A Python script (`nanopb_generator.py`) turns a `.proto` file into `.pb.c` and `.pb.h` using protoc; the runtime is `pb_encode.c`, `pb_decode.c` and `pb_common.c`. Generated message descriptors are `pb_msgdesc_t` structures. An `.options` file customizes generation.
- Reads `.proto`: yes (through protoc, which the binary packages bundle).
- gRPC: no gRPC support is described. The README and the docs index name the Python package `grpcio-tools` only as a dependency to install with pip.
- Latest release: nanopb-0.4.9.2, published 2026-08-25T10:49:13Z.
- License: Zlib (GitHub API).
- Published measurements (self-reported): the docs claim code size of 5 to 20 kB depending on processor and options, plus message definitions, and about 1 kB of stack. No compile-time figures.
- Relevance to P5: shows the generator-centred design at the small end. It is C, so it is not a C++ compile-time comparison point.
- Checked 2026-10-30: confirmed, except the gRPC sentence, which was corrected (C2).

### 1.3 protozero

- Sources (read 2026-10-30): https://github.com/mapbox/protozero ; https://api.github.com/repos/mapbox/protozero/releases/latest ; https://github.com/mapbox/protozero/blob/master/README.md ; https://api.github.com/repos/mapbox/protozero/contents/bench
- What it is: a minimal C++14 protobuf encoder and decoder. (The repository `dloebl/protozero` named in the task returns 404; `mapbox/protozero` is the live one.)
- How it works: low-level reader and writer classes. The README says the library does not read `.proto` files. The developer translates the schema into code by hand, so field names, enum names, default values and field types are not available to the library.
- gRPC: no.
- Latest release: v1.8.2, published 2026-06-30T18:04:37Z.
- License: BSD-2-Clause (GitHub API).
- Published measurements: the `bench` directory holds only a `data` subdirectory; no benchmark results were found in it. The README states it is designed for high performance and zero-copy parsing, without numbers.
- Relevance to P5: the opposite of P5. All schema knowledge lives in hand-written C++.
- Checked 2026-10-30: confirmed; the `bench` directory was listed (M1).

### 1.4 Embedded Proto

- Sources (read 2026-10-30): https://github.com/Embedded-AMS/EmbeddedProto ; https://github.com/Embedded-AMS/EmbeddedProto/blob/master/README.md ; https://api.github.com/repos/Embedded-AMS/EmbeddedProto/tags ; https://github.com/Embedded-AMS/EmbeddedProto/blob/4.0.0b7/setup.py
- What it is: a C++ protobuf implementation for microcontrollers, a product of Embedded AMS B.V.
- How it works: a plugin for protoc generates C++ that uses no dynamic memory allocation. The lengths of repeated, string and bytes fields can be fixed from the `.proto` file with options. The README changelog for 3.6.0 says that release moved to Protobuf 32.0 and raised the minimum Python version to 3.10.
- Reads `.proto`: yes (protoc plugin). gRPC: not mentioned in the README.
- Release: no GitHub release. Latest stable-looking tag is 3.6.2, tagged commit dated 2026-08-20. A tag 4.0.0b7 exists, tagged commit dated 2026-10-19. `setup.py` at that tag accepts PEP 440 version strings with an a, b or rc suffix and mentions beta builds made by CI.
- License: dual. The README says GPL v3.0 for open-source use and a paid commercial license otherwise. The GitHub API reports no license.
- Published measurements: none seen.
- Relevance to P5: another generator-based design for a niche. No compile-time or gRPC angle.
- Checked 2026-10-30: confirmed; the Python and protobuf versions were restated as changelog facts (C3); the beta question was settled from `setup.py` (M2).

### 1.5 protopuf

- Sources (read 2026-10-30): https://github.com/PragmaTwice/protopuf ; https://protopuf.surge.sh ; https://github.com/PragmaTwice/protopuf/blob/master/README.md ; https://api.github.com/repos/PragmaTwice/protopuf/tags ; https://github.com/ArchNemSyS/proto-to-protopuf
- What it is: Protocol Puffers, a header-only, highly templated, protobuf-compatible serialization library for C++20.
- How it works: the user describes messages as C++ types, for example `message<uint32_field<"id", 1>, string_field<"name", 3>>`, using class-type non-type template parameters. The README says it does extensive compile-time work to improve run-time speed.
- Reads `.proto`: no. The README lists describing data "using type in C++ instead of DSLs like the Protocol Buffer Language". A separate repository, `ArchNemSyS/proto-to-protopuf` (MIT, created 2024-04-21, last push 2025-06-14, 2 stars), is a standalone parser program that converts proto source files into protopuf headers. Its README lists proto2 only and several parsing limits. It runs as an external tool, not in the compiler.
- gRPC: no.
- Release: no GitHub release. Latest tag v3.1.0, tagged commit dated 2025-05-07; the repository was last pushed 2026-10-02.
- License: Apache-2.0 (GitHub API).
- Published measurements: none. The documentation site and README show no benchmarks or build-time figures.
- Relevance to P5: a well-known (209 stars) C++20 library that derives protobuf types from templates. The schema is C++ text, not `.proto` text.
- Checked 2026-10-30: confirmed; the documentation site was read in full; proto-to-protopuf was opened (M3); the "earliest" claim was removed (R1).

### 1.6 hpp-proto

- Sources (read 2026-10-30): https://github.com/huangminghuang/hpp-proto ; https://github.com/huangminghuang/hpp-proto/blob/main/README.md ; https://github.com/huangminghuang/hpp-proto/blob/main/benchmarks/ReadMe.md ; https://github.com/huangminghuang/hpp-proto/blob/main/docs/grpc-adapter.md ; https://github.com/huangminghuang/hpp-proto/blob/main/docs/Code_Generation_Guide.md ; https://api.github.com/repos/huangminghuang/hpp-proto/releases
- What it is: a C++23 protobuf implementation with generated aggregate message types, binary and JSON (through glaze) codecs, a gRPC adapter and dynamic messages.
- How it works: code generation by `protoc-gen-hpp`, a protoc plugin invoked from CMake with `protobuf_generate_hpp`. The guide says the plugin relies on protoc for descriptor structure and validation. Generated message types take a `Traits` template parameter that swaps containers and allocators. The README lists `consteval` and concepts among the C++23 features used.
- Reads `.proto`: yes, through protoc. gRPC: yes, an adapter over the gRPC C++ asynchronous API that wraps generated `*.service.hpp` descriptors. `hpp_proto::grpc::Stub` offers a blocking `call()` for unary RPCs; `hpp_proto::grpc::CallbackService` wires handlers into a `grpc::ServerBuilder`; callback reactors cover streaming.
- Release: latest stable release v0.10.0, published 2025-03-23. A pre-release v1.0.0-beta.1 was published 2026-01-02 (tagged commit f514a0a, dated 2026-01-02).
- License: MIT (GitHub API).
- Published measurements (self-reported): benchmarks against Google protobuf 33.2 on Linux (Ubuntu 24.04.2, Intel i9-11950H, gcc 13.3.0) and macOS (Apple M1 Pro). Dataset: Google's protobuf 3.6.0 benchmark `.proto` and data. Linux numbers: deserialize, proto3: Google 251 ns, hpp-proto 212 ns (regular mode); 252 ns versus 160 ns (arena versus non-owning). Serialize-only, proto2: Google 64.5 ns, hpp-proto 85.1 ns, so Google is faster there. Code-size charts exist. The benchmark page has runtime and code-size sections only, no compile-time section.
- Relevance to P5: the closest C++ library that is complete (messages, codec, gRPC adapter), with a published runtime comparison against protoc-generated code. It still needs protoc at build time. It publishes no compile-time data, which is the gap P5 fills.
- Checked 2026-10-30: confirmed; the numbers are labelled as the Linux figures and the adapter description was made precise (C4).

### 1.7 gRPC C++ library and its callback API

- Sources (read 2026-10-30): https://github.com/grpc/grpc ; https://api.github.com/repos/grpc/grpc/releases/latest ; https://grpc.io/docs/languages/cpp/callback/ ; https://github.com/grpc/proposal/blob/master/L67-cpp-callback-api.md ; https://github.com/grpc/grpc/blob/master/include/grpcpp/impl/serialization_traits.h ; https://github.com/grpc/grpc/blob/master/src/compiler/cpp_plugin.h ; https://grpc.io/docs/guides/benchmarking/ ; https://github.com/grpc/grpc/blob/master/doc/interop-test-descriptions.md
- What it is: the C++ implementation of gRPC. It offers a synchronous API, a completion-queue asynchronous API, and a callback API. The grpc.io tutorial says the callback API is the easier of the two asynchronous kinds.
- How it works: services are defined in `.proto`. The C++ plugin (`src/compiler/cpp_plugin.h`, class `CppGrpcGenerator`, a protobuf compiler `CodeGenerator`) generates service classes; the callback tutorial has the user subclass `RouteGuide::CallbackService`. Message (de)serialization is pluggable through `grpc::SerializationTraits<Message>`, whose header says it hooks "different message serialization API's into GRPC". Implementations provide `Serialize` into a `ByteBuffer` and `Deserialize` from one.
- Callback API design: gRFC L67 (status "Proposed", last updated March 22, 2021; authors vjpai, sheenaqotj, yang-g and zhouyihaiding) describes a callback-based asynchronous API intended to give asynchronous performance without the completion-queue programming model.
- Reads `.proto`: yes, through protoc and the plugin. Hand-written declarations: not needed.
- Latest release: v1.85.0, published 2026-10-26T05:31:13Z.
- License: Apache-2.0 (GitHub API).
- Published measurements (self-reported): the benchmarking guide describes a worker design with `UnaryCall` and `StreamingCall`, continuous multi-language tests against a dashboard, and tests of every language against a C++ server and a C++ client. It gives no numbers on the page.
- Interoperability: the interop test description document says client and server use `test.proto` and "the gRPC over HTTP/2 v2 protocol" (the document points to `doc/PROTOCOL-HTTP2.md`, which neither agent read). It defines named test cases such as `empty_unary`, `large_unary`, `client_compressed_unary`, `custom_metadata` and `status_code_and_message`.
- Relevance to P5: the P5 end-to-end baseline (protoc-generated code on gRPC C++), and the source of the test descriptions that a grpc-go interoperability check can reuse. `SerializationTraits` is the documented point where a non-protoc message type enters gRPC C++.
- Checked 2026-10-30: confirmed; L67 author list completed (C5).

### 1.8 grpc-go (interoperability partner)

- Sources (read 2026-10-30): https://github.com/grpc/grpc-go ; https://api.github.com/repos/grpc/grpc-go/releases/latest ; https://grpc.io/docs/guides/benchmarking/
- What it is: the Go implementation of gRPC over HTTP/2.
- Latest release: v1.85.0, published 2026-10-27T09:28:04Z. License: Apache-2.0 (GitHub API).
- Published measurements: Go is one of the languages in the gRPC benchmarking guide (see 1.7).
- Relevance to P5: the interoperability peer named in the task.
- Checked 2026-10-30: confirmed.

### 1.9 protopug

- Sources (read 2026-10-30): https://github.com/onto/protopug ; https://github.com/onto/protopug/blob/master/README.md
- What it is: a small header-only library that serializes and parses C++ structs to the protobuf wire format.
- How it works: the user specializes `protopug::descriptor<Message>` by hand with `field<N, &Message::member>("name")` entries.
- Reads `.proto`: no. gRPC: no. Release: none on GitHub; created 2020-08-09, last push 2020-10-11. License: MIT (GitHub API). Measurements: none.
- Relevance to P5: minor. Same family as protopuf, with hand-written descriptors.
- Checked 2026-10-30: confirmed; creation date added (C6).

### 1.10 Other C++ libraries that derive protobuf codecs at compile time

Cross-reference note (no sources of its own). These are covered in section 2 because they derive the codec from a C++ struct: zpp_bits (2.1), struct_pb in yalantinglibs (2.3), Iguana (2.4), struct_proto26 (2.14), Hutorny's article (2.19). Neither agent found a C++ library that parses `.proto` text at compile time (see section 7).

- Checked 2026-10-30: the cross-references were updated with item 2.19 (C7).

### 1.11 Protobuf conformance test suite

- Sources (read 2026-10-30): https://github.com/protocolbuffers/protobuf/tree/main/conformance ; https://github.com/protocolbuffers/protobuf/blob/main/conformance/README.md
- Facts: the README says the directory holds conformance tests for the completeness and correctness of protobuf implementations, designed to run against any implementation. A tester process (`conformance-test`) holds the tests; a separate program in any language, the testee, talks to it over a pipe. Building with `-Dprotobuf_BUILD_CONFORMANCE=ON` produces a `conformance_test_runner` binary. The directory contains testees and failure lists for several languages and runtimes (for example C++, Java, Python, Rust).
- Latest release: part of protobuf v36.2 (1.1). License: as protobuf (1.1).
- Published measurements: none; it is a correctness suite.
- Relevance to P5: checks that P5's codec is wire-correct independently of gRPC, next to the gRPC interop test descriptions (1.7) that cover grpc-go. Iguana (2.4) reports a conformance testee; zig-protobuf (3.3) and the Elixir protox (3.7) also say they pass the suite (self-reported).
- Checked 2026-10-30: confirmed; the list of implementations that report the suite was extended (C8).

### 1.12 FlatBuffers pull request 9063: constexpr embedding of a binary schema (added by the checker)

- Sources (read 2026-10-30): https://github.com/google/flatbuffers/pull/9063 (through the GitHub REST API)
- What it is: an open pull request (opened 2026-04-25 by BenniRip, not merged on 2026-10-30) that adds a `flatc` flag, `--bfbs-gen-embed-constexpr`, which emits the binary schema as a file-scope `inline constexpr` byte array instead of a function-local static.
- Motivation stated in the PR: compile-time schema processing, for example computing worst-case serialized sizes at compile time.
- Reads schema text in the compiler: no. `flatc` still compiles the schema; only its binary form reaches constant evaluation. gRPC: no.
- Relevance to P5: low. It shows demand for schema data inside constant evaluation in a neighbouring format, still behind an external schema compiler.
- Checked 2026-10-30: added by the checker (A1).

---

## 2. C++ serialization by static or aggregate reflection

### 2.1 zpp_bits

- Sources (read 2026-10-30): https://github.com/eyalz800/zpp_bits ; https://github.com/eyalz800/zpp_bits/blob/main/README.md ; https://api.github.com/repos/eyalz800/zpp_bits/releases/latest
- What it is: a single-header C++20 binary serialization and RPC library.
- How it works: aggregate reflection. The README says that before C++26 serializable types must be aggregates without array members, and that the C++26 feature P1061 (structured bindings can introduce a pack) lifts this. Non-aggregates need a one-line member-count declaration before C++26.
- Protobuf mode: the README has a Protobuf section. It calls protobuf support experimental and says it is generally 2 to 5 times slower, mostly on deserialization, than the library's default format. Field numbers follow member order, or are set with `pb_field<T, N>`, `pb_reserved` or `pb_map`.
- Reads `.proto`: no. The user writes C++ declarations. gRPC: no. It has its own thin RPC.
- Latest release: v4.7.6, published 2026-08-09T21:24:42Z. License: MIT (GitHub API).
- Published measurements (self-reported): README table from fraillt/cpp_serializers_benchmark at commit a4c0ebf, GCC 11: zpp_bits general case 733 ms serialize and 693 ms deserialize, "bin size" column 52192 B, data size 8413 B; protobuf general 19929 ms and 20592 ms, bin size 2077864 B; protobuf with arena 10319 ms and 11787 ms. These are for the default format, not the protobuf mode. No compile-time numbers.
- Relevance to P5: shows aggregate reflection reaching protobuf wire format, and the cost the author attributes to it. Schema is C++.
- Checked 2026-10-30: confirmed; the size columns were labelled as in the README (C9).

### 2.2 glaze

- Sources (read 2026-10-30): https://github.com/stephenberry/glaze ; https://github.com/stephenberry/glaze/blob/main/README.md ; https://github.com/stephenberry/glaze/blob/main/docs/p2996-reflection.md ; https://api.github.com/repos/stephenberry/glaze/releases/latest ; https://api.github.com/repos/stephenberry/glaze/contents/docs/rpc
- What it is: a C++ serialization, reflection and RPC library. The repository description lists JSON, BEVE, BSON, CBOR, CSV, JSONB, MessagePack, TOML, YAML and EETF. Protobuf is not in that list.
- How it works: what the README calls pure reflection, plus an optional P2996 backend. The README says it supports GCC 16+ (`-std=c++26 -freflection`) and the Bloomberg clang-p2996 fork (which also needs `-fexpansion-statements` and libc++). Enabled with the CMake option `glaze_ENABLE_REFLECTION26`. The README lists benefits: no 128-member limit, non-aggregates, private members, automatic enums. The docs say P2996 was voted into C++26 in June 2025.
- Reads `.proto`: no. gRPC: no. The RPC docs cover JSON-RPC and REPE.
- Latest release: v9.1.0, published 2026-10-28T16:07:32Z. License: MIT (GitHub API). hpp-proto (1.6) uses glaze for its JSON.
- Published measurements: a README runtime benchmark table that compares libraries; the P2996 page has no compile-time numbers.
- Relevance to P5: a mature library already using GCC 16 `-freflection` in production-style code. It shows the toolchain flags and pitfalls P5 will meet.
- Checked 2026-10-30: confirmed.

### 2.3 struct_pack and struct_pb (yalantinglibs)

- Sources (read 2026-10-30): https://github.com/alibaba/yalantinglibs ; https://api.github.com/repos/alibaba/yalantinglibs/releases/latest ; https://github.com/alibaba/yalantinglibs/blob/main/website/docs/en/struct_pack/struct_pack_intro.md ; https://github.com/alibaba/yalantinglibs/blob/main/website/docs/en/struct_pb/struct_pb_intro.md ; https://github.com/alibaba/yalantinglibs/blob/main/src/struct_pb/tools/README.md ; https://github.com/alibaba/yalantinglibs/blob/main/include/ylt/struct_pb/struct_pb_impl.hpp ; https://github.com/alibaba/yalantinglibs/blob/main/website/docs/en/coro_rpc/coro_rpc_introduction.md
- What it is: an Alibaba collection of C++ libraries. struct_pack is a binary serializer for aggregates. struct_pb is a protobuf wire-format serializer in the same collection.
- struct_pack: the docs say serialization of one aggregate takes one line "without any DSL, macro, or template". It uses compile-time reflection, computes an MD5 type hash at compile time (so runtime compares 32-bit hash values), and offers a `compatible<T>` wrapper for versioning. The format is its own, not protobuf. Self-reported: the intro claims performance significantly better than protobuf and msgpack, based on its benchmark section (one million iterations per case, pre-allocated buffers).
- struct_pb: C++17, header-only. Its stated motivation is to not depend on proto files or protoc. Structs are declared by hand with `YLT_REFL(my_struct, x, y, z)` and special types such as `struct_pb::fixed64_t`. Limitations listed in the doc: proto3 only, no unknown fields, structs must inherit `base_impl` (the roadmap says that requirement is to be removed). The docs describe a "monster" benchmark with struct_pb 2.4 times faster than protobuf on serialize and 3.4 times faster on deserialize (self-reported; compiler and hardware not stated in the doc). `struct_pb_impl.hpp` includes Iguana's protobuf reader and writer headers and declares `namespace struct_pb = iguana`.
- Tool: `src/struct_pb/tools` contains `proto_to_struct`, a protoc plugin that converts a `.proto` file into struct_pack-style C++ structs. It needs libprotobuf and libprotoc (README says version 3.21.0).
- gRPC: no gRPC claim in these docs. The collection has its own `coro_rpc`; its introduction describes struct_pack serialization of RPC arguments and compares its throughput with gRPC and brpc (self-reported). It does not claim gRPC wire compatibility.
- Latest release: 0.6.1, published 2026-04-29T06:31:52Z (an `lts2.0.1` release, 2025-12-16, also exists). License: Apache-2.0 (GitHub API).
- Relevance to P5: shows both routes: hand-declared structs, or a protoc plugin that emits those structs. P5 replaces the plugin with compile-time parsing.
- Checked 2026-10-30: confirmed; the struct_pb to Iguana link was sourced (C10); the coro_rpc question was settled from its introduction (M4).

### 2.4 Iguana (qicosmos), with a P2996 path

- Sources (read 2026-10-30): https://github.com/qicosmos/iguana ; https://api.github.com/repos/qicosmos/iguana/releases/latest ; https://github.com/qicosmos/iguana/blob/master/README.md ; https://github.com/qicosmos/iguana/blob/master/iguana_reflect26_changes.md ; https://github.com/qicosmos/iguana/tree/master/tool ; https://github.com/qicosmos/iguana/blob/master/.github/workflows/linux-gcc-cxx26.yml
- What it is: a serialization engine for JSON, XML, YAML and protobuf. struct_pb (2.3) is a namespace alias of its protobuf code.
- How it works with P2996: the README (in English) and the change notes (in Chinese; paraphrased here) say a C++26 path reads class members with `std::meta::nonstatic_data_members_of` and `template for`, with extra semantics as `[[= ...]]` annotations. Protobuf field numbers use `[[= iguana::pb_field(N)]]`; the macro `YLT_REFL_PB` stays for older compilers. The struct is the schema. The README also offers `iguana::to_proto<T>`, which writes a `.proto` view of a struct. CI runs GCC 16.1.0 (`gcc:16.1.0-trixie`) with `-std=gnu++26 -freflection` and `-DENABLE_CXX26_REFLECTION=ON`.
- Tests and conformance (self-reported in the change notes): 21 of 21 CTest tests pass in the C++26 build. Unit tests compare Iguana bytes with a C++ runtime generated by protoc 3.21.12. The `iguana_conformance` testee was run with `conformance_test_runner --enforce_recommended` built from protobuf v3.21.12 source: Binary/JSON suite 651 successes, 1366 skipped, 0 unexpected failures; Text-format suite 0 successes, 120 skipped. The notes say this testee is a proto3 binary canonicalizer that does not call the public `iguana::to_pb` and `iguana::from_pb`, so it complements, not replaces, the API tests. `benchmark/pb_benchmark.cpp` prints the Iguana-to-protobuf time ratio (no numbers captured).
- Reads `.proto`: not in the C++26 path. `tool/proto_to_struct.cpp` is a protoc plugin (it calls `google::protobuf::compiler::PluginMain`) that emits structs. gRPC: no.
- Latest release: 1.2.0, published 2026-06-25T00:41:48Z. The change notes were last committed 2026-06-06. Last push 2026-08-12. License: Apache-2.0 (GitHub API).
- Relevance to P5: the nearest existing P2996 protobuf codec with CI on GCC 16.1 and a conformance runner. It confirms that P2996 can drive a protobuf encoder and decoder, and shows the conformance suite P5 can reuse. It goes from struct to schema (it can even print the schema as `.proto` text); P5 goes from schema text to struct.
- Checked 2026-10-30: confirmed; the language of the README was corrected and the conformance details, the testee scope and `to_proto` were added (C11).

### 2.5 Boost.PFR

- Sources (read 2026-10-30): https://github.com/boostorg/pfr ; https://github.com/boostorg/pfr/blob/develop/doc/pfr.qbk ; https://api.github.com/repos/boostorg/pfr/releases/latest ; https://api.github.com/repos/boostorg/boost/releases/latest
- What it is: C++14 library giving `std::tuple`-like access, and (C++20 recommended) field names, for aggregate types without macros.
- How it works: aggregate reflection. The docs require a "SimpleAggregate": no base classes, no const fields, no references, no C arrays. Reflection of unions is disabled.
- Reads `.proto`: no. gRPC: no.
- Release: GitHub release 2.2.0, published 2023-11-23. Latest tag boost-1.92.0. Boost 1.92.0 was published 2026-08-12T11:41:32Z (boostorg/boost releases).
- License: BSL-1.0 (GitHub API). Measurements: none relevant.
- Relevance to P5: the aggregate-reflection baseline that P2996 replaces; its limits explain why P2996 is attractive.
- Checked 2026-10-30: confirmed.

### 2.6 cereal

- Sources (read 2026-10-30): https://github.com/USCiLab/cereal ; https://api.github.com/repos/USCiLab/cereal/releases/latest
- What it is: a header-only C++11 serialization library with binary, XML and JSON archives.
- How it works: the user writes a `serialize` function per type (README example). No reflection, no schema.
- gRPC and protobuf: neither. Latest release: v1.3.2, published 2022-02-28. License: BSD-3-Clause (GitHub API).
- Relevance to P5: the hand-written serialization-function baseline that `autocereal` (2.16) removes with P2996.
- Checked 2026-10-30: confirmed; the relevance sentence was reworded (C12).

### 2.7 bitsery

- Sources (read 2026-10-30): https://github.com/fraillt/bitsery ; https://github.com/fraillt/bitsery/blob/master/README.md ; https://api.github.com/repos/fraillt/bitsery/tags
- What it is: a header-only C++ binary serialization library for real-time and game networking.
- How it works: the README lists "No code generation required: no IDL or metadata" and no macros. The README usage example has the user write a `serialize(S& s, T& o)` function template per type that names each field with its byte width (for example `value4b`, `container4b`).
- Release: no GitHub release. Latest tag v5.2.5, tagged commit dated 2025-10-08. License: MIT (GitHub API).
- Published measurements (self-reported): README table, Ubuntu with GCC 10.3.0: bitsery data size 6913 B, 1119 ms serialize, 1166 ms deserialize; protobuf data size 10018 B, 21229 ms and 22077 ms.
- Relevance to P5: shows how large the vendor-reported gap is between protoc-generated code and hand-tuned serializers, and warns that these tables are old and use their own benchmark rig.
- Checked 2026-10-30: confirmed; the size column was labelled and the usage example was read (C13).

### 2.8 reflect-cpp

- Sources (read 2026-10-30): https://github.com/getml/reflect-cpp ; https://api.github.com/repos/getml/reflect-cpp/releases/latest ; https://github.com/getml/reflect-cpp/blob/main/README.md ; https://munich-data-engineering.github.io/reflect-cpp/cpp26_reflection ; https://munich-data-engineering.github.io/reflect-cpp/benchmarks
- What it is: a C++20 serialization, deserialization and validation library based on reflection. The repository description lists JSON, Avro, Boost Serialization, BSON, Cap'n Proto, CBOR, Cereal, CLI, CSV, environment variables, flexbuffers, msgpack, parquet, TOML, UBJSON, XML, YAML and yas. Protobuf is not in the list.
- C++26 page: an opt-in C++26 mode uses `<meta>` (P2996) instead of structured-binding tricks. Supported compilers: GCC 16.2 or higher, and Clang only as experimental builds that implement P2996 (such as clang-p2996). The page says the C++26 mode has lower compile-time cost, because fields are queried from the compiler instead of inferred, but gives no numbers.
- Benchmarks page: runtime JSON benchmarks run continuously in CI across systems and compilers; no compile-time results.
- Latest release: v0.26.2, published 2026-10-19T21:04:51Z. License: MIT (GitHub API).
- Relevance to P5: confirms that a large reflection-based serialization library had no protobuf backend on 2026-10-30. Its unquantified claim of cheaper compile time under P2996 is one more reason P5 should measure.
- Checked 2026-10-30: confirmed; both linked pages were opened (M5).

### 2.9 The P2996 paper's own examples

- Sources (read 2026-10-30): https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html (downloaded and searched locally)
- Facts: section 3 lists 17 examples, including enum to string (3.6), command-line parsing (3.7 and 3.11), a simple tuple (3.8), a simple variant (3.9), struct to struct of arrays (3.10), a universal formatter (3.12), member-wise `hash_append` (3.13), struct to tuple (3.14), `tuple_cat` (3.15), a named tuple (3.16) and a compile-time ticket counter (3.17). `define_aggregate` appears in the text. A search of the full text for "serializ", "json" and "protobuf" found no match.
- Relevance to P5: the paper's examples show type synthesis (`define_aggregate`, tuple, variant, named tuple) but contain no serialization or schema example. P5's schema-to-type step relies on the same `define_aggregate` mechanism.
- Checked 2026-10-30: confirmed (the checker repeated the three searches).

### 2.10 Bloomberg clang-p2996 fork

- Sources (read 2026-10-30): https://github.com/bloomberg/clang-p2996 ; https://github.com/bloomberg/clang-p2996/blob/p2996/P2996.md ; https://api.github.com/repos/bloomberg/clang-p2996 ; https://api.github.com/repos/bloomberg/clang-p2996/contents/test.json?ref=p2996
- What it is: an experimental Clang fork implementing P2996. P2996R13 section 2.3 names it as a second implementation next to EDG's.
- Facts: the project page `P2996.md` (on the `p2996` branch) says the fork is highly experimental, not for production artifacts, and that memory use has not been optimized and is often wasteful (paraphrased). It says new code is under Apache 2.0 with LLVM extensions; the GitHub API reports no license. The default branch is `purpose`. Last push 2026-09-24. No GitHub release.
- Modules and precompiled headers: `P2996.md` explains that its metafunction design keeps an evaluation callback in the AST that cannot be serialized, that this breaks C++20 modules and precompiled headers in that design, and that the authors consider the problem not fully solved.
- Examples: `P2996.md` has no serialization example. The `p2996` branch root has a `test.json` whose keys match the JSON example in Revzin's post (2.12), with one value different; its role was not checked.
- Relevance to P5: an alternative compiler for cross-checking P5's generated code. Its stated memory behavior matters for any compile-time scaling claim, and the precompiled-header mitigation measured on GCC (4.1) may not carry over to it.
- Checked 2026-10-30: confirmed; the modules and precompiled-header statement was added and the `test.json` note made precise (C14).

### 2.11 P3394R4 serde-style example

- Sources (read 2026-10-30): https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html
- Facts: section 3.3 ("Serialization") shows Rust-serde-style member-wise JSON output into Boost.JSON using a `[[= serde::derive]]` annotation and a `serde::rename` annotation, walking `nonstatic_data_members_of` with `template for`.
- Relevance to P5: annotations are the standard way to attach field numbers or other protobuf options to members. P5 does not need them when types come from `.proto` text, but they matter for any struct-to-schema comparison arm.
- Checked 2026-10-30: confirmed.

### 2.12 Revzin and Katz: JSON file to C++ object with `#embed` and `define_aggregate`

- Sources (read 2026-10-30): https://brevzin.github.io/c++/2025/06/26/json-reflection/ (posted Jun 26, 2025, updated Oct 12, 2025, by Barry Revzin) ; https://isocpp.org/blog/2025/08/reflecting-json-into-cpp-objects-barry-revzin
- What it is: an example that Dan Katz wrote on the flight home from the Sofia meeting, per Revzin. A `constexpr` array is filled by `#embed "test.json"` (plus a trailing 0), a compile-time JSON parser reads it, and `define_aggregate` completes struct types, so the result is an object whose type mirrors the JSON (the post shows the object type for the sample file and `define_aggregate(^^Inner, {Ms...})` in the code).
- Reads schema text: yes (JSON). Generates types: yes. Endpoints: no. External generator: none.
- Revzin notes that Boost.JSON and nlohmann::json had no constexpr support at the time, so he shows the parser part with a pretend Boost.JSON and links a Compiler Explorer version.
- Measurements: none in the post.
- Relevance to P5: the same mechanism P5 uses (`#embed`, constexpr parse, `define_aggregate`), applied to JSON. Together with simdjson's released implementation (2.13), it is the closest published precedent for the schema-text-to-types step.
- Checked 2026-10-30: confirmed.

### 2.13 simdjson: compile-time JSON and reflection-based deserialization

- Sources (read 2026-10-30): https://github.com/simdjson/simdjson/blob/master/doc/compile_time.md ; https://github.com/simdjson/simdjson/blob/master/include/simdjson/compile_time_json-inl.h ; https://api.github.com/repos/simdjson/simdjson/releases ; https://simdjson.github.io/simdjson_talks/cppcon2025/cppcon_2025_slides.pdf ; https://github.com/simdjson/simdjson/blob/master/benchmark/benchmark_reflection_usage_compilation.sh
- What it is: the JSON parser library. With C++26 it offers `simdjson::compile_time::parse_json` (and a `""_json` literal), which parses a JSON document at compile time and returns a `constexpr` structure; objects become anonymous structs, arrays become `std::array`. The doc shows loading from disk with `#embed "test.json"`. It also offers reflection-based deserialization into user structs when static reflection is enabled.
- P2996 use: `compile_time_json-inl.h`, guarded by `SIMDJSON_STATIC_REFLECTION`, builds the result types with `std::meta::data_member_spec` and `std::meta::define_aggregate`, and uses `std::meta::substitute` and `std::meta::reflect_constant`. So `parse_json` is a P2996 type synthesizer over embedded text.
- Latest release: v5.1.0, published 2026-10-30T00:42:09Z (v5.0.8 was published 2026-10-29T01:10:56Z). License: Apache-2.0 (GitHub API).
- Published measurements (self-reported): CppCon 2025 slides by Daniel Lemire and Francisco Geiman Thiesen, slide 54: they observed a 6% slow-down when compiling simdjson with static reflection enabled, on the clang-p2996 experimental branch. The slide names no feature and no input size; it is a whole-library build figure. The repository also has `benchmark/benchmark_reflection_usage_compilation.sh`, a script that compares compile time of reflection-based and manual runtime parsing; neither agent ran it or found published results from it.
- Relevance to P5: a released library that does `#embed` plus compile-time parsing plus `define_aggregate`, the exact mechanism of P5's schema step, for JSON. It is also the source of one of few published compile-cost figures for reflection use.
- Checked 2026-10-30: confirmed; the release was updated (C15) and the P2996 question was settled from the source (M6).

### 2.14 struct_proto26 (PikachuHyA)

- Sources (read 2026-10-30): https://github.com/PikachuHyA/struct_proto26 ; https://github.com/PikachuHyA/struct_proto26/blob/master/README.md ; https://github.com/PikachuHyA/struct_proto26/blob/master/include/proto3.hpp ; https://pikachuhya.github.io/posts/serde-proto3-with-cxx26-reflection/ (post dated May 14, 2026)
- What it is: a header-only proto3 wire-format serializer where the user's C++ struct is the schema. The README says: no `.proto` files, no codegen, no descriptor runtime. It uses `<meta>` to walk members at compile time. Field numbers default to declaration order; `[[= proto3::field(N)]]`, `zigzag`, `fixed`, `oneof<...>`, `skip` and other annotations override. It covers scalars, nested messages, repeated (packed), maps, oneof through `std::variant`, optional, and well-known types in `well_known.hpp`. `static_assert` checks field numbers are in range, outside 19000 to 19999, and unique.
- Build: Bazel with GCC 16 and `-std=c++26 -freflection`. The README describes a pinned toolchain and calls mainline GCC 16 support upcoming; the later post says it was built with GCC 16.1 from Ubuntu with no compiler patches. The post gives about 1400 lines of header (`proto3.hpp` has 1232 lines on 2026-10-30, plus `well_known.hpp`) and about 2000 lines of tests.
- gRPC: no. Reads `.proto`: no.
- Release: none. Repository created 2026-05-13, last push 2026-06-04, 15 stars. The root listing has no license file and the GitHub API reports none, so reuse terms are unclear.
- Measurements: the post reports no compile-time or run-time benchmarks, only illustrative wire sizes.
- Relevance to P5: a public P2996 proto3 codec. Opposite direction from P5, and no performance data.
- Checked 2026-10-30: confirmed; the toolchain statements of README and post were set side by side (C16).

### 2.15 static-serial (lim-james)

- Sources (read 2026-10-30): https://github.com/lim-james/static-serial ; https://github.com/lim-james/static-serial/blob/main/README.md
- Facts: reflection-based binary serializer for fixed-size wire formats, GCC 16 required, MIT (GitHub API), no release, last push 2026-09-01. Own format, not protobuf.
- Relevance to P5: another GCC 16 reflection serializer; low relevance.
- Checked 2026-10-30: confirmed.

### 2.16 autocereal (FlyingRhenquest)

- Sources (read 2026-10-30): https://github.com/FlyingRhenquest/autocereal ; https://github.com/FlyingRhenquest/autocereal/blob/main/README.md
- Facts: a C++26 project, tested with a freshly built GCC 16, that lets cereal serialize classes without hand-written `load` and `save` functions, including private members. License reported as `NOASSERTION` by GitHub. Last push 2026-04-03.
- Relevance to P5: low; shows P2996 removing cereal's boilerplate.
- Checked 2026-10-30: confirmed.

### 2.17 grpcxx (trunkszi), a P2996 gRPC framework

- Sources (read 2026-10-30): https://github.com/trunkszi/grpcxx ; https://github.com/trunkszi/grpcxx/blob/main/README.md ; https://github.com/trunkszi/grpcxx/blob/main/docs/protoc-gen-grpcxx.md ; https://api.github.com/repos/trunkszi/grpcxx/git/trees/main?recursive=1
- What it is (self-reported): a Linux C++26 gRPC framework with caller-driven, single-threaded `io_uring` services. Services are declared as structs with `[[= grpcxx::service{...}]]` and `[[= grpcxx::rpc{...}]]` annotations (P3394); `ServiceRouter` and `ClientStub` consume the reflected contracts, with nghttp2 for HTTP/2. Codecs: Protobuf, Apache Fory and Cap'n Proto.
- Where the schema comes from, two routes: (1) the README's reflection example declares the annotated contract struct and the message struct by hand and supplies its own `QuoteCodec`; this route needs no protoc but reads no schema text. (2) The contract generation guide says tools under `src/grpcxx/tools/` generate the reflected contracts from `.proto`, `.fdl` or `.capnp` files and keep each backend's native message bindings. For protobuf the guide lists `order_book.pb.h` and `order_book.pb.cc` plus a generated `order_book.grpcxx.h`. The tools need protoc and libprotoc. In this route protoc stays in the build and message types come from protoc.
- Build: GCC 16.2 default preset; library targets compile with `-fno-exceptions`.
- Interoperability: a search of the repository tree (278 paths) for interop, grpc-go and compatibility tests found none.
- Latest release: none found. Repository created 2026-10-21, last push 2026-10-22, MIT (GitHub API), 1 star at read time. Very young.
- Published measurements: the README lists five benchmark suites (framing, router, codecs, compression, order pipeline) and says "No comparison with another gRPC implementation is implied" by a benchmark run.
- Relevance to P5: the closest public work on P2996 plus gRPC. It differs from P5 in two ways: no route takes `.proto` text in the compiler (either protoc produces messages and contracts, or the user writes them), and it makes no protoc-versus-reflection or cross-implementation comparison. Its claims are README-level (code not reviewed).
- Checked 2026-10-30: confirmed; the hand-declared route and the absence of interop tests were added (C17).

### 2.18 zBuffer and zRPC (static reflection by templates and macros)

- Sources (read 2026-10-30): https://doi.org/10.1145/3774934.3786426 ; https://api.crossref.org/works/10.1145/3774934.3786426 ; https://api.openalex.org/works/doi:10.1145/3774934.3786426
- Facts from the abstract: zBuffer is a zero-copy, metadata-free serialization library for RPC. It uses "scatter-gather reflection": NIC scatter-gather hardware plus static reflection built from C++ template metaprogramming and macros at compile time. zRPC is an RPC system on top. The abstract reports zBuffer about 7 times faster than Cornflakes in serialization for complex objects, and zRPC with 21% lower 99th percentile latency and 62% higher throughput than eRPC (self-reported). Authors: Xiangyu Liu, Huiba Li, Shun Gai, Youmin Chen, Yiming Zhang. Published at the ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP '26), 2026.
- DOI: 10.1145/3774934.3786426 (resolves at doi.org). Format is not protobuf.
- Relevance to P5: academic example of compile-time reflection for RPC serialization, in the pre-P2996 style (templates and macros).
- Checked 2026-10-30: confirmed; the author list was completed (C18).

### 2.19 Hutorny: practical C++26 reflection for protobuf (added by the checker)

- Sources (read 2026-10-30): https://hutorny.in.ua/projects/c/practical-c26-reflection-for-protobuf (posted Saturday, January 31, 2026, by Eugene Hutorny)
- What it is: a long article that builds a malloc-free protobuf deserializer with P2996. The user translates each `.proto` message into a C++ struct by hand and marks each field with an integer annotation that holds its field number; protobuf integer subtypes become C++ enums. At compile time, reflection fills a dispatch table of field readers.
- Use of `define_aggregate`: only to synthesize the result type, a struct of bit-field presence flags named after the message fields. Message types themselves are hand-written.
- Toolchain: built with the Bloomberg clang-p2996 fork. Test vectors were made with `protoc --encode`. The author says Compiler Explorer's compile-time limits allowed only a few tests online and that an offline set of 400+ tests would appear on GitHub; no link was given on the page.
- Reads `.proto`: no. Encoder: not covered; the article is about deserialization. gRPC: no. Measurements: none.
- Relevance to P5: a third struct-first P2996 protobuf codec next to Iguana (2.4) and struct_proto26 (2.14), and an example of `define_aggregate` used for an auxiliary type. Same direction as those two, opposite to P5.
- Checked 2026-10-30: added by the checker (A2).

### 2.20 Dathskovsky: `#embed` and reflection for schema-driven types (added by the checker)

- Sources (read 2026-10-30): https://www.cppnext.com/post/c-26-embed-and-reflection-when-the-compiler-starts-doing-more-work-for-you (by Alex Dathskovsky; the page's metadata gives 2026-06-02 as the publication date)
- What it is: a short blog post that describes, in prose, embedding a tiny schema file with `#embed`, parsing it during compilation, extracting field names and types, and using reflection to create a matching struct with extra internal fields (for example a payload span) that the schema did not name. It motivates this for communication protocols.
- Code: the post shows the input schema and the intended resulting struct; it publishes no implementation or repository in its text.
- Not protobuf. No endpoints. No measurements.
- Relevance to P5: evidence that the `#embed` plus parse plus reflection idea for protocol schemas is circulating in 2026 blog writing, without an implementation or data.
- Checked 2026-10-30: added by the checker (A3).

---

## 3. Code generation in other languages, without an external generator or at build time

### 3.1 Rust: prost (with build.rs), protox, tonic, and the official Rust crate

- Sources (read 2026-10-30): https://github.com/tokio-rs/prost ; https://github.com/tokio-rs/prost/blob/master/README.md ; https://api.github.com/repos/tokio-rs/prost/releases/latest ; https://github.com/andrewhickman/protox ; https://api.github.com/repos/andrewhickman/protox/releases/latest ; https://github.com/hyperium/tonic ; https://api.github.com/repos/hyperium/tonic/releases ; https://api.github.com/repos/protocolbuffers/protobuf/releases/tags/v36.2
- prost: generates simple, idiomatic Rust types using `derive` attributes. `prost-build` runs from `build.rs`. Since prost-build v0.11 it requires `protoc` to be installed (no bundled protoc) unless `skip_protoc` is enabled. The `derive` feature can be disabled "to reduce compile times" per the feature list. Existing Rust types can be serialized by adding derives and annotations. No runtime reflection. Latest release v0.14.4, 2026-06-07; Apache-2.0. The README carries a "passively-maintained" badge and says the current maintainer is not contributing new features and expects the official Rust library from the protobuf project.
- protox: an implementation of the protobuf compiler in Rust, meant as a library for crates such as prost-build so that protoc need not be built. README shows `protox::compile` feeding `prost_build::compile_fds` and `tonic_build::...compile_fds`. Latest release protox-v0.10.0, 2026-10-07; GitHub API says Apache-2.0 (the README badge says Apache-2.0 OR MIT). Not to be confused with the Elixir library of the same name (3.7).
- tonic: GitHub description "A native gRPC client & server implementation with async/await support"; MIT (GitHub API). The releases list is per crate; the main crate's latest entry is tonic-v0.14.9, published 2026-10-29T17:37:41Z.
- Official crate: the protobuf v36.2 release notes say a README update notes that the `protobuf` crate is superseded by `google-protobuf`, and list Rust code generator changes.
- Reads `.proto`: yes, through protoc or protox, at build time. gRPC: through tonic. Compile-time reflection: Rust uses build-time generation, not compile-time parsing of embedded text.
- Published measurements: none found for compile time.
- Relevance to P5: the standard build-time-generation model in Rust, plus protox as an example of replacing protoc with an in-language parser.
- Checked 2026-10-30: confirmed; the tonic version was found (M7).

### 3.2 Rust: quick-protobuf and pb-rs

- Sources (read 2026-10-30): https://github.com/tafia/quick-protobuf ; https://github.com/tafia/quick-protobuf/blob/master/README.md ; https://api.github.com/repos/tafia/quick-protobuf/tags ; https://github.com/tafia/quick-protobuf/blob/master/perftest/README.md
- Facts: `pb-rs` is a code generation tool that reads `.proto` files with its own parser; the README says there is "no need to use google protoc tool". Generated modules assume the `quick-protobuf` crate, which acts as an event-style parser. Message mappings use `Cow` for bytes and strings.
- Release: no GitHub release. Latest tags by date order seen: `quick.0.6.4` and `pbrs.0.8.3`, both on the same commit dated 2019-11-20; `v0.6.3` dated 2018-08-25. Last push 2024-02-14. License: MIT (GitHub API).
- Measurements: the `perftest` README compares runtime of rust-protobuf, quick-protobuf and prost in nanoseconds per iteration (self-reported; no machine, compiler or date stated). No compile-time data.
- Relevance to P5: build-time generation without protoc, in a different language.
- Checked 2026-10-30: confirmed; the `perftest` README was opened (M8).

### 3.3 Zig: zig-protobuf

- Sources (read 2026-10-30): https://github.com/Arwalk/zig-protobuf ; https://github.com/Arwalk/zig-protobuf/blob/master/README.md ; https://github.com/Arwalk/zig-protobuf/blob/master/build_util.zig ; https://api.github.com/repos/Arwalk/zig-protobuf/releases/latest ; https://github.com/Arwalk/proto-benchmarks ; https://github.com/mlugg/zigpb
- Facts: supports proto2, proto3 and editions (up to edition 2026), with extensions. The README says it passes the upstream conformance suite at protobuf v36.2 for binary and JSON (self-reported). `.zig` files are generated by a protoc plugin (`build_util.zig` passes `--plugin=protoc-gen-zig=`); `RunProtocStep` in the build runs protoc and, by default, downloads Google's protoc (version 36.2 per the README and the `PROTOC_VERSION` constant). Options appear as `comptime` constants. Services: generated interfaces in a delegate pattern; the README says there is no gRPC transport layer.
- Latest release: v6.0.1, published 2026-10-22T21:08:47Z. License: MIT (GitHub API).
- Benchmarks (self-reported setup): `Arwalk/proto-benchmarks` (created 2026-10-11, no license, 0 stars) compares protobuf implementations across ten languages on three shared datasets, from a single host, pinning each process to an allowed CPU set, with documented methodology; its README shows a re-render option `--baseline cpp/protobuf`. No numbers captured here.
- Zig compile-time `.proto` parsing: no Zig library that parses `.proto` at comptime was found. The only mention found is `mlugg/zigpb` (created and last pushed 2023-01-20), whose README lists a comptime `.proto` parser among planned changes.
- Relevance to P5: even in a language built around comptime, the leading library uses protoc. `proto-benchmarks` is a methodology reference for cross-implementation comparison.
- Checked 2026-10-30: confirmed with corrections: editions, conformance and the plugin name were added (C19); the unsupported baseline statement was removed (R2); the zigpb plan was added (A4).

### 3.4 D: dproto (compile-time parsing of `.proto`)

- Sources (read 2026-10-30): https://github.com/msoucy/dproto ; https://github.com/msoucy/dproto/blob/master/README.md ; https://github.com/msoucy/dproto/blob/master/import/dproto/dproto.d ; https://github.com/msoucy/dproto/blob/master/import/dproto/intermediate.d ; https://api.github.com/repos/msoucy/dproto/releases/latest
- What it is: "D Protocol Buffer mixins to create structures at compile time" (repository description). `mixin ProtocolBufferFromString!"message Person {...}"` parses a schema string at compile time and generates D structs with `serialize()` and a deserializing constructor.
- How it works: in `dproto.d`, `template ProtocolBuffer(string s)` calls `mixin(ParseProtoSchema(s, import(s)).toD())`, so D's string import (`import(s)`) reads the file at compile time and compile-time function evaluation parses it. The header comment says the file is assumed to be in the string imports. A separate template takes a string directly.
- Services: `ProtocolBufferInterface` generates an interface from `service` definitions (README example implements `TestService`). In `intermediate.d`, `ProtocolBufferRpc` generates an interface whose methods take the request and a reference to the response, and `ProtocolBufferImpl` generates a class whose methods return a default response. No transport, gRPC or otherwise, is mentioned.
- Latest release: v2.1.6, published 2020-05-02. License: BSL-1.0 (GitHub API). The parser is adapted from square/protoparser (README).
- Published measurements: none.
- Relevance to P5: the same idea in another language, including service declarations. It predates C++ reflection by years and shows the design is feasible. No measurements exist to compare against.
- Checked 2026-10-30: confirmed; the generated forms of the Rpc and Impl templates were read in the source (C20).

### 3.5 Nim: protobuf-nim

- Sources (read 2026-10-30): https://github.com/PMunch/protobuf-nim (README.rst read through the GitHub API)
- What it is: a "pure Nim implementation of protobuf" whose single macro takes a proto3 specification, as a string or a file, and produces types plus read, write and length procedures. The README says it does not rely on `protoc` and gives the rationale: no extra build step and no external dependency.
- Limits (README): proto3 only; no maps at the time of writing (planned); options unsupported; services unsupported. A compile-time switch `-d:echoProtobuf` prints the generated code.
- Release: no GitHub release. Latest tag v0.5.4, tagged commit dated 2023-10-17; last push 2023-10-17. License: MIT (GitHub API).
- Measurements: none.
- Relevance to P5: another existing implementation of `.proto` text to types at compile time, without services.
- Checked 2026-10-30: confirmed.

### 3.6 Go: protoc-gen-go (contrast only)

- Sources (read 2026-10-30): https://github.com/protocolbuffers/protobuf-go ; https://api.github.com/repos/protocolbuffers/protobuf-go/releases/latest ; https://protobuf.dev/reference/go/go-generated/
- Facts: the project has two components: `protoc-gen-go`, a plugin to protoc, and a runtime library with `protoreflect` interfaces. protobuf.dev says the compiler needs a plugin to generate Go code. Latest release v1.36.12, published 2026-08-10T15:29:56Z. License: BSD-3-Clause (GitHub API). gRPC for Go is grpc-go (1.8).
- Relevance to P5: the peer implementation for interop and the model of plugin-based generation. Go generates with a protoc plugin and keeps a runtime descriptor; P5 has neither.
- Checked 2026-10-30: confirmed.

### 3.7 Elixir: exprotobuf and protox (added by the checker)

- Sources (read 2026-10-30): https://github.com/bitwalker/exprotobuf ; https://github.com/bitwalker/exprotobuf/blob/master/mix.exs ; https://github.com/tomas-abrahamsson/gpb ; https://api.github.com/repos/bitwalker/exprotobuf/releases/latest ; https://github.com/ahamez/protox ; https://api.github.com/repos/ahamez/protox/releases/latest ; https://hexdocs.pm/elixir/alias-require-and-import.html
- Language fact: the Elixir guide says macros are expanded at compile time and that `use` is a macro. Both libraries below take the schema through `use` inside a module definition.
- exprotobuf: `use Protobuf, from: <path>` (or a schema string) builds module and struct definitions from a protobuf schema. The README says it uses gpb for schema parsing, and `mix.exs` depends on gpb 4.x. gpb's README calls gpb a compiler for protocol buffer definition files for Erlang, with its own front end `protoc-erl`, and suggests Google's protoc only as an optional prior check for better error messages; GitHub reports gpb's language as Erlang. So the parse runs in the Elixir compiler's process through a library, with no protoc run. Release 1.0 dropped handling of `import` statements. Services are not mentioned in the README. Apache-2.0, 483 stars, last push 2024-07-04. The GitHub releases entry is 0.10.0 (2015-07-06); the latest tag is 1.2.17, tagged commit dated 2019-07-16.
- protox (Elixir, not the Rust crate of 3.1): `use Protox, schema: """..."""` or `files: [...]` compiles proto2 and proto3 schemas at build time, but the README says protoc 3.0 or later must be on the path at compile time. The README says it passes Google's conformance suite (self-reported) and points to a benchmark document, not opened. MIT, 300 stars, latest release v2.1.0, 2026-09-19; last push 2026-10-26.
- gRPC: neither README mentions it.
- Relevance to P5: exprotobuf is another schema-text-to-types step inside the compiler with no external generator, next to dproto (3.4), protobuf-nim (3.5) and Froto (3.8). protox shows the other variant: compile-time generation that still calls protoc.
- Checked 2026-10-30: added by the checker (A5).

### 3.8 F#: Froto.TypeProvider (added by the checker)

- Sources (read 2026-10-30): https://github.com/ctaggart/froto ; https://github.com/ctaggart/froto/blob/master/docs/type-provider/type-provider.md ; https://github.com/ctaggart/froto/blob/master/TypeProvider.DesignTime/Froto.TypeProvider.DesignTime.fsproj ; https://api.github.com/repos/ctaggart/froto/releases/latest ; https://learn.microsoft.com/en-us/dotnet/fsharp/tutorials/type-providers/
- What it is: Froto has a parser for proto2, proto3 and gRPC syntax, and `Froto.TypeProvider`, which its documentation calls a generative type provider that generates types from `.proto` files. The README says the type provider supports proto2 so far.
- How it works: the user writes `type Protocol = ProtocolBuffersTypeProvider<ProtoFile>` with a literal path; the provided type then contains the messages and enums of the file. Microsoft's documentation says provided types are generated by the F# compiler from an external data source. The design-time project file references Froto's own parser project (built on FParsec) and the serialization project, and no protoc.
- Services from the type provider: not described. gRPC: only the parser handles gRPC syntax.
- Release: 0.7.2, published 2019-08-18. Repository archived, last push 2019-08-19, MIT, 145 stars.
- Measurements: none seen.
- Relevance to P5: a third family member of dproto: `.proto` file to types inside the compiler, with no external generator. Archived and proto2 only.
- Checked 2026-10-30: added by the checker (A6).

---

## 4. Research on C++ compile-time cost

### 4.1 Romeo, "the hidden compile-time cost of C++26 reflection" (2026-03-06)

- Sources (read 2026-10-30): https://vittorioromeo.com/index/blog/refl_compiletime.html
- Setup: Fedora 44 container, Intel i9-13900K, `gcc 16.0.1 20260305 (Red Hat 16.0.1-0)` built with `--enable-checking=release`, flags `-std=c++26 -freflection`, timed with hyperfine. Single-translation-unit numbers, which the author calls not rigorous.
- Correction in the post: the first version used a GCC built with internal assertions, which made GCC look slower; the author re-ran on a release build and updated the post. The numbers below are the corrected ones.
- Results (mean per TU): empty `main` 33.2 ms; the same with `-freflection` 33.9 ms (the appendix table of the same post lists 32.0 ms for this row, an internal inconsistency on the page); `#include <meta>` 187.2 ms; reflecting one struct 200.3 ms; 10 distinct structs 210.0 ms; 20 structs 227.4 ms. With a precompiled header of `<meta>`: 91.7, 96.9 and 109.3 ms. Barry Revzin's array-of-structs to struct-of-arrays example: 818.9 ms unedited, 310.2 ms without `<print>`, 224.4 ms without `<print>` and `<ranges>`; with PCH 628.0, 137.4 and 113.7 ms.
- Author's reading: the flag itself is free; the header costs most; the per-type reflection work is about 1.1 ms per extra type from 1 to 10 types and about 1.7 ms per type from 10 to 20.
- Relevance to P5: the best public decomposition of fixed header cost versus per-type reflection cost on GCC 16. P5 should report `<meta>` cost separately from schema-size scaling.
- Checked 2026-10-30: confirmed, including the 32.0 ms inconsistency.

### 4.2 Romeo, "cost of enum-to-string: C++26 reflection vs the old ways" (2026-05-12)

- Sources (read 2026-10-30): https://vittorioromeo.com/index/blog/refl_enum_to_string.html
- Setup: `gcc 16.1.1 20260501 (Red Hat 16.1.1-1, release build)`, same i9-13900K, performance governor, compiler pinned to a core, hyperfine with 5 warmups and at least 20 runs.
- Results: header tax for `<meta>` about 155 ms (author's figure). Reflection cost above include-only, by enum size: 5.9 ms (4 enumerators), 6.9 ms (16), 10.3 ms (64), 34.2 ms (256), 74.2 ms (1024). Per-enumerator slope: reflection about 0.07 ms, X-macro with `std::string_view` about 0.06 ms, X-macro with `const char*` about 0.027 ms. The enchantum library scales with its scan range, not enum size.
- Relevance to P5: a measured per-element slope on a released GCC 16. P5's per-field and per-message slopes can be compared with it.
- Checked 2026-10-30: confirmed.

### 4.3 GCC Bugzilla 124925, "-freflection slows down compilation (of e.g. stdc++.h) by about 20%"

- Sources (read 2026-10-30): https://gcc.gnu.org/bugzilla/rest.cgi/bug/124925 ; https://gcc.gnu.org/bugzilla/rest.cgi/bug/124925/comment (the HTML page sits behind a bot check) ; https://gcc.gnu.org/releases.html
- Facts: opened 2026-04-18, keyword `compile-time-hog`, status RESOLVED FIXED, version 16.0. The first comment (user `ppalka`) measured `-fsyntax-only` on `stdc++.h` with a release-checking build: 1.42 s without and 1.69 s with `-freflection`, and named `consteval_only_p` and `check_out_of_consteval_use` at about 10% of compile time each. A second comment (user `jakub`) says the consteval-only flag is not cached on types. On 2026-09-14 a comment (user `mpolacek`) re-measured and reports about a 1.13% increase, and concludes it is fixed.
- Timing: that re-measurement is dated after GCC 16.2 (released 2026-08-07). The bug does not say in which commit or release branch the fix landed, or whether it was backported to 16.x.
- Relevance to P5: shows the flag cost is compiler-version dependent. P5 should pin the exact GCC build and say whether the fix is in it.
- Checked 2026-10-30: confirmed; the release-timing caveat was added (C21).

### 4.4 simdjson, 6% compile slow-down with static reflection

Cross-reference note (details in 2.13).

- Sources (read 2026-10-30): https://simdjson.github.io/simdjson_talks/cppcon2025/cppcon_2025_slides.pdf (slide 54)
- Facts: the slide reports a 6% slow-down when compiling simdjson with static reflection enabled, on the clang-p2996 experimental branch. Self-reported; the slide does not state the machine or baseline.
- Relevance to P5: one of the few published compile-time figures for reflection use in a real library, on Clang rather than GCC 16.
- Checked 2026-10-30: confirmed (slide 54 of 60, talk by Lemire and Geiman Thiesen).

### 4.5 mp-units-benchmarks issue 10 (A/B kit for P2996)

- Sources (read 2026-10-30): https://github.com/mpusz/mp-units-benchmarks/issues/10 (opened 2026-09-14 by `mpusz`, open; body read through the API)
- Facts: the issue describes a measurement kit comparing a value-domain, P2996-based implementation of mp-units against a type-domain baseline, needing GCC 16 or newer. It says wall time and peak RSS are the only instruments, because counting instantiations would need Clang's `-ftime-trace` and no upstream Clang has reflection, and it says the exchange rate between template instantiations and constant-evaluator work has not been published. It warns that the reflection-off arm is the wrong baseline, because it also drops `<meta>`, `<vector>` and `<string_view>`. Findings so far: the conversion engine about 10% to 12% faster to compile than the reflection-available baseline, with lower peak RSS, and the expression operations 8% to 33% slower on the ISQ-heavy translation unit. The issue itself says both findings were measured only on WSL, that WSL noise was large, and that both still need native confirmation.
- Relevance to P5: a useful warning about baselines (reflection compiled in versus off) and instruments. Its findings are self-reported and, by the issue's own account, unconfirmed.
- Checked 2026-10-30: confirmed; the author and status were added and the caveat is now sourced to the issue itself (M9).

### 4.6 P3429R1, "<meta> should minimize standard library dependencies"

- Sources (read 2026-10-30): https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3429r1.html
- Facts: by Jonathan Müller, dated 2024-11-15, audience LEWG. It argues that the P2996 `<meta>` header should depend on fewer standard headers than `std::vector`, `std::string_view` and `std::optional`, partly to cut compile time, because reflection will be used in widely included headers. It does not report measurements.
- Relevance to P5: explains why the `<meta>` include cost in 4.1 and 4.2 exists.
- Checked 2026-10-30: confirmed.

### 4.7 GCC constexpr evaluation limits

- Sources (read 2026-10-30): https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html
- Facts: `-fconstexpr-ops-limit=n` caps the operations in one constexpr evaluation, default 33554432 (1<<25). `-fconstexpr-loop-limit=n` caps iterations of one loop, default 262144 (1<<18). `-fconstexpr-depth=n` sets nested depth (the docs give 512 as the standard's minimum). `-fconstexpr-cache-depth=n` trades compile speed against memory, default 8. `-freflection` is listed as "Enable experimental C++26 Reflection". The docs are those online on 2026-10-30.
- Relevance to P5: a compile-time `.proto` parser that scans a large schema can hit the operations limit; the schema-size sweep should record the limit used.
- Checked 2026-10-30: confirmed.

### 4.8 Clang `-ftime-trace`, Aras Pranckevičius, ClangBuildAnalyzer

- Sources (read 2026-10-30): https://clang.llvm.org/docs/UsersManual.html ; https://aras-p.info/blog/2019/01/16/time-trace-timeline-flame-chart-profiler-for-Clang/ ; https://releases.llvm.org/9.0.0/tools/clang/docs/ReleaseNotes.html ; https://github.com/aras-p/ClangBuildAnalyzer ; https://api.github.com/repos/aras-p/ClangBuildAnalyzer/releases/latest ; https://gcc.gnu.org/bugzilla/rest.cgi/bug/92396
- Facts: the Clang user manual documents `-ftime-trace[=<path>]` (Chrome tracing JSON), `-ftime-trace-granularity` (500 microseconds default) and `-ftime-trace-verbose`. Aras's post (2019-01-16) introduces the flag; an update in it says the change landed on 2019-03-30 and Clang 9.0 should contain it. The Clang 9.0.0 release notes mention `-ftime-trace` and `-ftime-trace-granularity=N`. ClangBuildAnalyzer analyses these traces; latest release v1.6.0, 2025-01-05; Unlicense (GitHub API).
- GCC: GCC Bugzilla 92396, "-ftime-trace support", opened 2019-11-06 against version 10.0, had status NEW on 2026-10-30, so no fix had landed under that bug.
- Relevance to P5: the main profiling tool for per-phase compile cost. P5's measured compiler is GCC, so P5 must rely on wall time, peak RSS and `-ftime-report` (not opened in this survey).
- Checked 2026-10-30: confirmed; the GCC bug status was read (M10).

### 4.9 Templight

- Sources (read 2026-10-30): https://github.com/mikael-s-persson/templight ; https://github.com/mikael-s-persson/templight/blob/master/README.md ; https://doi.org/10.1145/1173706.1173746 ; https://api.openalex.org/works/doi:10.1145/1173706.1173746
- Facts: Templight 2.0 is a Clang-based tool that profiles time and memory of template instantiations and offers a GDB-style template debugger. The profiler is a drop-in replacement for clang that records entry and exit time stamps per instantiation. The README credits the original version to Zoltán Borók-Nagy, Zoltán Porkoláb and József Mihalicza. No GitHub release or tag was returned by the API; last push 2024-12-07; GitHub reports license `NOASSERTION`.
- Academic: "Debugging C++ template metaprograms" by Porkoláb, Mihalicza and Sipos, GPCE 2006 (ACM), DOI: 10.1145/1173706.1173746. Its abstract says the paper introduces Templight, a debugging framework for template instantiation.
- Relevance to P5: a template-instantiation profiler. P5's reflection design avoids deep template recursion, which is the cost Templight measures; P1306R5 (5.2) makes the same argument.
- Checked 2026-10-30: confirmed; the paper's link to Templight was settled from its abstract (M11).

### 4.10 ctbench

- Sources (read 2026-10-30): https://github.com/JPenuchot/ctbench ; https://api.github.com/repos/JPenuchot/ctbench/releases/latest ; https://doi.org/10.21105/joss.05165 ; https://joss.theoj.org/papers/10.21105/joss.05165 ; https://api.openalex.org/works/doi:10.21105/joss.05165
- Facts: "ctbench - compile-time benchmarking and analysis" by Jules Penuchot and Joel Falcou, Journal of Open Source Software, 2023, DOI: 10.21105/joss.05165. The abstract says there is no clear methodology for comparing the compile-time impact of metaprogramming strategies, and cites compile-time containers and static reflection as growing needs. The README says it generates variably sized compile-time benchmark batches, uses Clang's time-trace, and measures compiler execution time for compilers without a profiler, such as GCC. Latest release v1.3.5, 2026-03-30; MIT (GitHub API).
- Relevance to P5: a published methodology for exactly the P5 experiment, compile time against input size, usable with GCC.
- Checked 2026-10-30: confirmed.

### 4.11 Metabench

- Sources (read 2026-10-30): https://github.com/ldionne/metabench ; https://github.com/ldionne/metabench/blob/master/README.md ; http://metaben.ch
- Facts: a CMake module for compile-time microbenchmarks; the README says nightly results of metaprogramming libraries are published at http://metaben.ch. On 2026-10-30 that host name did not resolve from the checker's machine (curl exit code 6). Last push 2021-05-01; GitHub reports license `NOASSERTION`; no release found.
- Relevance to P5: older precedent for compile-time microbenchmarks; superseded in practice by ctbench for GCC use.
- Checked 2026-10-30: confirmed; the site check was replaced by the observation above (R3).

### 4.12 Clang's constant-expression interpreter

- Sources (read 2026-10-30): https://clang.llvm.org/docs/ConstantInterpreter.html ; https://www.redhat.com/en/blog/new-constant-expression-interpreter-clang (Timm Baeder, 2022-11-23) ; https://en.cppreference.com/w/cpp/26.html
- Facts: the Clang docs say the bytecode interpreter aims to replace the AST-traversal evaluator, is enabled with `-fexperimental-new-constant-interpreter`, and from Clang 23 can be made the default with a CMake option. Baeder's post gives one loop example (100,000 iterations) taking 0.9 s in the new interpreter and 25.2 s in the old one, on unoptimized debug builds of Clang, and calls the number meaningless as a comparison.
- Reflection: on 2026-10-30, cppreference's C++26 table gave a version only for GCC (16) in the Reflection row, and the mp-units issue (4.5) says no upstream Clang has reflection. So whether either Clang evaluator handles P2996 is a question for the clang-p2996 fork only.
- Relevance to P5: constexpr evaluation speed is compiler-dependent, and a Clang cross-check of P5 can only use the fork.
- Checked 2026-10-30: confirmed; the reflection question was restated from opened sources (M12).

### 4.13 Constexpr parsing precedents: CTRE, poacher, and json2cpp as a contrast

- Sources (read 2026-10-30): https://github.com/hanickadot/compile-time-regular-expressions ; https://api.github.com/repos/hanickadot/compile-time-regular-expressions/releases/latest ; https://github.com/JPenuchot/poacher ; https://github.com/lefticus/json2cpp ; https://github.com/lefticus/json2cpp/blob/main/README.md
- Facts: CTRE (Hana Dusíková) parses regular expressions at compile time; latest release v3.11.0, 2026-05-08; Apache-2.0 (GitHub API). P3068R6 (5.8) uses CTRE in a date-parsing example. Poacher's README describes compile-time parsing experiments for arbitrary eDSLs in C++20, names CTRE and the work of Jason Turner and Ben Deane as pioneers, and says its original parser-generator goal is now nearly met by ctpg (peter-winter/ctpg, not opened); no license, last push 2024-10-21.
- json2cpp (Jason Turner): the README says it compiles a JSON file into `static constexpr` data structures with an nlohmann::json compatible API. It is an offline generator that emits C++ source; the parsing happens in the tool, not in the compiler. MIT; last push 2024-05-06.
- Published measurements: none read from these pages.
- Relevance to P5: CTRE and poacher are the lineage of constexpr text parsing in C++. json2cpp is the generator-based alternative for JSON, as protoc is for protobuf. None is protobuf or P2996.
- Checked 2026-10-30: confirmed, except json2cpp, which was wrongly grouped as a constexpr parser and was corrected after reading its README (C22).

### 4.14 What was found on P2996 and constexpr compile cost, in one place

Summary note (no sources of its own; sources are in the items cited). Per-feature figures exist for P2996 (4.1, 4.2, 4.3, 2.13 and 4.5). Constexpr evaluation cost has been measured in other settings: a 2019 WG21 paper on reflection API style (4.15), a parallel constexpr evaluator (4.16), and template-based runtime hooks in D (6.4). Neither agent found a published study of compile time against schema or input size for reflection-generated types, and none comparing generated types with protoc output. The mp-units issue states the instantiation-versus-evaluation exchange rate is unpublished.

- Relevance to P5: the compile-time experiment (cost against schema size, against protoc-generated C++) is not covered by published data, so P5 must supply it.
- Checked 2026-10-30: updated with the items added by the checker (C23).

### 4.15 P1447R0, "constexpr C++ is not constexpr C" (added by the checker)

- Sources (read 2026-10-30): https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1447r0.pdf
- Facts: by Matúš Chochlík, Axel Naumann and David Sankel, dated 2019-01-21, audience SG7, Evolution and Library Evolution. It argues for a typed, object-oriented constexpr reflection API over free functions on a single opaque type, and measures the compile-time cost of the two styles with Clang. Stateless benchmark (10000 generated constexpr functions, `-fsyntax-only`, Apple LLVM 10.0.0 on a 2014 MacBook with a 2.5 GHz Core i7): 2.969 s for the free-function style and 2.702 s for the object-oriented style, which the authors call within random fluctuation. Stateful benchmark on a modified Clang of 2019-01-02: 2.674 s for the free-function style and 4.020 s for the object-oriented style (self-reported).
- Relevance to P5: an early WG21 measurement of constexpr reflection-style API cost against a generated workload size. It predates P2996 and does not concern schemas, but it is a precedent for timing reflection-like constexpr code in committee papers.
- Checked 2026-10-30: added by the checker (A7).

### 4.16 ClangOz: parallel evaluation of constant expressions (added by the checker)

- Sources (read 2026-10-30): https://doi.org/10.1007/978-3-031-51075-5_6 ; https://myresearchspace.uws.ac.uk/ws/files/45103546/2023_11_01_Gozillon_et_al_Performance_accepted.pdf (accepted manuscript)
- Facts: "Performance Analysis of Compiler Support for Parallel Evaluation of C++ Constant Expressions" by Andrew Gozillon, Seyed Hossein Haeri, James Riordan and Paul Keir. Crossref lists it as a 2024 chapter in Lecture Notes in Business Information Processing (Springer Nature Switzerland); the manuscript calls it an extended version of the authors' earlier paper. DOI: 10.1007/978-3-031-51075-5_6 (resolves at doi.org). The abstract names longer compilation times as a cost of constexpr metaprogramming and presents ClangOz, a Clang-based research compiler that evaluates annotated constexpr loops in parallel. Benchmarks (Mandelbrot, BlackScholes, N-Body, Swaptions and a SYCL Sobel filter) ran on an Intel i9-13900K under Ubuntu 23.10; the abstract reports reduced compile times and 100% parallel efficiency in two cases (self-reported). Because stock Clang cannot time parts of a constant evaluation, the authors added timing intrinsics.
- Not P2996, not a text parser (the BlackScholes input data is brought in with `#include`), not protobuf.
- Relevance to P5: a peer-reviewed measurement of constexpr evaluation time on the same CPU family as Romeo's posts, and a note that per-phase timing of constant evaluation needs compiler support that stock compilers lack.
- Checked 2026-10-30: added by the checker (A8).

---

## 5. Standards documents

All fetched from open-std.org and read locally on 2026-10-30. "Final revision" means the revision `wg21.link/<number>` resolved to on that day.

### 5.1 P2996, Reflection for C++26

- Sources: https://wg21.link/P2996 (resolved to https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html) ; https://gcc.gnu.org/projects/cxx-status.html
- Facts: P2996R13, dated 2025-06-20, audience CWG and LWG. Authors: Wyatt Childers, Peter Dimov, Dan Katz, Barry Revzin, Andrew Sutton, Faisal Vali, Daveed Vandevoorde. R13 changes: core wording for members of static anonymous unions (CWG3026 fix) and EWG updates from P3687R0; library wording for `reflect_constant`, `reflect_object` and `reflect_function`. Section 2.3 says Lock3 implemented much of the equivalent in a Clang fork based on the earlier P1240 proposal, and that EDG and Bloomberg now have implementations; it says neither of those two is complete and that they lack expansion statements. `define_aggregate` and `data_member_spec` are in the paper; its revision history notes (in the entry for changes since R8) that `define_aggregate` cannot be used for a class currently being defined. The paper does not discuss compile-time cost.
- Status: cppreference's C++26 table lists P2996R13 with P3096R12, P3293R3, P3394R4 and P3795R2 as "Reflection", GCC 16 (https://en.cppreference.com/w/cpp/26.html). The GCC 16 changes page lists P2996R13 (PR120775), enabled with `-std=c++26 -freflection` (https://gcc.gnu.org/gcc-16/changes.html), and GCC's C++ status page lists it for GCC 16 with `-freflection` required. Revzin's post says C++26 was finalized in Sofia and lists the reflection papers; the isocpp post of Sutter says the committee voted the first seven reflection papers into the draft (https://isocpp.org/blog/2025/06/reflection-voted-into-cpp26-whole-new-language-herb-sutter).
- Relevance to P5: the core language feature. `define_aggregate` is the type-synthesis step.
- Checked 2026-10-30: confirmed; the description of section 2.3 was corrected (C24).

### 5.2 P1306, Expansion statements

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1306r5.html
- Facts: P1306R5, dated 2025-06-20, audience CWG; authors Katz, Sutton, Goodrick, Vandevoorde, Revzin. Adds `template for`: compile-time repetition over a tuple, array, class, range or expression list. The introduction argues recursive template instantiation can consume significant compile time and memory, and that repetition should be cheap. Listed in the GCC 16 changes (PR120776) and in cppreference's C++26 table as GCC 16.
- Relevance to P5: the iteration construct for walking fields and messages without recursive templates.
- Checked 2026-10-30: confirmed.

### 5.3 P3394, Annotations for Reflection

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html
- Facts: P3394R4, dated 2025-06-19; authors Childers, Katz, Revzin, Vandevoorde. Adds `[[= expr]]` annotations queryable by reflection. Motivating examples: command-line parsing, test parametrization, and serialization (2.11). In GCC 16 and cppreference's table.
- Relevance to P5: needed only if P5 attaches protobuf options to hand-written structs; used by grpcxx (2.17), Iguana (2.4), struct_proto26 (2.14) and Hutorny (2.19).
- Checked 2026-10-30: confirmed.

### 5.4 P3096, Function parameter reflection

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3096r12.pdf
- Facts: P3096R12, dated 2025-06-20; authors Adam Lach, Dan Katz, Walter Genovese. Adds reflection of function parameters. In GCC 16 and cppreference's table.
- Relevance to P5: marginal. It matters only if endpoints are derived from hand-written function signatures, not from `.proto` service text.
- Checked 2026-10-30: confirmed.

### 5.5 P3560, Error handling in reflection, and P3491, define_static_{string,object,array}

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3560r2.html ; https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3491r3.html
- Facts: P3560R2 (2025-06-17; Dimov, Revzin; audience EWG and LEWG) specifies an exception type for reflection errors; its contents list covers exception type encoding and recoverable versus unrecoverable errors. P3491R3 (2025-06-20; Childers, Dimov, Revzin, Vandevoorde) adds `define_static_string`, `define_static_object` and `define_static_array`, which promote computed constexpr data to static storage. Both are in the GCC 16 changes page. Romeo's examples (4.1, 4.2) call `std::define_static_array`.
- Relevance to P5: P3491 is how parsed schema data survives out of the consteval phase; P3560 is the diagnostic path for bad `.proto` input.
- Checked 2026-10-30: confirmed.

### 5.6 #embed: P1967 (C++) and N3017 (C23)

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1967r14.html ; https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3017.htm ; https://gcc.gnu.org/gcc-15/changes.html ; https://en.cppreference.com/w/cpp/26.html
- Facts: P1967R14 (JeanHeyd Meneide; title block "Draft Proposal, 2025-02-14"; its changelog lists revision 14 on February 13th, 2025). N3017, "Published Proposal, 2022-06-27", is the C23 paper, same author. GCC 15 changes list P1967R14 (PR119065) and C23 `#embed`. cppreference lists #embed in C++26 with GCC 15 and Clang 19.
- Published measurements (self-reported by the paper, section 3.2; machine: Intel Core i7 2.60 GHz, 24 GB RAM, Debian Sid or Windows 10; the paper says it used the latest compiler built from source where available, without version numbers): time to compile a data array. At 40 kB: `#embed` with GCC 0.236 s, xxd-generated array with GCC 0.406 s. At 4 MB: 0.300 s versus 23.567 s (GCC), 8.309 s (Clang), 52.397 s (MSVC). At 40 MB: 1.069 s versus 225.290 s (GCC) and 83.250 s (Clang); MSVC ran out of memory. Peak memory at 40 MB: 341.72 MB for `#embed` with GCC versus 12,622 MB for xxd arrays with GCC. The paper says the memory figures are approximate, taken by fixed-interval probes of OS reporting.
- Relevance to P5: `#embed` is the input path, so schema text cost is negligible compared with array-literal inclusion. `.proto` files are small; the numbers matter only for the claim that `#embed` is cheap.
- Checked 2026-10-30: confirmed; the memory caveat and the compiler-version statement were added (C25).

### 5.7 P2758, Emitting messages at compile time

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2758r5.html ; https://github.com/cplusplus/papers/issues/1445
- Facts: P2758R5 by Barry Revzin, dated 2025-02-11, audience CWG and LWG. R5 added wording and re-targeted LWG. It proposes library functions that emit messages at compile time (and the idea of constexpr-erroneous). Status on 2026-10-30: the cplusplus/papers issue 1445 is open and carries the labels `C++29` and `needs-revision`, among others. It is absent from the GCC 16 changes page and from cppreference's C++26 table.
- Related and adopted: P2741R3, "user-generated static_assert messages", Corentin Jabot, dated 2023-06-16 (https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2741r3.pdf); cppreference lists it under C++26 with GCC 14 and Clang 17.
- Relevance to P5: C++26 has no `constexpr_error` style API. Diagnostics for bad schema text must use `static_assert` with a computed message (P2741), exceptions in constant evaluation (5.8) or reflection errors (5.5).
- Checked 2026-10-30: confirmed; the `needs-revision` label was added (C26).

### 5.8 P3068, constexpr exceptions

- Sources: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3068r6.html ; https://gcc.gnu.org/gcc-16/changes.html ; https://gcc.gnu.org/projects/cxx-status.html
- Facts: P3068R6, dated 2024-11-19, by Hana Dusíková; allows throwing and catching exceptions during constant evaluation. An uncaught throw in a constant expression stays an error, which the paper notes libraries such as fmt and CTHASH already use for compile-time errors. Its date-parsing example uses `ctre::match`. The GCC 16 changes page and GCC's C++ status page list P3068R5 (PR117785), the status page with feature-test macro `__cpp_constexpr_exceptions >= 202411L`; cppreference's table lists P3068R6 for GCC 16. Which revision GCC implements exactly is not stated by any source opened (unverified).
- Relevance to P5: lets a `consteval` `.proto` parser throw a descriptive error for malformed input and still support `try`/`catch` inside the parser.
- Checked 2026-10-30: confirmed; the GCC status page entry was added and the open question carries the standard marker (C27).

### 5.9 Status of C++26 itself

- Sources (read 2026-10-30): https://herbsutter.com/2026/03/ ; https://gcc.gnu.org/releases.html ; https://isocpp.org/blog/2026/04/gcc-16.1 ; https://isocpp.org/std/status ; https://train-it.eu/blog/cpp26-contracts-the-ballot-result/ ; https://news.ycombinator.com/item?id=50239748 ; https://zeli.app/story/50239748 (a plain fetch returned no article text) ; https://ecosistemastartup.com/?p=121998 (not opened)
- Facts verified: Herb Sutter's trip report for the March 2026 meeting says the committee finished technical work on C++26 in London (Croydon), resolving the national body comments, and that the draft goes next to its international approval ballot (DIS). GCC 16.1 was released 2026-04-30 and GCC 16.2 on 2026-08-07 (https://gcc.gnu.org/releases.html); the isocpp post on 16.1 lists reflection and contracts (https://isocpp.org/blog/2026/04/gcc-16.1). isocpp.org lists the November 16 to 21, 2026 WG21 meeting in Búzios, Brazil.
- DIS ballot: Mateusz Pusz (train-it.eu, October 25, 2026) reports that the ballot closed on 23 October 2026 and that C++26 was disapproved, quoting a LinkedIn post of Robert Seacord, convenor of the C committee, dated 24 October 2026. He gives 12 of 20 P-member votes in favour (two thirds needed) and 9 of 22 negative votes (at most a quarter allowed), and says the official SC22 voting result is not public. Secondary reports (zeli.app, ecosistemastartup.com) carry the same story. No ISO or WG21 primary document confirming the result was found, so the failed ballot remains a reported fact (unverified).
- Advice: if the paper says "C++26", phrase it as "the C++26 draft" until the status is confirmed by a primary source.
- Relevance to P5: fixes what the paper may call the language version, and which GCC release (16.1 on 2026-04-30, 16.2 on 2026-08-07) first shipped `-freflection`.
- Checked 2026-10-30: confirmed; the ballot report was traced to a named author and quoted source, and stays marked (C28).

---

## 6. Academic papers on protobuf performance, serialization benchmarks, gRPC performance and generated serialization code

Each paper below lists openable sources (read 2026-10-30): the DOI link and the OpenAlex record that holds the abstract (or Semantic Scholar where noted). The checker resolved every DOI at doi.org and compared the returned title and authors. Claims are limited to the abstracts. None of these papers compares compile time of protobuf code, or schema-to-type derivation at compile time.

### 6.1 Protobuf and datacenter-scale serialization

- Karandikar, Leary, Kennelly, Zhao and others, "A Hardware Accelerator for Protocol Buffers", MICRO-54, 2021. DOI: 10.1145/3466752.3480051.
  - Sources: https://doi.org/10.1145/3466752.3480051 ; https://api.openalex.org/works/doi:10.1145/3466752.3480051
  - The abstract says it presents the first in-depth study of serialization framework usage at scale by profiling protobuf across Google's fleet, and builds the open-source benchmark HyperProtoBench. No fleet percentages are claimed here.
  - Relevance to P5: describes protobuf usage at scale and supplies a benchmark with realistic message shapes that P5's runtime arm could draw on. No compile-time or reflection angle.
  - Checked 2026-10-30: confirmed.
- Kanev, Darago, Hazelwood, Ranganathan and others, "Profiling a warehouse-scale computer", ISCA 2015. DOI: 10.1145/2749469.2750392.
  - Sources: https://doi.org/10.1145/2749469.2750392 ; https://api.openalex.org/works/doi:10.1145/2749469.2750392
  - The abstract (OpenAlex holds only its opening; Semantic Scholar has it elided; the ACM PDF was blocked to a plain fetch) describes microarchitectural analysis of live datacenter jobs on more than 20,000 Google machines over three years. It does not name protobuf there; any protobuf finding in the body is (unverified).
  - Relevance to P5: background on server workloads only; no compile-time angle.
  - Checked 2026-10-30: confirmed as far as the abstract goes.
- Sriraman and Dhanotia, "Accelerometer", ASPLOS 2020 (Crossref gives only this short title; Semantic Scholar gives "Accelerometer: Understanding Acceleration Opportunities for Data Center Overheads at Hyperscale"). DOI: 10.1145/3373376.3378450.
  - Sources: https://doi.org/10.1145/3373376.3378450 ; https://api.openalex.org/works/doi:10.1145/3373376.3378450 ; https://api.semanticscholar.org/graph/v1/paper/DOI:10.1145/3373376.3378450
  - The full abstract characterizes the top seven microservices of Facebook's compute-optimized fleet, reports that core application logic can take as little as 18% of CPU cycles, names I/O processing, logging and compression as examples of the remaining work, and presents an analytical model for acceleration gains. It does not mention serialization.
  - Relevance to P5: context for datacenter overheads only; cite it for serialization, if at all, only after reading the body.
  - Checked 2026-10-30: confirmed; the full abstract was read (C29, M13).
- Pourhabibi, Gupta, Kassir, Sutherland and others, "Optimus Prime: Accelerating Data Transformation in Servers", ASPLOS 2020. DOI: 10.1145/3373376.3378501.
  - Sources: https://doi.org/10.1145/3373376.3378501 ; https://api.openalex.org/works/doi:10.1145/3373376.3378501
  - The abstract identifies data transformation (message conversion between formats) as a bottleneck for microservices and proposes a programmable accelerator with an in-memory schema, reporting about 60 times the throughput of software; evaluated with Thrift microservices.
  - Relevance to P5: supports the claim that message conversion cost matters; hardware focus, no compile-time or reflection angle.
  - Checked 2026-10-30: confirmed.
- Pourhabibi, Sutherland, Daglis, Falsafi, "Cerebros: Evading the RPC Tax in Datacenters", MICRO-54, 2021. DOI: 10.1145/3466752.3480055.
  - Sources: https://doi.org/10.1145/3466752.3480055 ; https://api.openalex.org/works/doi:10.1145/3466752.3480055
  - The abstract argues the RPC layer accounts for 40% to 90% of a microservice's total execution cycles and proposes a NIC-attached accelerator for all RPC layer modules.
  - Relevance to P5: motivates why the RPC layer is worth optimizing; hardware focus, no compile-time angle.
  - Checked 2026-10-30: confirmed.
- Raghavan, Levis, Zaharia, Zhang, "Breakfast of champions", HotOS 2021 (short title as in the Crossref record; Semantic Scholar adds "towards zero-copy serialization with NIC scatter-gather"). DOI: 10.1145/3458336.3465287.
  - Sources: https://doi.org/10.1145/3458336.3465287 ; https://api.openalex.org/works/doi:10.1145/3458336.3465287
  - The abstract argues microsecond I/O will make serialization a major bottleneck.
  - Relevance to P5: motivation only; no compile-time angle.
  - Checked 2026-10-30: confirmed.
- Raghavan, Ravi, Yuan, Thaker and others, "Cornflakes: Zero-Copy Serialization for Microsecond-Scale Networking", SOSP 2023. DOI: 10.1145/3600006.3613137.
  - Sources: https://doi.org/10.1145/3600006.3613137 ; https://api.openalex.org/works/doi:10.1145/3600006.3613137
  - The abstract describes a hybrid serialization library using scatter-gather when beneficial; 15.4% higher throughput than prior software approaches on a custom key-value store with a Twitter trace, and 8.8% higher than Redis serialization within Redis.
  - Relevance to P5: a serialization comparison point that is not protobuf; no compile-time angle.
  - Checked 2026-10-30: confirmed.
- Chen, Guan, Shuai, Huang, Aibin, "Lite2: A Schemaless Zero-Copy Serialization Format", Computers (MDPI) 2024. DOI: 10.3390/computers13040089.
  - Sources: https://doi.org/10.3390/computers13040089 ; https://api.openalex.org/works/doi:10.3390/computers13040089
  - The abstract describes a schemaless zero-copy format built on a contiguously stored B-tree. It is a separate format, not protobuf.
  - Relevance to P5: an MDPI reference on serialization trade-offs only; not comparable to a schema-driven protobuf codec.
  - Checked 2026-10-30: confirmed; the author list was completed (C30).
- Liu, Li, Gai, Chen, Zhang, "zBuffer: Zero-Copy and Metadata-Free Serialization for Fast RPC with Scatter-Gather Reflection", PPoPP 2026. DOI: 10.1145/3774934.3786426.
  - Sources: https://doi.org/10.1145/3774934.3786426 ; https://api.openalex.org/works/doi:10.1145/3774934.3786426 (details in 2.18)
  - Relevance to P5: the nearest academic precedent for compile-time reflection used for RPC serialization, built with templates and macros before P2996. It is not protobuf and reports no compile-time cost.
  - Checked 2026-10-30: confirmed; the author list was completed (C18).

### 6.2 Serialization benchmarks

- Viotti and Kinderkhedia, "A Benchmark of JSON-compatible Binary Serialization Specifications", arXiv:2201.03051 [cs.SE], submitted 9 Jan 2022. DOI: not found for a journal version; the arXiv page shows the arXiv-issued DOI 10.48550/arXiv.2201.03051.
  - Sources: https://arxiv.org/abs/2201.03051 ; https://doi.org/10.48550/arXiv.2201.03051
  - The abstract says it benchmarks schema-driven formats (ASN.1, Avro, Bond, Cap'n Proto, FlatBuffers, Protocol Buffers, Thrift) and schema-less ones (BSON, CBOR, FlexBuffers, MessagePack, Smile, UBJSON) on over 400 JSON documents from SchemaStore.
  - Relevance to P5: a methodology and dataset reference for comparing protobuf with other formats; size and speed only, no compile-time angle.
  - Checked 2026-10-30: confirmed on the arXiv abstract page.
- Jackson, Cummings, Khan, "Streaming Technologies and Serialization Protocols: Empirical Performance Analysis", IEEE Access, 2024. DOI: 10.1109/access.2024.3486054.
  - Sources: https://doi.org/10.1109/access.2024.3486054 ; https://api.openalex.org/works/doi:10.1109/access.2024.3486054
  - The abstract describes an empirical study of streaming technologies and serialization protocols, with an open-source benchmarking framework.
  - Relevance to P5: runtime comparison only; no compile-time or reflection angle.
  - Checked 2026-10-30: confirmed.
- Maeda, "Performance evaluation of object serialization libraries in XML, JSON and binary formats", DICTAP 2012. DOI: 10.1109/dictap.2012.6215346.
  - Sources: https://doi.org/10.1109/dictap.2012.6215346 ; https://api.openalex.org/works/doi:10.1109/dictap.2012.6215346
  - The abstract compares twelve libraries by serialized size and processing time and concludes there is no best solution.
  - Relevance to P5: early runtime comparison of serialization libraries; no compile-time angle.
  - Checked 2026-10-30: confirmed.
- Sumaray and Makki, "A comparison of data serialization formats for optimal efficiency on a mobile platform", ICUIMC 2012. DOI: 10.1145/2184751.2184810.
  - Sources: https://doi.org/10.1145/2184751.2184810 ; https://api.openalex.org/works/doi:10.1145/2184751.2184810
  - The abstract compares XML, JSON, Thrift and ProtoBuf on an Android device for speed, size and usability.
  - Relevance to P5: an early protobuf-versus-Thrift runtime comparison; no compile-time angle.
  - Checked 2026-10-30: confirmed.
- "Impact of Serialization Format on Inter-Service Latency", Advances in Cyber-Physical Systems, 2024. The doi.org metadata lists Eduard Maltsev and Riaz Ul Amin as authors, plus two institution names (Lviv Polytechnic National University; Edinburgh Napier University) placed in author fields. DOI: 10.23939/acps2024.02.089.
  - Sources: https://doi.org/10.23939/acps2024.02.089 ; https://api.openalex.org/works/doi:10.23939/acps2024.02.089
  - The abstract finds payload compactness matters more than serialization speed for end-to-end latency; FlatBuffers and Cap'n Proto underperform in distributed settings while Avro, Thrift and Protobuf are more balanced.
  - Relevance to P5: cautions that codec speed alone may not move end-to-end gRPC latency; useful when interpreting the unary-call results.
  - Checked 2026-10-30: confirmed; the author names were read from the metadata (C31).
- Shatnawi, Bahri, Niang, Verhaeghe, "Enhancing Data Serialization Efficiency in REST Services: Migrating from JSON to Protocol Buffers", ICSOFT 2025. DOI: 10.5220/0013459500003964.
  - Sources: https://doi.org/10.5220/0013459500003964 ; https://api.openalex.org/works/doi:10.5220/0013459500003964
  - The abstract describes a semi-automated migration of REST services from JSON to protobuf; payload size reduced by 60% to 80% on two web applications.
  - Relevance to P5: payload-size evidence for protobuf over JSON; no compile-time angle.
  - Checked 2026-10-30: confirmed.

### 6.3 gRPC and API performance

- Śliwa and Pańczyk, "Performance comparison of programming interfaces on the example of REST API, GraphQL and gRPC", Journal of Computer Sciences Institute, 2021. DOI: 10.35784/jcsi.2744.
  - Sources: https://doi.org/10.35784/jcsi.2744 ; https://api.openalex.org/works/doi:10.35784/jcsi.2744
  - The abstract reports that with k6 tests on three equivalent applications, REST gave the best transactions per second and response time, while gRPC gave the smallest data volume.
  - Relevance to P5: shows gRPC is not always faster end to end, so P5 should compare against protoc-generated gRPC code, not against REST.
  - Checked 2026-10-30: confirmed.
- Niswar, Safruddin, Bustamin, Aswad, "Performance evaluation of microservices communication with REST, GraphQL, and gRPC", International Journal of Electronics and Telecommunications, 2024. DOI: 10.24425/ijet.2024.149562.
  - Sources: https://doi.org/10.24425/ijet.2024.149562 ; https://api.openalex.org/works/doi:10.24425/ijet.2024.149562
  - The abstract reports gRPC had the fastest response time and GraphQL the highest CPU utilization, for 100 to 500 requests.
  - Relevance to P5: another end-to-end gRPC measurement; runtime only.
  - Checked 2026-10-30: confirmed.
- Soares and Terra, "Go x Java e gRPC x REST: Um estudo empírico", SBLP 2024. DOI: 10.5753/sblp.2024.3686.
  - Sources: https://doi.org/10.5753/sblp.2024.3686 ; https://api.openalex.org/works/doi:10.5753/sblp.2024.3686
  - The abstract (Portuguese) says a factorial design found the communication architecture explained 94% of performance influence and the language 2%; gRPC did better on usual-size requests and REST on very large ones.
  - Relevance to P5: involves Go and gRPC, but compares REST with gRPC, not code generation strategies.
  - Checked 2026-10-30: confirmed.
- Non-peer-reviewed REST versus gRPC benchmark posts exist (for example on Medium). Neither agent opened any of them and none is cited.

### 6.4 Compile-time, reflection and C++ metaprogramming papers

- Porkoláb, Mihalicza, Sipos, "Debugging C++ template metaprograms", GPCE 2006. DOI: 10.1145/1173706.1173746.
  - Sources: https://doi.org/10.1145/1173706.1173746 ; https://api.crossref.org/works/10.1145/1173706.1173746 ; https://api.openalex.org/works/doi:10.1145/1173706.1173746
  - The abstract says the paper introduces Templight, a framework that shows the steps the compiler executes while instantiating templates, with breakpoints and inspection of metaprogram information.
  - Relevance to P5: foundational template-metaprogram debugging work, and the origin of Templight (4.9).
  - Checked 2026-10-30: confirmed; the abstract was found in OpenAlex (M14).
- Penuchot and Falcou, "ctbench - compile-time benchmarking and analysis", JOSS 2023. DOI: 10.21105/joss.05165.
  - Sources: https://doi.org/10.21105/joss.05165 ; https://api.openalex.org/works/doi:10.21105/joss.05165 ; https://joss.theoj.org/papers/10.21105/joss.05165 (details in 4.10)
  - Relevance to P5: a published methodology for compile time against input size; the closest peer-reviewed support for P5's compile-time experiment design.
  - Checked 2026-10-30: confirmed.
- Duţu, Guiman, Niţu, Stăniloiu, "From Runtime Reflection to Compile-Time Specialization: A Template-Based Approach to Runtime Libraries", ICSOFT 2026. DOI: 10.5220/0015065300004088.
  - Sources: https://doi.org/10.5220/0015065300004088 ; https://api.crossref.org/works/10.5220/0015065300004088 ; https://api.semanticscholar.org/graph/v1/paper/DOI:10.5220/0015065300004088
  - The abstract (Semantic Scholar) describes moving the D runtime library from TypeInfo-based runtime reflection to template-based hooks, and evaluates the trade-offs between performance, compilation time and binary size. It reports up to 99% better performance for the template-based versions with minimal impact on compilation time and binary size (self-reported), and says the approach extends to C++, Rust and Go.
  - Relevance to P5: a recent peer-reviewed measurement of compile time against a runtime alternative, in D, for generic code rather than schemas. Same shape of argument as P5 (compile-time specialization versus a runtime descriptor), different domain.
  - Checked 2026-10-30: confirmed; the abstract was found (M15).
- Gozillon, Haeri, Riordan, Keir, ClangOz chapter, 2024. DOI: 10.1007/978-3-031-51075-5_6. Details in 4.16 (added by the checker).
- Neither agent found a peer-reviewed paper on C++26 P2996 compile-time cost, and none on protobuf generation by reflection.

---

## 7. Gaps and closest prior work

### 7.1 Scoring the candidates

Constraints from the paper's approach: (a) reads schema text; (b) uses C++ P2996 reflection; (c) generates the message types from the schema, not only a codec for a struct the user wrote; (d) produces RPC or service endpoints; (e) needs no external generator or schema compiler. Y = yes, n = no, p = partly.

| Candidate | a | b | c | d | e | Note |
|---|---|---|---|---|---|---|
| dproto (D, 3.4) | Y | n | Y | p | Y | `.proto` text or file at compile time via string import; service interfaces and stub classes, no transport; last release 2020 |
| Froto.TypeProvider (F#, 3.8) | Y | n | Y | n | Y | generative type provider over `.proto` files; proto2 only; services not described; archived 2019 |
| exprotobuf (Elixir, 3.7) | Y | n | Y | n | Y | `use Protobuf` macro with the gpb parser; services not described |
| protobuf-nim (3.5) | Y | n | Y | n | Y | macro parses proto3 string or file; no services |
| protox (Elixir, 3.7) | Y | n | Y | n | n | compile-time macro, but calls protoc |
| simdjson `compile_time::parse_json` (2.13) | Y (JSON) | Y | Y | n | Y | released library; `#embed`; `define_aggregate` |
| Revzin and Katz JSON example (2.12) | Y (JSON) | Y | Y | n | Y | `#embed` plus `define_aggregate`; not protobuf |
| Dathskovsky blog sketch (2.20) | Y (toy schema) | p | Y | n | Y | described in prose only; no published code |
| struct_proto26 (2.14) | n | Y | n | n | Y | proto3 codec; struct is the schema |
| Iguana C++26 path (2.4) | n | Y | n | n | p | proto3 codec with conformance runner; protoc plugin exists separately |
| Hutorny article (2.19) | n | Y | n | n | Y | decoder only; `define_aggregate` for a presence-flag type |
| grpcxx (2.17) | p | Y | n | Y | p | P2996 and P3394 gRPC; contracts and messages from protoc, or written by hand with a user codec |
| hpp-proto (1.6) | p | n | Y | Y | n | complete C++23 library; protoc plugin |
| zpp_bits protobuf mode (2.1) | n | n | n | p | Y | aggregate reflection; own RPC, not gRPC |
| struct_pb (2.3) | n | n | n | n | p | macros or protoc plugin |
| protopuf (1.5) | n | n | n | n | Y | templates; schema as C++ types |
| prost with protox (3.1) | Y | n | Y | p | p | build.rs; pure-Rust protox removes protoc; gRPC through tonic-build |

Checked 2026-10-30: rows re-derived from the corrected items (C32).

### 7.2 Closest prior work

1. dproto (3.4) remains the only item found that does what P5 does in the strict sense outside C++: it reads `.proto` text (a string or a file through string import) at compile time, generates message types and codec, and generates service declarations (interfaces and stub classes), with no external generator. Three more tools do the schema-to-types step inside the compiler without services: Froto.TypeProvider in F# (3.8), exprotobuf in Elixir (3.7) and protobuf-nim (3.5). None has a gRPC transport, none uses C++ reflection, and none publishes measurements.
2. simdjson's `compile_time::parse_json` (2.13), with the Revzin and Katz example (2.12) it follows, is the closest C++ mechanism: `#embed` of a text file, a consteval parser, and type synthesis with `define_aggregate`, now in a released library. It handles JSON, not a protobuf schema, and has no endpoints. Its only published compile-cost figure (6%) is for building the whole library with static reflection enabled, not a measurement of `parse_json` against input size.
3. grpcxx (2.17) is the closest work on P5's two distinctive axes together, P2996 and gRPC endpoints. It fails (c) and does not read schema text in the compiler: its contracts and messages come from protoc, or are written by hand with a user-supplied codec. It is also new (repository created 2026-10-21, 1 star), makes no comparison with another gRPC implementation, and has no interoperability tests in its tree.
4. As supporting precedent for the codec side, three P2996 protobuf codecs run in the opposite direction, from a C++ struct: Iguana's C++26 path (2.4, with a conformance testee that reports 651 binary successes and 0 unexpected failures), struct_proto26 (2.14) and Hutorny's decoder (2.19). None reads `.proto` text in the compiler, and none publishes compile-time or run-time measurements against protoc-generated code.

Checked 2026-10-30: rewritten from the corrected items (C33).

### 7.3 What none of the items measures

Not found in searches on 2026-10-30, by either agent. Each line names the nearest miss.

- Compile time against schema size for in-compiler schema-to-types, compared with protoc-generated C++. Nearest misses: per-feature P2996 figures (4.1, 4.2, 4.3, 2.13, 4.5); constexpr timings for API styles (4.15) and for a parallel evaluator (4.16); compile time of template-based runtime hooks in D (6.4); reflect-cpp's unquantified claim that its C++26 mode compiles faster (2.8).
- The compile-time cost of a `consteval` text parser run over an embedded file. Nearest misses: the `#embed` data-inclusion figures in 5.6 (no parsing); simdjson's 6% figure, a whole-library build with static reflection enabled, not a sweep of a consteval parser over input size (2.13); ClangOz workloads, which `#include` their input data and parse no text (4.16).
- End-to-end unary gRPC calls of a P2996-generated implementation against protoc-generated code. Nearest misses: hpp-proto publishes codec runtime against Google protobuf (1.6); grpcxx publishes component benchmarks only (2.17).
- Interoperability of such an implementation with grpc-go. Nearest misses: gRPC publishes interop test descriptions (1.7); Iguana's conformance testee is wire-level only and involves no gRPC (2.4); grpcxx's tree has no interop tests (2.17).
- Peak memory of the compiler against schema size. Nearest misses: P1967 reports peak memory against embedded data size (5.6); the mp-units kit records peak RSS for a reflection A/B, not against schema size (4.5); clang-p2996's `P2996.md` says its memory use is not optimized, without figures (2.10).

Verdict of the check: the gap statement survives the checker's independent search, with the near misses above.

### 7.4 Search log (all on 2026-10-30)

Gatherer:

- GitHub repository search: "protobuf c++26", "p2996", "c++26 reflection", "protobuf constexpr", "constexpr protobuf", "compile-time protobuf", "grpc c++26", "grpc reflection p2996", "protobuf c++26 reflection", "proto3 serializer c++26", "p2996 rpc", "p2996 json serialization", "zig protobuf", "protobuf nim macro", "embedded proto".
- GitHub code search: "nonstatic_data_members_of proto3", "freflection grpc", "define_aggregate message_descriptor", "define_aggregate protobuf", "#embed .proto".
- Web search: constexpr `.proto` parser; C++26 reflection protobuf; reflect-cpp protobuf; P2996 compile time cost; protobuf `.pb.cc` compile time; Templight; `-ftime-trace`; Clang constant interpreter; Boost.Hana and Metabench; CTRE compile cost; zig comptime protobuf; prost-build and protox; gRPC versus REST studies; serialization benchmark papers; talks on protobuf with reflection.
- Crossref queries for each paper before its DOI was recorded.

Checker (independent):

- Web search: "C++26 reflection protobuf .proto parse compile time define_aggregate"; "P2996 reflection gRPC C++ generate service stubs without protoc"; "constexpr protobuf schema parser C++ #embed .proto file compile-time"; "Rust proc macro include .proto compile time protobuf without build.rs"; "Rust crate proc-macro parses .proto file at compile time"; "Zig comptime protobuf parse .proto @embedFile"; "compile time cost C++26 static reflection measurement study paper 2026"; "paper measuring compile time of constexpr evaluation C++ compilers"; "protobuf compile time generated code C++ .pb.cc build time schema size measurement"; "arXiv 2026 C++ static reflection P2996 serialization evaluation compile time"; "D language protobuf compile-time mixin .proto"; "exprotobuf Elixir use Protobuf from proto file compile time"; "C++26 DIS ballot result October 2026 ISO" and a variant.
- GitHub REST code search: "define_aggregate proto" (151 hits; the first 20 were inspected and none concerned protobuf), plus the `gh search code` forms of "define_aggregate proto3", "define_aggregate protobuf", "freflection protobuf", "nonstatic_data_members_of protobuf", "data_member_spec protobuf", "std::meta grpc" (no hits).
- GitHub REST repository search: "protobuf c++26", "proto reflection p2996", "grpc c++26", "protobuf consteval", "proto3 reflection c++", "compile-time protobuf parser", "protobuf freflection", "constexpr proto parser", "froto protobuf", "protobuf type provider fsharp".
- Found and added: items 1.12, 2.19, 2.20, 3.7, 3.8, 4.15 and 4.16, and the zigpb plan in 3.3.
- Screened and not added: KMX-Systems/kmx-grpc (C++26 gRPC wrapper over generated stubs); jbcoe/cc-protocol (`define_aggregate` for type-erased protocols, not protobuf); opticron/ProtocolBuffer (D; its README does not say generation happens at compile time); trunkszi/greptimedb-cxx (a client built on grpcxx); the Rust crates proto_rs and tonic_include_protos (seen only in search summaries, not opened); a 2025 Red Hat update on Clang's bytecode interpreter and an LLVM Discourse thread on constexpr evaluation speed (not opened).
- Unconfirmed lead: JeanHeyd Meneide's post of 2020-09-03 (https://thephd.dev/preprocessor-embed-std-embed-the-last-spring) says people had used his `phd::embed` patch to parse protobuf files at constexpr time. It names no project and none was found (unverified).

Limits: GitHub search is weak for multi-word queries; the web search tool is US-only; neither agent searched conference programs (CppCon, C++Now, Meeting C++) in depth. A library not indexed by these queries could exist.

---

## Change log of the check

Done on 2026-10-30 by the second agent (model Opus). Each entry names its source. Entry types: C = correction or clarification, R = removal, A = addition, M = unverified marker resolved.

Corrections and clarifications:

- C1. 1.1: the Haberman post is attributed to Josh Haberman, and its statement that the 2+ GB/s parser lived in upb and was not yet in the C++ version was added. Source: blog.reverberate.org post.
- C2. 1.2: "gRPC not mentioned" was wrong; the README and docs name `grpcio-tools` as a pip dependency. Restated as "no gRPC support described". Source: nanopb README and docs/index.md.
- C3. 1.4: Python 3.10 and protobuf 32.0 come from the 3.6.0 changelog, not from a requirements list. Source: EmbeddedProto README.
- C4. 1.6: the nanosecond figures are the Linux numbers; the gRPC adapter is described precisely (blocking unary `call()`, callback reactors). Sources: hpp-proto benchmarks/ReadMe.md, docs/grpc-adapter.md.
- C5. 1.7: L67 author list completed (vjpai, sheenaqotj, yang-g, zhouyihaiding). Source: L67-cpp-callback-api.md.
- C6. 1.9: protopug creation date 2020-08-09 added. Source: GitHub API.
- C7. 1.10: cross-reference to 2.19 added. Source: item 2.19.
- C8. 1.11: zig-protobuf and the Elixir protox also report passing the conformance suite. Sources: their READMEs.
- C9. 2.1: the 52192 B figure is the "bin size" column; data size 8413 B added. Source: zpp_bits README.
- C10. 2.3: "struct_pb is built on Iguana" is now sourced: `struct_pb_impl.hpp` includes Iguana's protobuf headers and aliases the namespace. Source: yalantinglibs include/ylt/struct_pb/struct_pb_impl.hpp.
- C11. 2.4: the README is in English (only the change notes are in Chinese); conformance counts, runner version, the testee's scope (it does not call the public API), the protoc 3.21.12 byte comparisons and `iguana::to_proto` were added. Sources: Iguana README, iguana_reflect26_changes.md.
- C12. 2.6: relevance reworded; cereal's baseline is the hand-written serialization function, not macros. Source: cereal README.
- C13. 2.7: the bitsery byte figures are data sizes; the usage example (a `serialize` function per type) was read. Source: bitsery README.
- C14. 2.10: `P2996.md` statement on modules and precompiled headers added; `test.json` has one value different from Revzin's example. Sources: clang-p2996 P2996.md; GitHub contents API for test.json.
- C15. 2.13: latest simdjson release is v5.1.0 (2026-10-30T00:42:09Z), not v5.0.8; the 6% figure is stated as a whole-library build figure that names no feature or input size. Sources: GitHub releases API; CppCon 2025 slides, slide 54.
- C16. 2.14: README and post differ on the toolchain (pinned toolchain versus GCC 16.1 without patches); `proto3.hpp` line count added. Sources: struct_proto26 README, proto3.hpp, the May 14, 2026 post.
- C17. 2.17: grpcxx's hand-declared contract route and the absence of interop tests in its tree were added. Sources: grpcxx README; git tree API.
- C18. 2.18 and 6.1: zBuffer author list completed with Yiming Zhang. Source: doi.org CSL metadata.
- C19. 3.3: zig-protobuf covers editions, reports conformance at v36.2, and generates through `protoc-gen-zig`. Sources: zig-protobuf README, build_util.zig.
- C20. 3.4: the generated forms of `ProtocolBufferRpc` and `ProtocolBufferImpl` were read. Source: dproto intermediate.d.
- C21. 4.3: the fix re-measurement is dated after GCC 16.2; the bug does not say where the fix landed. Sources: Bugzilla REST API; gcc.gnu.org/releases.html.
- C22. 4.13: json2cpp is an offline generator that emits constexpr data, not a constexpr parser. Source: json2cpp README.
- C23. 4.14: summary extended with 4.15, 4.16 and the D paper in 6.4. Sources: those items.
- C24. 5.1: P2996R13 section 2.3 credits Lock3 with an earlier P1240-based fork; "neither implementation is complete" refers to EDG and Bloomberg only. Source: P2996R13 section 2.3.
- C25. 5.6: P1967's memory figures are approximate (fixed-interval probes), and compiler versions are "latest built from source". Source: P1967R14 section 3.2.
- C26. 5.7: issue 1445 also carries `needs-revision`. Source: GitHub issues API.
- C27. 5.8: GCC's C++ status page lists P3068R5 with `__cpp_constexpr_exceptions >= 202411L`; the open revision question now carries the standard marker. Source: gcc.gnu.org/projects/cxx-status.html.
- C28. 5.9: the ballot report is now traced to Mateusz Pusz's post of 2026-10-25, which quotes Robert Seacord and gives the vote counts; still not confirmed by a primary source. Sources: train-it.eu post; Hacker News item 50239748.
- C29. 6.1: Accelerometer's full title and full abstract added. Source: Semantic Scholar API.
- C30. 6.1: Lite2 author list completed with Michal Aibin. Source: doi.org CSL metadata.
- C31. 6.2: ACPS author names read from the doi.org metadata (Maltsev, Ul Amin, plus two institution names). Source: doi.org CSL metadata.
- C32. 7.1: simdjson column (b) set to Y; zpp_bits column (d) set to p (own RPC); grpcxx column (e) set to p (hand-written route); rows for Froto, exprotobuf, Elixir protox, the Dathskovsky sketch and Hutorny added. Sources: items 2.13, 2.1, 2.17, 3.7, 3.8, 2.20, 2.19.
- C33. 7.2 and 7.3: closest prior work and the gap bullets rewritten with the near misses found. Sources: items cited in 7.2 and 7.3.

Removals:

- R1. 1.5: "earliest well-known C++ example" removed; protopug (created 2020-08-09) predates protopuf (created 2020-10-21). Source: GitHub API.
- R2. 3.3: "baseline `cpp/protobuf`" removed as unsupported; the README shows `--baseline cpp/protobuf` only as an option to re-render with another baseline. Source: proto-benchmarks README.
- R3. 4.11: the unchecked statement about metaben.ch replaced by the observation that the host name did not resolve from the checker's machine. Source: curl run on 2026-10-30 (exit code 6).

Additions:

- A1. 1.12, FlatBuffers PR 9063 (constexpr embedding of a binary schema). Source: GitHub pulls API.
- A2. 2.19, Hutorny's P2996 protobuf deserializer article. Source: hutorny.in.ua article.
- A3. 2.20, Dathskovsky's `#embed` and reflection blog sketch. Source: cppnext.com post.
- A4. 3.3, the mlugg/zigpb plan for a comptime `.proto` parser. Source: zigpb README.
- A5. 3.7, Elixir exprotobuf and protox. Sources: their READMEs and mix.exs; Elixir guide on macros.
- A6. 3.8, F# Froto.TypeProvider. Sources: Froto README and type-provider doc; Microsoft F# type provider docs.
- A7. 4.15, WG21 P1447R0 constexpr timings. Source: P1447R0 PDF.
- A8. 4.16, ClangOz chapter, DOI 10.1007/978-3-031-51075-5_6 (resolved at doi.org). Sources: doi.org metadata; accepted manuscript.
- A9. 7.4, the checker's queries, screened finds and one unconfirmed lead (thephd.dev post). Sources: as listed in 7.4.

Unverified markers resolved (16 of the gatherer's 19):

- M1. 1.3 protozero `bench`: holds only `data/`. Source: GitHub contents API.
- M2. 1.4 tag 4.0.0b7: `setup.py` at the tag accepts PEP 440 a, b and rc suffixes and mentions beta builds. Source: setup.py at 4.0.0b7.
- M3. 1.5 proto-to-protopuf: opened; an external proto2 parser program. Source: its README and GitHub API.
- M4. 2.3 coro_rpc: struct_pack serialization, compared with gRPC, no compatibility claim. Source: coro_rpc_introduction.md.
- M5. 2.8 reflect-cpp pages: opened. Sources: cpp26_reflection and benchmarks pages.
- M6. 2.13 simdjson P2996 use: `define_aggregate` and `data_member_spec` in `compile_time_json-inl.h`. Source: that file.
- M7. 3.1 tonic version: tonic-v0.14.9, 2026-10-29T17:37:41Z. Source: GitHub releases API.
- M8. 3.2 quick-protobuf `perftest`: runtime ns per iteration, no machine stated. Source: perftest/README.md.
- M9. 4.5 mp-units findings: the issue itself states WSL-only, unconfirmed results; author `mpusz`. Source: GitHub issues API.
- M10. 4.8 GCC Bugzilla 92396: status NEW. Source: Bugzilla REST API.
- M11. 4.9 Templight paper link: the GPCE 2006 abstract introduces Templight. Source: OpenAlex abstract.
- M12. 4.12 Clang interpreter and P2996: upstream Clang has no P2996 per cppreference's table and the mp-units issue. Sources: cppreference C++26 page; mp-units issue 10.
- M13. 6.1 Accelerometer and serialization: the full abstract does not mention serialization. Source: Semantic Scholar API.
- M14. 6.4 Templight link: as M11. Source: OpenAlex abstract.
- M15. 6.4 Duţu et al. content: abstract found. Source: Semantic Scholar API.
- M16. 7.1 simdjson column (b): as M6. Source: compile_time_json-inl.h.

Markers that remain after the check: 5. Three of the gatherer's (1.1 generated tables and `constinit`; 5.9 the failed DIS ballot; 6.1 protobuf in the body of Kanev et al.), the 5.8 revision question converted to the standard marker, and the new lead in 7.4.
