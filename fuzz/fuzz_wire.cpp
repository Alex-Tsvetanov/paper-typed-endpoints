// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Fuzz target: the portable wire and gRPC parsers on arbitrary bytes. The input is walked as a
// sequence of fields, each skipped as an unknown field is (groups included), and also parsed as a
// unary gRPC body, a UTF-8 string and a method path. Every read must stay inside the input.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

#include "tep/grpc.hpp"
#include "tep/wire.hpp"

namespace {

void require(bool ok) {
    if (!ok) std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace tep;
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
    wire::reader r(bytes);
    while (!r.done()) {
        std::uint64_t tag = 0;
        wire::read_error err{};
        const std::byte* const before = r.p;
        if (!r.varint(tag, err)) break;
        require(r.p > before && r.p <= r.end);
        const std::byte* const value = r.p;
        if (!r.skip(static_cast<wire::wire_type>(tag & 7u), static_cast<std::uint32_t>(tag >> 3), err)) break;
        require(r.p >= value && r.p <= r.end);
    }
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)wire::valid_utf8(text);
    if (const auto frame = grpc::parse_unary(bytes); frame) require(frame->payload.size() + grpc::prefix_size == size);
    if (const auto path = grpc::split_path(text); path) require(path->service.size() + path->method.size() + 2 == size);
    return 0;
}
