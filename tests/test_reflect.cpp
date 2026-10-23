// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Message synthesis and the codec. The schema file is read with #embed, the members are checked
// by reflection to prove that synthesis ran, and the encoder's output is compared byte for byte
// with protoc's encoding of the same values (tests/golden/README.md).

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "tep/reflect.hpp"

struct everything_proto {
    static constexpr char data[] = {
#embed "golden/everything.proto"
        , '\0'};
    static constexpr std::string_view text{data, sizeof data - 1};
};

consteval { tep::define_messages<everything_proto>(); }

using Everything = tep::message_t<everything_proto, "tep.test.Everything">;
using Inner = tep::message_t<everything_proto, "tep.test.Inner">;

// Synthesis ran: the members exist with the schema's names, order and mapped types.
namespace synthesis {

constexpr auto ctx = std::meta::access_context::unchecked();

static_assert(std::is_aggregate_v<Everything> && std::is_aggregate_v<Inner>);
static_assert(std::meta::nonstatic_data_members_of(^^Everything, ctx).size() == 30);
static_assert(std::meta::nonstatic_data_members_of(^^Inner, ctx).size() == 2);
static_assert(std::meta::identifier_of(std::meta::nonstatic_data_members_of(^^Everything, ctx)[0]) == "out_of_order");
static_assert(std::is_same_v<decltype(Everything::f_double), double>);
static_assert(std::is_same_v<decltype(Everything::f_float), float>);
static_assert(std::is_same_v<decltype(Everything::f_int64), std::int64_t>);
static_assert(std::is_same_v<decltype(Everything::f_uint64), std::uint64_t>);
static_assert(std::is_same_v<decltype(Everything::f_sint32), std::int32_t>);
static_assert(std::is_same_v<decltype(Everything::f_fixed32), std::uint32_t>);
static_assert(std::is_same_v<decltype(Everything::f_bool), bool>);
static_assert(std::is_same_v<decltype(Everything::f_string), std::string>);
static_assert(std::is_same_v<decltype(Everything::f_bytes), std::string>);
static_assert(std::is_same_v<decltype(Everything::f_enum), std::int32_t>);
static_assert(std::is_same_v<decltype(Everything::f_inner), tep::box<Inner>>);
static_assert(sizeof(tep::box<Inner>) == sizeof(void*), "an absent sub-message costs one pointer");
static_assert(std::is_same_v<decltype(Everything::r_inner), std::vector<Inner>>);
static_assert(std::is_same_v<decltype(Everything::r_sint64), std::vector<std::int64_t>>);
static_assert(std::is_same_v<decltype(Everything::class_), std::int32_t>, "a keyword field gets a trailing underscore");
static_assert(tep::enum_value<everything_proto, "tep.test.Color.NEG"> == -3);
static_assert(tep::enum_value<everything_proto, "tep.test.Color.GREEN"> == 2);

consteval bool names_match_schema() {
    const auto members = std::meta::nonstatic_data_members_of(^^Everything, ctx);
    const auto& m = tep::tables<everything_proto>.messages[tep::message_traits<Everything>::index];
    if (members.size() != m.field_count) return false;
    for (std::size_t k = 0; k < members.size(); ++k)
        if (std::meta::identifier_of(members[k]) != std::string_view(tep::tables<everything_proto>.fields[m.first_field + k].member))
            return false;
    return true;
}
static_assert(names_match_schema());

}  // namespace synthesis

