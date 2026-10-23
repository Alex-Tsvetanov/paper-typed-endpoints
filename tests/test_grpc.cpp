// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// gRPC unary framing and method paths.

#include <vector>

#include "check.hpp"
#include "tep/grpc.hpp"

using namespace tep::grpc;

namespace {

std::vector<std::byte> frame(int flag, std::uint32_t length, std::size_t payload) {
    std::vector<std::byte> out(prefix_size + payload, std::byte{0x2A});
    write_prefix(out.data(), flag == 1, length);
    if (flag > 1) out[0] = static_cast<std::byte>(flag);
    return out;
}

void test_frames() {
    {
        const auto f = frame(0, 3, 3);
        const auto v = parse_unary(f);
        TEP_CHECK(v && !v->compressed && v->payload.size() == 3 && v->payload[0] == std::byte{0x2A});
        TEP_CHECK(f[1] == std::byte{0} && f[4] == std::byte{3});
    }
    {
        const auto f = frame(0, 0, 0);
        const auto v = parse_unary(f);
        TEP_CHECK(v && v->payload.empty());  // an empty message is valid
    }
    {
        const auto f = frame(1, 2, 2);
        const auto v = parse_unary(f);
        TEP_CHECK(v && v->compressed);
    }
    {
        const auto f = frame(0, 0x01020304u, 0);
        TEP_CHECK(f[1] == std::byte{1} && f[2] == std::byte{2} && f[3] == std::byte{3} && f[4] == std::byte{4});
        TEP_CHECK(read_be32(f.data() + 1) == 0x01020304u);
    }
    // Each check first requires an error, so a wrongly accepted frame cannot pass.
    const auto fails = [](const auto& r, frame_error e) { return !r.has_value() && r.error() == e; };
    std::vector<std::byte> four(4);
    TEP_CHECK(fails(parse_unary(four), frame_error::incomplete));
    TEP_CHECK(fails(parse_unary(frame(0, 10, 3)), frame_error::incomplete));
    TEP_CHECK(fails(parse_unary(frame(0, 2, 3)), frame_error::trailing_bytes));
    TEP_CHECK(fails(parse_unary(frame(0, 100, 100), 99), frame_error::too_large));
    TEP_CHECK(fails(parse_unary(frame(0, 0xFFFF'FFFFu, 0)), frame_error::too_large));
    TEP_CHECK(fails(parse_unary(frame(2, 1, 1)), frame_error::bad_flag));
}

void test_paths() {
    const auto p = split_path("/tep.demo.Greeter/SayHello");
    TEP_CHECK(p && p->service == "tep.demo.Greeter" && p->method == "SayHello");
    TEP_CHECK(!split_path(""));
    TEP_CHECK(!split_path("/"));
    TEP_CHECK(!split_path("//x"));
    TEP_CHECK(!split_path("/a/"));
    TEP_CHECK(!split_path("a/b/c"));
    TEP_CHECK(!split_path("/a/b/c"));
    static_assert(split_path("/s.S/M")->method == "M");
    static_assert(static_cast<int>(status::unimplemented) == 12 && static_cast<int>(status::unauthenticated) == 16);
}

}  // namespace

int main() {
    test_frames();
    test_paths();
    return tep_test::finish("test_grpc");
}
