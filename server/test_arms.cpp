// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The two arms answer the same requests with byte-identical responses: an echo of a canonically
// encoded request is the request itself, behind a gRPC prefix. Run before any timed request, so a
// difference in what the arms do cannot hide behind the transport.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "arm.hpp"
#include "check.hpp"
#include "e2e.pb.h"
#include "requests.hpp"

namespace {

constexpr const char* person_path = "/bench.e2e.Echo/EchoPerson";
constexpr const char* order_path = "/bench.e2e.Echo/EchoOrder";

std::span<const std::byte> bytes_of(const std::string& s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

std::vector<std::byte> framed(const std::string& message) {
    std::vector<std::byte> out(tep::grpc::prefix_size + message.size());
    tep::grpc::write_prefix(out.data(), false, static_cast<std::uint32_t>(message.size()));
    for (std::size_t i = 0; i < message.size(); ++i)
        out[tep::grpc::prefix_size + i] = static_cast<std::byte>(message[i]);
    return out;
}

void same_echo(e2e::arm& r, e2e::arm& p, const char* path, const std::string& message) {
    std::vector<std::byte> from_r;
    std::vector<std::byte> from_p;
    TEP_CHECK(r.call(path, bytes_of(message), from_r) == tep::grpc::status::ok);
    TEP_CHECK(p.call(path, bytes_of(message), from_p) == tep::grpc::status::ok);
    TEP_CHECK(from_r == from_p);
    TEP_CHECK(from_r == framed(message));
}

void same_error(e2e::arm& r, e2e::arm& p, const char* path, const std::string& message, tep::grpc::status want) {
    std::vector<std::byte> from_r;
    std::vector<std::byte> from_p;
    TEP_CHECK(r.call(path, bytes_of(message), from_r) == want);
    TEP_CHECK(p.call(path, bytes_of(message), from_p) == want);
    TEP_CHECK(from_r.empty() && from_p.empty());
}

}  // namespace

int main() {
    const auto r = e2e::make_reflection_arm();
    const auto p = e2e::make_protoc_arm();

    const std::string small = e2e::small_request();
    const std::string medium = e2e::medium_request();
    std::printf("request sizes: small %zu bytes, medium %zu bytes\n", small.size(), medium.size());
    same_echo(*r, *p, person_path, small);
    same_echo(*r, *p, order_path, medium);
    same_echo(*r, *p, person_path, "");
    same_echo(*r, *p, order_path, "");

    bench::e2e::Person edge;
    edge.set_name("\xc3\xa9t\xc3\xa9 \xe2\x82\xac");  // UTF-8, two- and three-byte sequences
    edge.set_id(-2147483647 - 1);
    same_echo(*r, *p, person_path, edge.SerializeAsString());

    same_error(*r, *p, "/bench.e2e.Echo/Missing", small, tep::grpc::status::unimplemented);
    same_error(*r, *p, person_path, small.substr(0, small.size() - 3), tep::grpc::status::internal);
    return tep_test::finish("test_arms");
}
