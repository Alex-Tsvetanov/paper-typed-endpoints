// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Message synthesis and the codec. The schema file is read with #embed, the members are checked
// by reflection to prove that synthesis ran, and the encoder's output is compared byte for byte
// with protoc's encoding of the same values (tests/golden/README.md).

#include <algorithm>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "kept_storage.hpp"
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

// Both decode dispatches: Small (16 fields) compares inline, Dense (17 fields, numbers 1 to 17)
// uses the direct slot table. Everything (30 fields, one number of 536870911) uses the binary search.
struct dispatch_proto {
    static constexpr std::string_view text = R"(syntax = "proto3";
package tep.dispatch;
message Small {
  int32 a1 = 1; string a2 = 2; sint64 a3 = 3; fixed32 a4 = 4; repeated int32 a5 = 5; double a6 = 6;
  bool a7 = 7; bytes a8 = 8; uint64 a9 = 9; sfixed64 a10 = 10; float a11 = 11; int64 a12 = 12;
  repeated string a13 = 13; uint32 a14 = 14; sint32 a15 = 15; fixed64 a16 = 16;
}
message Dense {
  int32 b1 = 1; string b2 = 2; sint64 b3 = 3; fixed32 b4 = 4; repeated int32 b5 = 5; double b6 = 6;
  bool b7 = 7; bytes b8 = 8; uint64 b9 = 9; sfixed64 b10 = 10; float b11 = 11; int64 b12 = 12;
  repeated string b13 = 13; uint32 b14 = 14; sint32 b15 = 15; fixed64 b16 = 16; Small b17 = 17;
}
)";
};

consteval { tep::define_messages<dispatch_proto>(); }

using Small = tep::message_t<dispatch_proto, "tep.dispatch.Small">;
using Dense = tep::message_t<dispatch_proto, "tep.dispatch.Dense">;

// Every kind of member parse_from keeps or clears: a nested message, repeated messages, strings and
// bytes, packed numbers, and singular strings, bytes and numbers.
struct reuse_proto {
    static constexpr std::string_view text = R"(syntax = "proto3";
package tep.reuse;
message Twig { int32 n = 1; string s = 2; }
message Leaf { int32 id = 1; string label = 2; repeated sint64 values = 3; Twig twig = 4; repeated Twig twigs = 5; }
message Holder {
  Leaf one = 1; repeated Leaf many = 2; repeated sint32 packed = 3; string name = 4; bytes raw = 5;
  repeated string tags = 6; uint32 u = 7; repeated bytes blobs = 8;
}
)";
};

consteval { tep::define_messages<reuse_proto>(); }

using Twig = tep::message_t<reuse_proto, "tep.reuse.Twig">;
using Leaf = tep::message_t<reuse_proto, "tep.reuse.Leaf">;
using Holder = tep::message_t<reuse_proto, "tep.reuse.Holder">;

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
static_assert(std::is_same_v<decltype(Everything::r_inner), tep::repeated<Inner>>);
static_assert(std::is_same_v<decltype(Everything::r_string), tep::repeated<std::string>>);
static_assert(std::is_same_v<decltype(Everything::r_sint64), std::vector<std::int64_t>>);
static_assert(std::is_same_v<decltype(Everything::class_), std::int32_t>, "a keyword field gets a trailing underscore");
static_assert(tep::enum_value<everything_proto, "tep.test.Color.NEG"> == -3);
static_assert(tep::enum_value<everything_proto, "tep.test.Color.GREEN"> == 2);

