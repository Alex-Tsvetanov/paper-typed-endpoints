// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Arms P and Q: the echo service on protoc-generated classes and libprotobuf, with a route table
// of the same shape as arm R's, in the two configurations protobuf documents for serving many
// messages (Alex, 2026-10-30, design/revision-log.md).
// - P: one Arena per request on a reused initial block, with the request and the response created
//   on it. The Arena constructor that takes only an initial block is the one arena.h calls more
//   efficient than passing ArenaOptions.
// - Q: reused non-arena messages, one request and one response per rpc, as the C++ tutorial's
//   optimization tips advise ("Reuse non-arena message objects when possible. Messages try to keep
//   around any memory they allocate for reuse, even when they are cleared."). ParseFromArray clears
//   the request and copy assignment the response, both keeping their memory. The tips also advise
//   deleting messages once they get too big (measured there with SpaceUsed): Q replaces its pair
//   after a call whose request or response is larger than reuse_limit_bytes, measured by wire size,
//   as arm R's endpoint does (R also drops its pair after a failed call; Q need not, since
//   ParseFromArray clears the request first).
// Each worker has its own arm, so neither the block nor the messages are ever shared.
#include <google/protobuf/arena.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "arm.hpp"
#include "e2e.pb.h"

namespace {

using tep::grpc::status;

constexpr std::string_view person_path = "/bench.e2e.Echo/EchoPerson";
constexpr std::string_view order_path = "/bench.e2e.Echo/EchoOrder";

// Writes m into response behind the gRPC prefix. SerializeWithCachedSizesToArray after
// ByteSizeLong sizes the message once (SerializeToArray would size it again). Always inline, so
// arm P's code is what it was before arm Q shared this function.
template <class M>
[[gnu::always_inline]] inline status write_framed(const M& m, std::vector<std::byte>& response) {
    const std::size_t n = m.ByteSizeLong();
    if (n > 0xFFFF'FFFFu) return status::resource_exhausted;
    response.resize(tep::grpc::prefix_size + n);
    tep::grpc::write_prefix(response.data(), false, static_cast<std::uint32_t>(n));
    auto* const begin = reinterpret_cast<std::uint8_t*>(response.data() + tep::grpc::prefix_size);
    if (m.SerializeWithCachedSizesToArray(begin) != begin + n) return status::internal;
    return status::ok;
}

class protoc_arm final : public e2e::arm {
public:
    status call(std::string_view path, std::span<const std::byte> request, std::vector<std::byte>& response) override {
        response.clear();
        if (path == person_path) return echo<bench::e2e::Person>(request, response);
        if (path == order_path) return echo<bench::e2e::Order>(request, response);
        return status::unimplemented;
    }

private:
    // Holds both messages of the medium request with room to spare; a larger request makes the
    // arena take further blocks from malloc, which it frees with itself.
    static constexpr std::size_t initial_block_size = 64 * 1024;

    // The handler: the response is a copy of the request, in every arm.
    template <class M>
    status echo(std::span<const std::byte> request, std::vector<std::byte>& response) {
        if (request.size() > static_cast<std::size_t>(INT32_MAX)) return status::resource_exhausted;
        google::protobuf::Arena arena(block_.data(), block_.size());
        M* const q = google::protobuf::Arena::Create<M>(&arena);
        if (!q->ParseFromArray(request.data(), static_cast<int>(request.size()))) return status::internal;
        M* const r = google::protobuf::Arena::Create<M>(&arena);
        *r = *q;
        return write_framed(*r, response);
    }

    alignas(std::max_align_t) std::array<char, initial_block_size> block_{};
};

class protoc_reuse_arm final : public e2e::arm {
public:
    status call(std::string_view path, std::span<const std::byte> request, std::vector<std::byte>& response) override {
        response.clear();
        if (path == person_path) return echo(person_, request, response);
        if (path == order_path) return echo(order_, request, response);
        return status::unimplemented;
    }

private:
    static constexpr std::size_t reuse_limit_bytes = 64 * 1024;

    template <class M>
    struct reused {
        std::unique_ptr<M> request = std::make_unique<M>();
        std::unique_ptr<M> response = std::make_unique<M>();
    };

    // The handler: the response is a copy of the request, in every arm.
    template <class M>
    static status echo(reused<M>& m, std::span<const std::byte> request, std::vector<std::byte>& response) {
        const status st = echo_into(*m.request, *m.response, request, response);
        if (request.size() > reuse_limit_bytes || response.size() > reuse_limit_bytes) m = reused<M>{};
        return st;
    }

    template <class M>
    static status echo_into(M& q, M& r, std::span<const std::byte> request, std::vector<std::byte>& response) {
        if (request.size() > static_cast<std::size_t>(INT32_MAX)) return status::resource_exhausted;
        if (!q.ParseFromArray(request.data(), static_cast<int>(request.size()))) return status::internal;
        r = q;
        return write_framed(r, response);
    }

    reused<bench::e2e::Person> person_;
    reused<bench::e2e::Order> order_;
};

}  // namespace

std::unique_ptr<e2e::arm> e2e::make_protoc_arm() { return std::make_unique<protoc_arm>(); }
std::unique_ptr<e2e::arm> e2e::make_protoc_reuse_arm() { return std::make_unique<protoc_reuse_arm>(); }
