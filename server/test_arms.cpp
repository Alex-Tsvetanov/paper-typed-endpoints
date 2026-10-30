// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Arms R, P and Q answer the same requests with byte-identical responses: an echo of a canonically
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
    const std::string small = e2e::small_request();
    const std::string medium = e2e::medium_request();
    std::printf("request sizes: small %zu bytes, medium %zu bytes\n", small.size(), medium.size());

    bench::e2e::Person edge;
    edge.set_name("\xc3\xa9t\xc3\xa9 \xe2\x82\xac");  // UTF-8, two- and three-byte sequences
    edge.set_id(-2147483647 - 1);
    const std::string edge_person = edge.SerializeAsString();
    // A smaller Order than the medium one: 2 items, no customer, no note.
    bench::e2e::Order shorter;
    TEP_CHECK(shorter.ParseFromString(medium));
    shorter.mutable_items()->DeleteSubrange(2, shorter.items_size() - 2);
    shorter.clear_customer();
    shorter.clear_note();
    const std::string short_order = shorter.SerializeAsString();
    // Over the 64 KiB reuse limit of arms R and Q, and past arm P's arena block.
    bench::e2e::Person big;
    big.set_name(std::string(70 * 1024, 'L'));
    const std::string large = big.SerializeAsString();

    // Arm R against each protobuf arm: P (an Arena per request) and Q (reused messages). The
    // sequences run twice and shrink without an empty message between, so stale data in the
    // reused messages of R and Q would show.
    for (const auto& p : {e2e::make_protoc_arm(), e2e::make_protoc_reuse_arm()}) {
        for (int pass = 0; pass < 2; ++pass) {
            same_echo(*r, *p, order_path, medium);
            same_echo(*r, *p, person_path, small);
            same_echo(*r, *p, person_path, "");
            same_echo(*r, *p, order_path, "");
            same_echo(*r, *p, person_path, edge_person);
            same_echo(*r, *p, order_path, medium);
            same_echo(*r, *p, person_path, small);
            same_echo(*r, *p, person_path, edge_person);
            same_echo(*r, *p, order_path, medium);
            same_echo(*r, *p, order_path, short_order);
            same_echo(*r, *p, order_path, medium);
            same_echo(*r, *p, person_path, large);
            same_echo(*r, *p, person_path, small);
        }
        same_error(*r, *p, "/bench.e2e.Echo/Missing", small, tep::grpc::status::unimplemented);
        // A partial parse, then messages the partial content could leak into.
        same_error(*r, *p, person_path, small.substr(0, small.size() - 3), tep::grpc::status::internal);
        same_echo(*r, *p, person_path, edge_person);
        same_error(*r, *p, order_path, medium.substr(0, medium.size() - 3), tep::grpc::status::internal);
        same_echo(*r, *p, order_path, short_order);
        same_echo(*r, *p, order_path, "");
    }
    return tep_test::finish("test_arms");
}