consteval bool names_match_schema() {
    const auto members = std::meta::nonstatic_data_members_of(^^Everything, ctx);
    const auto& m = tep::tables<everything_proto>.messages[tep::message_traits<Everything>::index];
    if (members.size() != m.field_count) return false;
    for (std::size_t k = 0; k < members.size(); ++k)
        if (std::meta::identifier_of(members[k]) !=
            tep::member_name(tep::tables<everything_proto>, tep::tables<everything_proto>.fields[m.first_field + k]))
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

// Each dispatch decodes every field, skips unknown fields and wrong wire types, and rejects
// field number 0.
template <class M>
void check_dispatch(const M& value) {
    auto bytes = tep::encode(value);
    auto back = tep::decode<M>(bytes);
    TEP_CHECK(back && *back == value);
    // An unknown varint field (number 99) and a known field (1) sent as fixed32 are both skipped.
    for (const int b : {0x98, 0x06, 0x07, 0x0D, 0x01, 0x02, 0x03, 0x04}) bytes.push_back(static_cast<std::byte>(b));
    back = tep::decode<M>(bytes);
    TEP_CHECK(back && *back == value);
    const auto zero = tep::decode<M>(tep_test::bytes({0x00}));
    TEP_CHECK(!zero && zero.error() == tep::decode_error::bad_tag);
}

void test_dispatch_paths() {
    constexpr auto ctx = std::meta::access_context::unchecked();
    static_assert(std::meta::nonstatic_data_members_of(^^Small, ctx).size() <= tep::detail::inline_dispatch_fields);
    static_assert(std::meta::nonstatic_data_members_of(^^Dense, ctx).size() > tep::detail::inline_dispatch_fields);
    static_assert(tep::detail::dense_numbers<Dense>, "Dense covers the direct slot table");
    static_assert(!tep::detail::dense_numbers<Everything>, "Everything covers the binary search");
    Small s{};
    s.a1 = -7;
    s.a2 = "two";
    s.a3 = -300;
    s.a4 = 4;
    s.a5 = {5, -5, 500};
    s.a6 = 6.5;
    s.a7 = true;
    s.a8 = std::string{char{0}, static_cast<char>(0xFF)};
    s.a9 = 9'000'000'000ull;
    s.a10 = -10;
    s.a11 = 11.25f;
    s.a12 = -12;
    s.a13 = {"x", "", "thirteen"};
    s.a14 = 14;
    s.a15 = -15;
    s.a16 = 16;
    check_dispatch(s);
    Dense d{};
    d.b1 = 1;
    d.b2 = "dense";
    d.b5 = {1, 2, 3};
    d.b13 = {"a", "b"};
    d.b16 = 0xFFFF'FFFF'FFFF'FFFFull;
    d.b17 = s;
    check_dispatch(d);
}

Leaf leaf(std::int32_t id, std::string label, std::vector<std::int64_t> values) {
    Leaf l{};
    l.id = id;
    l.label = std::move(label);
    l.values = std::move(values);
    return l;
}

Holder rich_holder() {
    Holder h{};
    h.one = leaf(1, "a label longer than the small-string buffer", {-1, 2});
    for (std::int32_t i = 0; i < 3; ++i) {
        Leaf l = leaf(i, std::string(40, static_cast<char>('a' + i)), {i, -i, 1000});
        if (i != 1) l.twig = Twig{.n = i, .s = std::string(30, 'w')};  // a nested message in some elements
        for (std::int32_t t = 0; t <= i; ++t) l.twigs.push_back(Twig{.n = t, .s = std::string(20, 's')});
        h.many.push_back(std::move(l));
    }
    h.packed = {1, -2, 3};
    h.name = "a name longer than the small-string buffer";
    h.raw = std::string(30, 'r');
    h.tags = {std::string(20, 't'), "b", "c"};
    h.u = 7;
    h.blobs = {std::string(25, 'x'), ""};
    return h;
}

// Fewer elements, nested messages where rich_holder has none and none where it has them.
Holder sparse_holder() {
    Holder h{};
    Leaf l = leaf(9, {}, {});
    l.twigs = {Twig{.n = 7, .s = {}}};
    h.many.push_back(std::move(l));
    h.many.push_back(leaf(10, {}, {}));
    h.many[1].twig = Twig{.n = 1, .s = "new"};
    h.tags = {"x"};
    return h;
}

// The capacity of every string and vector a Holder holds, nested ones included, with the storage
// kept for reuse: elements past size() and the storage of absent nested messages.
std::size_t held_capacity(const Leaf& l) {
    std::size_t n = l.label.capacity() + l.values.capacity() * sizeof(std::int64_t);
    if (const Twig* t = l.twig.kept_storage()) n += sizeof(Twig) + t->s.capacity();
    n += l.twigs.capacity() * sizeof(Twig);
    for (const Twig& t : l.twigs.kept_storage()) n += t.s.capacity();
    return n;
}

std::size_t held_capacity(const Holder& h) {
    std::size_t n = h.many.capacity() * sizeof(Leaf) + h.packed.capacity() * sizeof(std::int32_t);
    n += h.name.capacity() + h.raw.capacity() + h.tags.capacity() * sizeof(std::string);
    n += h.blobs.capacity() * sizeof(std::string);
    for (const std::string& t : h.tags.kept_storage()) n += t.capacity();
    for (const std::string& b : h.blobs.kept_storage()) n += b.capacity();
    if (const Leaf* one = h.one.kept_storage()) n += sizeof(Leaf) + held_capacity(*one);
    for (const Leaf& l : h.many.kept_storage()) n += held_capacity(l);
    return n;
}

// parse_from gives what decode gives, whatever the message held before, and reuses its storage.
void test_parse_from() {
    const auto rich = tep::encode(rich_holder());
    const auto sparse = tep::encode(sparse_holder());
    tep::decode_error err{};
    Holder h{};
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(rich), err) && h == rich_holder());
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(sparse), err) && h == sparse_holder());
    TEP_CHECK(!h.one.has_value() && h.many.size() == 2 && h.tags.size() == 1 && h.blobs.empty());
    TEP_CHECK(!h.many[0].twig.has_value() && h.many[0].twigs.size() == 1 && h.many[1].twigs.empty());
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(rich), err) && h == rich_holder());
    // The same message again overwrites in place: no vector or string moves to new storage.
    const Leaf* many = h.many.data();
    const char* label = h.many[0].label.data();
    const char* tag = h.tags[0].data();
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(rich), err) && h == rich_holder());
    TEP_CHECK(h.many.data() == many && h.many[0].label.data() == label && h.tags[0].data() == tag);
    TEP_CHECK(tep::parse_from(h, tep::wire::reader({}), err) && h == Holder{});

    // A nested message that arrives twice merges, as in decode; after a rich message too.
    Holder part{};
    part.one = leaf(5, {}, {1});
    std::vector<std::byte> twice = tep::encode(part);
    part.one = leaf(0, "second", {2});
    const auto second = tep::encode(part);
    twice.insert(twice.end(), second.begin(), second.end());
    h = rich_holder();
    const auto merged = tep::decode<Holder>(twice);
    TEP_CHECK(merged && merged->one->id == 5 && merged->one->values.size() == 2);
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(twice), err) && merged && h == *merged);

    // Elements interleaved with other fields keep their order.
    Holder piece{};
    piece.many = {leaf(1, "p", {})};
    std::vector<std::byte> interleaved = tep::encode(piece);
    for (std::int32_t i = 2; i <= 4; ++i) {
        piece = Holder{};
        piece.tags = {std::to_string(i)};
        const auto tag_bytes = tep::encode(piece);
        piece = Holder{};
        piece.many = {leaf(i, {}, {})};
        const auto leaf_bytes = tep::encode(piece);
        interleaved.insert(interleaved.end(), tag_bytes.begin(), tag_bytes.end());
        interleaved.insert(interleaved.end(), leaf_bytes.begin(), leaf_bytes.end());
    }
    h = rich_holder();
    const auto fresh = tep::decode<Holder>(interleaved);
    TEP_CHECK(fresh && fresh->many.size() == 4 && fresh->tags.size() == 3);
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(interleaved), err) && fresh && h == *fresh);

    // A failed parse leaves a message that the next parse_from replaces.
    TEP_CHECK(!tep::parse_from(h, tep::wire::reader(std::span(rich).first(rich.size() - 1)), err));
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(sparse), err) && h == sparse_holder());

    // The table dispatch (Dense, 17 fields) with a nested message.
    Dense d{};
    d.b2 = "dense";
    d.b13 = {"a", "b"};
    d.b17 = Small{};
    d.b17->a13 = {"x"};
    const auto dense = tep::encode(d);
    Dense other{};
    other.b5 = {9, 9};
    other.b17 = Small{};
    other.b17->a2 = "kept storage";
    TEP_CHECK(tep::parse_from(other, tep::wire::reader(dense), err) && other == d);

    // merge_from still appends.
    Holder m = rich_holder();
    TEP_CHECK(tep::merge_from(m, tep::wire::reader(sparse), err) && m.many.size() == 5 && m.tags.size() == 4);
}

