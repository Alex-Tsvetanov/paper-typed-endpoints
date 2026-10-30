// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Fuzz target: the reflection decoder on arbitrary bytes, decoded as the golden Everything message
// (every scalar type, enums, nested and repeated fields; the table dispatch) and as Holder (nested
// and repeated messages behind the inline dispatch of small messages). A successful decode must re-encode to
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

struct holder_proto {
    static constexpr std::string_view text = R"(syntax = "proto3";
package tep.fuzz;
message Leaf { int32 id = 1; string label = 2; repeated sint64 values = 3; }
message Holder {
  Leaf one = 1; repeated Leaf many = 2; repeated sint32 packed = 3; string name = 4; bytes raw = 5;
  fixed64 f = 6; double d = 7; repeated string tags = 8; uint32 u = 9;
}
)";
};

void require(bool ok) {
    if (!ok) std::abort();
}

}  // namespace

consteval { tep::define_messages<everything_proto>(); }
consteval { tep::define_messages<holder_proto>(); }
using Everything = tep::message_t<everything_proto, "tep.test.Everything">;
using Holder = tep::message_t<holder_proto, "tep.fuzz.Holder">;

namespace {

template <class M>
void round_trip(std::span<const std::byte> bytes) {
    const auto m = tep::decode<M>(bytes);
    if (!m) return;
    const std::vector<std::byte> once = tep::encode(*m);
    require(once.size() == tep::byte_size(*m));
    const auto back = tep::decode<M>(once);
    require(back.has_value());
    require(tep::encode(*back) == once);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
    round_trip<Everything>(bytes);
    round_trip<Holder>(bytes);
    return 0;
}
