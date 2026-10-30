// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Fuzz target: the reflection decoder on arbitrary bytes, decoded as the golden Everything message
// (every scalar type, enums, nested and repeated fields; the table dispatch) and as Holder (nested
// and repeated messages behind the inline dispatch of small messages). A successful decode must re-encode to
// byte_size bytes, the backward writer must match the forward one (encode_to) byte for byte, and
// the re-encoded bytes must decode and encode again to the same bytes (the
// encoding is canonical; bytes are compared rather than messages because NaN != NaN). GCC is the
// only compiler with P2996 and has no libFuzzer, so this runs under fuzz/standalone_main.cpp.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
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
message Twig { int32 n = 1; string s = 2; }
message Leaf { int32 id = 1; string label = 2; repeated sint64 values = 3; Twig twig = 4; repeated Twig twigs = 5; }
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
using Leaf = tep::message_t<holder_proto, "tep.fuzz.Leaf">;
using Twig = tep::message_t<holder_proto, "tep.fuzz.Twig">;

namespace {

template <class M>
void round_trip(std::span<const std::byte> bytes) {
    const auto m = tep::decode<M>(bytes);
    if (!m) return;
    const std::vector<std::byte> once = tep::encode(*m);
    require(once.size() == tep::byte_size(*m));
    // encode writes back to front; it begins exactly at the buffer's start and matches the forward
    // writer (encode_to) byte for byte.
    std::vector<std::byte> forward(once.size());
    require(tep::encode_to(*m, forward.data()) == forward.data() + forward.size() && forward == once);
    std::vector<std::byte> backward(once.size());
    require(tep::detail::encode_back(*m, backward.data() + backward.size()) == backward.data());
    const auto back = tep::decode<M>(once);
    require(back.has_value());
    require(tep::encode(*back) == once);
}

// parse_from into a message that already holds `prior` agrees with decode: the same success or
// error, and on success the same message (compared by encoding, since NaN != NaN). Then the same
// bytes again into the same object, where every element is reused in place.
template <class M>
void replace_matches_decode(std::span<const std::byte> bytes, const M& prior) {
    const auto fresh = tep::decode<M>(bytes);
    M reused = prior;
    for (int pass = 0; pass < 2; ++pass) {
        tep::decode_error err{};
        const bool ok = tep::parse_from(reused, tep::wire::reader(bytes), err);
        require(ok == fresh.has_value());
        if (ok) {
            require(tep::encode(reused) == tep::encode(*fresh));
        } else {
            require(err == fresh.error());
        }
    }
}

template <class M>
void replace_after(std::span<const std::byte> first, std::span<const std::byte> second) {
    M m{};
    tep::decode_error ignored{};
    (void)tep::parse_from(m, tep::wire::reader(first), ignored);
    const auto fresh = tep::decode<M>(second);
    tep::decode_error err{};
    const bool ok = tep::parse_from(m, tep::wire::reader(second), err);
    require(ok == fresh.has_value());
    if (ok) {
        require(tep::encode(m) == tep::encode(*fresh));
    } else {
        require(err == fresh.error());
    }
}

Holder rich_holder() {
    Holder h{};
    h.one = Leaf{};
    h.one->id = 1;
    h.one->label = "a label longer than the small-string buffer";
    h.one->values = {-1, 2};
    for (std::int32_t i = 0; i < 3; ++i) {
        Leaf l{};
        l.id = i;
        l.label = std::string(40, 'm');
        l.values = {i, -i, 1000};
        if (i != 1) l.twig = Twig{.n = i, .s = std::string(30, 'w')};
        for (std::int32_t k = 0; k <= i; ++k) l.twigs.push_back(Twig{.n = k, .s = std::string(20, 's')});
        h.many.push_back(std::move(l));
    }
    h.packed = {1, -2, 3};
    h.name = "a name longer than the small-string buffer";
    h.raw = std::string(30, 'r');
    h.tags = {std::string(20, 't'), "b", "c"};
    h.u = 7;
    return h;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
    round_trip<Everything>(bytes);
    round_trip<Holder>(bytes);
    static const Holder rich = rich_holder();
    replace_matches_decode<Holder>(bytes, rich);
    replace_matches_decode<Holder>(bytes, Holder{});
    replace_matches_decode<Everything>(bytes, Everything{});
    // The input split in two: whatever the first part leaves in the message (decoded, partial or
    // failed), the second part replaces it as decode would.
    const std::size_t cut = size == 0 ? 0 : data[0] % (size + 1);
    replace_after<Holder>(bytes.first(cut), bytes.subspan(cut));
    replace_after<Everything>(bytes.first(cut), bytes.subspan(cut));
    return 0;
}