// clear gives the default message and keeps the capacity of strings and vectors within the slack.
void test_clear() {
    Holder h = rich_holder();
    const std::size_t many = h.many.capacity();
    const std::size_t name = h.name.capacity();
    tep::clear(h);
    TEP_CHECK(h == Holder{});
    TEP_CHECK(h.many.capacity() == many && h.name.capacity() == name);
    h.name = std::string(10'000, 'n');
    tep::clear(h);
    tep::clear(h);  // the second clear sees the size 0 left by the first
    TEP_CHECK(h.name.capacity() <= tep::detail::string_slack);
}

// Reused storage stays in proportion to the messages decoded: no run of calls ratchets it up.
void test_reuse_is_bounded() {
    tep::decode_error err{};
    // Each call puts one large string at a new position among empty ones (a reused element
    // would otherwise keep the largest value it ever held).
    Holder h{};
    std::size_t largest_wire = 0;
    for (std::size_t j = 1; j <= 40; ++j) {
        Holder m{};
        m.tags.resize(j);
        m.tags[j - 1] = std::string(2000, 'x');
        const auto bytes = tep::encode(m);
        largest_wire = std::max(largest_wire, bytes.size());
        TEP_CHECK(tep::parse_from(h, tep::wire::reader(bytes), err) && h == m);
        // About 2000 for the large string and 100 per element; kept large ones would be 2000 each.
        TEP_CHECK(held_capacity(h) <= 3 * 2048 + 100 * j + 1024);
    }
    TEP_CHECK(largest_wire < 3000);
    // One call with many elements, a singular string and packed numbers, then small calls.
    Holder big{};
    for (std::int32_t i = 0; i < 5000; ++i) big.many.push_back(leaf(i, {}, {}));
    big.many[0].values.assign(10'000, 5);  // numbers inside a reused element
    big.many[0].twig = Twig{.n = 1, .s = std::string(5000, 'w')};
    big.many[0].twigs.resize(500, Twig{.n = 2, .s = std::string(100, 's')});
    big.packed.assign(10'000, 3);
    big.name = std::string(10'000, 'n');
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(tep::encode(big)), err) && h == big);
    const Holder small = sparse_holder();
    const auto small_bytes = tep::encode(small);
    for (int k = 0; k < 2; ++k) TEP_CHECK(tep::parse_from(h, tep::wire::reader(small_bytes), err) && h == small);
    // What remains is the small message and the reset elements kept for reuse, at most twice the
    // last element count plus 16 per field (about 20 Leafs, 18 Twigs and 18 tag strings here), none
    // of it sized by the big message, which held over 500 KB.
    TEP_CHECK(held_capacity(h) <= 8192);
    TEP_CHECK(tep_test::kept_is_default(h));
}

// Storage kept for reuse holds no data: after shrinking parses, copies, clears, initializer lists
// and resizes, every element past size() and the storage of every absent nested message equals the
// default, at every depth, and the kept storage is what the next message reuses.
void test_kept_storage() {
    tep::decode_error err{};
    const auto rich = tep::encode(rich_holder());
    const auto sparse = tep::encode(sparse_holder());
    Holder h{};
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(rich), err) && tep_test::kept_is_default(h));
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(sparse), err) && h == sparse_holder());
    TEP_CHECK(tep_test::kept_is_default(h));
    TEP_CHECK(h.one.holds_storage() && !h.one.has_value() && h.many.kept_storage().size() == 3);
    // The kept nested message is reused: the same storage holds the next present value.
    const Leaf* one = h.one.kept_storage();
    TEP_CHECK(tep::parse_from(h, tep::wire::reader(rich), err) && h == rich_holder() && &*h.one == one);
    h = sparse_holder();
    TEP_CHECK(h == sparse_holder() && tep_test::kept_is_default(h));
    h = rich_holder();
    TEP_CHECK(h == rich_holder() && tep_test::kept_is_default(h));
    tep::clear(h);
    TEP_CHECK(h == Holder{} && tep_test::kept_is_default(h));
    h = rich_holder();
    h.many[2].twigs = {Twig{.n = 1, .s = "one"}};
    h.tags.resize(1);
    h.many.resize(1);
    h.many.resize(2, leaf(7, "seven", {7}));
    TEP_CHECK(h.many.size() == 2 && h.many[1] == leaf(7, "seven", {7}) && tep_test::kept_is_default(h));
    // A copy reuses kept elements: no element moves to new storage.
    const Leaf* elements = h.many.data();
    const Holder rich_value = rich_holder();
    h = rich_value;
    TEP_CHECK(h.many.data() == elements && h == rich_value);
    // push_back of an element of the same sequence, with and without a kept element to reuse.
    h.many.push_back(h.many[0]);
    tep::clear(h);
    h.many.push_back(leaf(1, "a label longer than the small-string buffer", {1}));
    h.many.push_back(h.many[0]);
    TEP_CHECK(h.many.size() == 2 && h.many[1] == h.many[0] && tep_test::kept_is_default(h));
}

