// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Arm P: the echo service on protoc-generated classes and libprotobuf, with a route table of the
// same shape as arm R's. Per-request messages without an arena for now; the arena choice is open
// (design question Q2 to Alex, 2026-10-30).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "arm.hpp"
#include "e2e.pb.h"

namespace {

using tep::grpc::status;

class protoc_arm final : public e2e::arm {
public:
    status call(std::string_view path, std::span<const std::byte> request, std::vector<std::byte>& response) override {
        response.clear();
        if (path == "/bench.e2e.Echo/EchoPerson") return echo<bench::e2e::Person>(request, response);
        if (path == "/bench.e2e.Echo/EchoOrder") return echo<bench::e2e::Order>(request, response);
        return status::unimplemented;
    }

private:
    // The handler: the response is a copy of the request, in both arms.
    template <class M>
    static status echo(std::span<const std::byte> request, std::vector<std::byte>& response) {
        if (request.size() > static_cast<std::size_t>(INT32_MAX)) return status::resource_exhausted;
        M q;
        if (!q.ParseFromArray(request.data(), static_cast<int>(request.size()))) return status::internal;
        const M r = q;
        const std::size_t n = r.ByteSizeLong();
        if (n > 0xFFFF'FFFFu) return status::resource_exhausted;
        response.resize(tep::grpc::prefix_size + n);
        tep::grpc::write_prefix(response.data(), false, static_cast<std::uint32_t>(n));
        auto* const begin = reinterpret_cast<std::uint8_t*>(response.data() + tep::grpc::prefix_size);
        if (r.SerializeWithCachedSizesToArray(begin) != begin + n) return status::internal;
        return status::ok;
    }
};

}  // namespace

std::unique_ptr<e2e::arm> e2e::make_protoc_arm() { return std::make_unique<protoc_arm>(); }
