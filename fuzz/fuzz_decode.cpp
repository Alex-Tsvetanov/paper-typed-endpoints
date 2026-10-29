// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Fuzz target: the reflection decoder on arbitrary bytes, decoded as the golden Everything message
// (every scalar type, enums, nested and repeated fields). A successful decode must re-encode to
// byte_size bytes, and the re-encoded bytes must decode and encode again to the same bytes (the
// encoding is canonical; bytes are compared rather than messages because NaN != NaN). GCC is the
// only compiler with P2996 and has no libFuzzer, so this runs under fuzz/standalone_main.cpp.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

#include "tep/reflect.hpp"

namespace {

struct everything_proto {
    static constexpr char data[] = {
#embed "../tests/golden/everything.proto"
        , '\0'};
    static constexpr std::string_view text{data, sizeof data - 1};
};

void require(bool ok) {
    if (!ok) std::abort();
}

}  // namespace

consteval { tep::define_messages<everything_proto>(); }
using Everything = tep::message_t<everything_proto, "tep.test.Everything">;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto m = tep::decode<Everything>(std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size));
    if (!m) return 0;
    const std::vector<std::byte> once = tep::encode(*m);
    require(once.size() == tep::byte_size(*m));
    const auto back = tep::decode<Everything>(once);
    require(back.has_value());
    require(tep::encode(*back) == once);
    return 0;
}