// encode and encode_framed write back to front; encode_to, front to back, is the reference. The
// backward writer must begin exactly at the buffer's start and match the reference byte for byte.
template <class M>
void check_backward(const M& m) {
    const std::size_t n = tep::byte_size(m);
    std::vector<std::byte> forward(n);
    TEP_CHECK(tep::encode_to(m, forward.data()) == forward.data() + n);
    std::vector<std::byte> backward(n);
    TEP_CHECK(tep::detail::encode_back(m, backward.data() + n) == backward.data());
    TEP_CHECK(backward == forward);
    TEP_CHECK(tep::encode(m) == forward);
    std::vector<std::byte> framed;
    TEP_CHECK(tep::encode_framed(m, framed) && framed.size() == tep::grpc::prefix_size + n);
    TEP_CHECK(std::equal(forward.begin(), forward.end(),
                         framed.begin() + static_cast<std::ptrdiff_t>(tep::grpc::prefix_size)));
    // The prefix: not compressed, then the length in four bytes, most significant first.
    TEP_CHECK(framed[0] == std::byte{0});
    for (std::size_t i = 0; i < 4; ++i)
        TEP_CHECK(framed[1 + i] == static_cast<std::byte>((n >> (8 * (3 - i))) & 0xFFu));
}

void test_backward_writer() {
    check_backward(full_value());
    check_backward(Everything{});
    Everything z{};
    z.f_double = -0.0;
    check_backward(z);
    check_backward(Inner{-1, ""});
    check_backward(rich_holder());
    check_backward(sparse_holder());
    check_backward(Holder{});
    // Lengths of two bytes: a nested string, a packed payload and a nested message over 127 bytes,
    // and negative int32 values of ten bytes each.
    Holder h = rich_holder();
    h.one->label = std::string(300, 'L');
    for (std::int32_t i = 0; i < 200; ++i) h.packed.push_back(i * 1000 - 70000);
    h.many[0].twigs.resize(40, Twig{.n = -1, .s = std::string(10, 'q')});
    TEP_CHECK(tep::byte_size(*h.one) > 300 && tep::byte_size(h.many[0]) > 127);
    check_backward(h);
    // A length of three bytes (over 16383), and a present but empty nested message (0a 00).
    h.name = std::string(20'000, 'n');
    check_backward(h);
    Holder present{};
    present.one = Leaf{};
    check_backward(present);
    TEP_CHECK(tep::encode(present) == (std::vector<std::byte>{std::byte{0x0a}, std::byte{0x00}}));
}

}  // namespace

int main() {
    test_matches_protoc();
    test_defaults();
    test_decode_rules();
    test_decode_errors();
    test_dispatch_paths();
    test_parse_from();
    test_clear();
    test_reuse_is_bounded();
    test_kept_storage();
    test_backward_writer();
    return tep_test::finish("test_reflect");
}
