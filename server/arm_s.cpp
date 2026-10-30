// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Arm S: a stub with no codec. For the echo service's two methods it returns the request bytes as
// the response, which is what both real arms return for a canonically encoded request (an echo of
// a message re-encodes to the same bytes), so a client cannot tell the difference. Two uses: the
// transport's test arm where protobuf cannot run (the Windows sanitizer record), and a lower bound
// for the end-to-end benchmark, the cost of the transport alone.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>

#include "arm.hpp"

namespace {

using tep::grpc::status;

class stub_arm final : public e2e::arm {
public:
    status call(std::string_view path, std::span<const std::byte> request, std::vector<std::byte>& response) override {
        response.clear();
        if (path != "/bench.e2e.Echo/EchoPerson" && path != "/bench.e2e.Echo/EchoOrder") return status::unimplemented;
        if (request.size() > 0xFFFF'FFFFu) return status::resource_exhausted;
        response.resize(tep::grpc::prefix_size + request.size());
        tep::grpc::write_prefix(response.data(), false, static_cast<std::uint32_t>(request.size()));
        std::copy(request.begin(), request.end(), response.begin() + tep::grpc::prefix_size);
        return status::ok;
    }
};

}  // namespace

std::unique_ptr<e2e::arm> e2e::make_stub_arm() { return std::make_unique<stub_arm>(); }