namespace {

// The values of tests/golden/everything.txtpb.
Everything full_value() {
    Everything e{};
    e.out_of_order = 77;
    e.f_double = -1.5;
    e.f_float = 3.25f;
    e.f_int64 = std::numeric_limits<std::int64_t>::min();
    e.f_uint64 = std::numeric_limits<std::uint64_t>::max();
    e.f_int32 = -42;
    e.f_fixed64 = 1234567890123ull;
    e.f_fixed32 = 4294967295u;
    e.f_bool = true;
    e.f_string = "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80";
    e.f_bytes = std::string("\x00\x01\xFF", 3);
    e.f_uint32 = 300;
    e.f_sfixed32 = -2;
    e.f_sfixed64 = -3;
    e.f_sint32 = std::numeric_limits<std::int32_t>::min();
    e.f_sint64 = std::numeric_limits<std::int64_t>::max();
    e.f_enum = tep::enum_value<everything_proto, "tep.test.Color.NEG">;
    e.f_inner = Inner{7, "seven"};
    e.r_int32 = {1, -1, 300};
    e.r_sint64 = {-1, 1, std::numeric_limits<std::int64_t>::min()};
    e.r_fixed32 = {0, 1, 4294967295u};
    e.r_double = {0.5, -0.0};
    e.r_bool = {true, false, true};
    e.r_enum = {1, -3};
    e.r_string = {"a", "", "zz"};
    e.r_bytes = {std::string("\x01", 1), ""};
    e.r_inner = {Inner{1, ""}, Inner{0, "x"}, Inner{}};
    e.r_unpacked = {5, 6};
    e.class_ = 9;
    e.big_number = 1;
    return e;
}

std::string golden(const char* name) { return std::string(TEP_GOLDEN_DIR) + "/" + name; }

void test_matches_protoc() {
    const auto want = tep_test::read_file(golden("everything.bin"));
    const Everything e = full_value();
    const auto got = tep::encode(e);
    TEP_CHECK(tep::byte_size(e) == got.size());
    TEP_CHECK(got == want);
    const auto back = tep::decode<Everything>(want);
    TEP_CHECK(back.has_value());
    if (back) {
        TEP_CHECK(*back == e);
        // == cannot see the sign of zero; the bits can.
        TEP_CHECK(back->r_double.size() == 2 &&
                  std::bit_cast<std::uint64_t>(back->r_double[1]) == std::bit_cast<std::uint64_t>(-0.0));
    }
}

void test_defaults() {
    TEP_CHECK(tep::encode(Everything{}).empty());
    const auto d = tep::decode<Everything>({});
    TEP_CHECK(d && *d == Everything{});
    Everything z{};
    z.f_double = -0.0;  // the bit pattern is not zero, so it is written (as libprotobuf does)
    TEP_CHECK(tep::byte_size(z) == 9);
    Inner neg{-1, ""};
    TEP_CHECK(tep::encode(neg).size() == 11);  // tag + ten-byte sign-extended varint
}

void test_decode_rules() {
    // Unpacked elements of a packed field are accepted.
    auto d = tep::decode<Everything>(tep_test::bytes({0x90, 0x01, 0x05, 0x90, 0x01, 0x06}));
    TEP_CHECK(d && d->r_int32 == std::vector<std::int32_t>{5, 6});
    // A packed run for a [packed = false] field is accepted.
    d = tep::decode<Everything>(tep_test::bytes({0xDA, 0x01, 0x02, 0x05, 0x06}));
    TEP_CHECK(d && d->r_unpacked == std::vector<std::int32_t>{5, 6});
    // Unknown fields are skipped.
    d = tep::decode<Everything>(tep_test::bytes({0xB8, 0x3E, 0x01, 0x28, 0x05}));
    TEP_CHECK(d && d->f_int32 == 5);
    // A known field with the wrong wire type is skipped like an unknown field.
    d = tep::decode<Everything>(tep_test::bytes({0x2A, 0x01, 0x00}));
    TEP_CHECK(d && d->f_int32 == 0);
    // The last singular value wins.
    d = tep::decode<Everything>(tep_test::bytes({0x28, 0x01, 0x28, 0x02}));
    TEP_CHECK(d && d->f_int32 == 2);
    // Two occurrences of a singular message merge.
    d = tep::decode<Everything>(tep_test::bytes({0x8A, 0x01, 0x02, 0x08, 0x01, 0x8A, 0x01, 0x03, 0x12, 0x01, 'x'}));
    TEP_CHECK(d && d->f_inner && d->f_inner->id == 1 && d->f_inner->label == "x");
    // A uint32 keeps the low 32 bits of a wider varint.
    d = tep::decode<Everything>(tep_test::bytes({0x58, 0x80, 0x80, 0x80, 0x80, 0x10}));
    TEP_CHECK(d && d->f_uint32 == 0);
}

void test_decode_errors() {
    const auto bad_utf8 = tep::decode<Everything>(tep_test::bytes({0x4A, 0x02, 0xC0, 0x80}));
    TEP_CHECK(!bad_utf8 && bad_utf8.error() == tep::decode_error::invalid_utf8);
    const auto bytes_ok = tep::decode<Everything>(tep_test::bytes({0x52, 0x02, 0xC0, 0x80}));
    TEP_CHECK(bytes_ok && bytes_ok->f_bytes.size() == 2);  // bytes are not UTF-8 checked
    const auto fails = [](std::initializer_list<int> in, tep::decode_error e) {
        const auto d = tep::decode<Everything>(tep_test::bytes(in));
        return !d.has_value() && d.error() == e;
    };
    TEP_CHECK(fails({0x00}, tep::decode_error::bad_tag));
    TEP_CHECK(fails({0x80, 0x80, 0x80, 0x80, 0x10}, tep::decode_error::bad_tag));        // tag above 32 bits
    TEP_CHECK(fails({0x88, 0x80, 0x80, 0x80, 0x80, 0x00}, tep::decode_error::bad_tag));  // a six-byte tag
    TEP_CHECK(fails({0x0E}, tep::decode_error::bad_wire_type));                          // wire type 6
    TEP_CHECK(fails({0xBF, 0x3E}, tep::decode_error::bad_wire_type));                    // unknown field, type 7
    TEP_CHECK(fails({0x4A, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F}, tep::decode_error::length_overflow));
    TEP_CHECK(fails({0x8A, 0x01, 0x05, 0x08, 0x01}, tep::decode_error::truncated));  // nested message cut short
    TEP_CHECK(fails({0x8A, 0x01, 0x04, 0x12, 0x02, 0xC0, 0x80}, tep::decode_error::invalid_utf8));  // nested string
    TEP_CHECK(fails({0xC2, 0x01, 0x02, 0xC0, 0x80}, tep::decode_error::invalid_utf8));  // repeated string
    // Groups are skipped like unknown fields, as libprotobuf does; a broken group is an error.
    TEP_CHECK(fails({0x0B}, tep::decode_error::truncated));
    TEP_CHECK(fails({0x0B, 0x14}, tep::decode_error::malformed_group));
    const auto known_group = tep::decode<Everything>(tep_test::bytes({0x0B, 0x0C, 0x28, 0x05}));
    TEP_CHECK(known_group && known_group->f_double == 0.0 && known_group->f_int32 == 5);
    const auto unknown_group = tep::decode<Everything>(tep_test::bytes({0xBB, 0x3E, 0x08, 0x01, 0xBC, 0x3E, 0x28, 0x05}));
    TEP_CHECK(unknown_group && unknown_group->f_int32 == 5);
    const auto packed_fixed = tep::decode<Everything>(tep_test::bytes({0xA2, 0x01, 0x03, 0x00, 0x00, 0x00}));
    TEP_CHECK(!packed_fixed && packed_fixed.error() == tep::decode_error::truncated);
    auto want = tep_test::read_file(golden("everything.bin"));
    TEP_CHECK(!want.empty());
    if (!want.empty()) {
        want.pop_back();
        TEP_CHECK(!tep::decode<Everything>(want).has_value());
    }
}

}  // namespace

int main() {
    test_matches_protoc();
    test_defaults();
    test_decode_rules();
    test_decode_errors();
    return tep_test::finish("test_reflect");
}
