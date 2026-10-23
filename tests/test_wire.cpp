// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Wire primitives: varints, zigzag, fixed-width values, the bounded reader and UTF-8. The last
// test walks a message encoded by protoc with the reader alone, so the portable layer is
// checked against the reference encoder on every platform.

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "check.hpp"
#include "tep/wire.hpp"

using namespace tep::wire;

static_assert(varint_size(0) == 1 && varint_size(127) == 1 && varint_size(128) == 2);
static_assert(varint_size(16383) == 2 && varint_size(16384) == 3);
static_assert(varint_size(std::numeric_limits<std::uint64_t>::max()) == 10);
static_assert(zigzag32(0) == 0 && zigzag32(-1) == 1 && zigzag32(1) == 2 && zigzag32(-2) == 3);
static_assert(zigzag32(std::numeric_limits<std::int32_t>::min()) == std::numeric_limits<std::uint32_t>::max());
static_assert(zigzag64(std::numeric_limits<std::int64_t>::min()) == std::numeric_limits<std::uint64_t>::max());
static_assert(unzigzag32(zigzag32(-123456)) == -123456);
static_assert(unzigzag64(zigzag64(std::numeric_limits<std::int64_t>::max())) == std::numeric_limits<std::int64_t>::max());
static_assert(varint_size(int32_wire(-1)) == 10, "a negative int32 is ten bytes on the wire");
static_assert(make_tag(1, wire_type::len) == 0x0A);

namespace {

void test_varint_round_trip() {
    const std::uint64_t values[] = {0, 1, 127, 128, 300, 16383, 16384, (1ull << 32) - 1, 1ull << 35,
                                    std::numeric_limits<std::uint64_t>::max()};
    for (const std::uint64_t v : values) {
        std::vector<std::byte> buf(max_varint_size);
        writer w{buf.data()};
        w.varint(v);
        const auto n = static_cast<std::size_t>(w.p - buf.data());
        TEP_CHECK(n == varint_size(v));
        reader r(std::span<const std::byte>(buf.data(), n));
        std::uint64_t out = 0;
        read_error err{};
        TEP_CHECK(r.varint(out, err) && out == v && r.done());
    }
    std::vector<std::byte> buf(16);
    writer w{buf.data()};
    w.varint(300);
    TEP_CHECK(buf[0] == std::byte{0xAC} && buf[1] == std::byte{0x02});
}

void test_fixed() {
    std::vector<std::byte> buf(12);
    writer w{buf.data()};
    w.fixed32(0x04030201u);
    w.fixed64(0x0807060504030201ull);
    TEP_CHECK(buf[0] == std::byte{1} && buf[3] == std::byte{4} && buf[4] == std::byte{1} && buf[11] == std::byte{8});
    reader r(buf);
    std::uint32_t a = 0;
    std::uint64_t b = 0;
    read_error err{};
    TEP_CHECK(r.fixed32(a, err) && a == 0x04030201u);
    TEP_CHECK(r.fixed64(b, err) && b == 0x0807060504030201ull && r.done());
}

void test_reader_errors() {
    read_error err{};
    std::uint64_t v = 0;
    {
        const auto in = tep_test::bytes({0x80, 0x80});
        reader r(in);
        TEP_CHECK(!r.varint(v, err) && err == read_error::truncated);
    }
    {
        const auto in = tep_test::bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01});
        reader r(in);
        TEP_CHECK(!r.varint(v, err) && err == read_error::malformed_varint);
    }
    {
        const auto in = tep_test::bytes({0x05, 'a', 'b'});
        reader r(in);
        std::span<const std::byte> s;
        TEP_CHECK(!r.length_delimited(s, err) && err == read_error::truncated);
    }
    {
        const auto in = tep_test::bytes({0x01, 0x02, 0x03});
        reader r(in);
        std::uint32_t f = 0;
        TEP_CHECK(!r.fixed32(f, err) && err == read_error::truncated);
    }
    {
        const auto in = tep_test::bytes({0x96, 0x01, 1, 2, 3, 4, 5, 6, 7, 8, 0x02, 'h', 'i', 9, 9, 9, 9});
        reader r(in);
        TEP_CHECK(r.skip(wire_type::varint, err) && r.skip(wire_type::i64, err));
        TEP_CHECK(r.skip(wire_type::len, err) && r.skip(wire_type::i32, err) && r.done());
        reader g(in);
        TEP_CHECK(!g.skip(wire_type::sgroup, err) && err == read_error::group_not_supported);
        TEP_CHECK(!g.skip(static_cast<wire_type>(7), err) && err == read_error::bad_wire_type);
    }
}

void test_utf8() {
    TEP_CHECK(valid_utf8(""));
    TEP_CHECK(valid_utf8("plain ascii"));
    TEP_CHECK(valid_utf8("h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80"));
    TEP_CHECK(valid_utf8("\xF4\x8F\xBF\xBF"));             // U+10FFFF
    TEP_CHECK(!valid_utf8("\xC0\x80"));                     // overlong NUL
    TEP_CHECK(!valid_utf8("\xE0\x80\xAF"));                 // overlong
    TEP_CHECK(!valid_utf8("\xED\xA0\x80"));                 // surrogate
    TEP_CHECK(!valid_utf8("\xF4\x90\x80\x80"));             // above U+10FFFF
    TEP_CHECK(!valid_utf8("\xE2\x82"));                     // truncated
    TEP_CHECK(!valid_utf8("\x80"));                         // stray continuation
    TEP_CHECK(!valid_utf8("\xFF"));
    static_assert(valid_utf8("\xE2\x82\xAC") && !valid_utf8("\xC0\x80"));
}

// hello_request.bin is protoc's encoding of {name: "tep", times: 3} (tests/golden/README.md).
void test_protoc_golden() {
    const auto in = tep_test::read_file(std::string(TEP_GOLDEN_DIR) + "/hello_request.bin");
    reader r(in);
    read_error err{};
    std::uint64_t tag = 0;
    TEP_CHECK(r.varint(tag, err) && tag == make_tag(1, wire_type::len));
    std::span<const std::byte> name;
    TEP_CHECK(r.length_delimited(name, err) && name.size() == 3 && name[0] == std::byte{'t'});
    TEP_CHECK(r.varint(tag, err) && tag == make_tag(2, wire_type::varint));
    std::uint64_t times = 0;
    TEP_CHECK(r.varint(times, err) && times == 3 && r.done());
}

}  // namespace

int main() {
    test_varint_round_trip();
    test_fixed();
    test_reader_errors();
    test_utf8();
    test_protoc_golden();
    return tep_test::finish("test_wire");
}
