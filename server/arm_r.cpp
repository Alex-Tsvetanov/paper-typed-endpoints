// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Arm R: the echo service synthesized from e2e.proto by reflection.
#include <memory>
#include <string_view>

#include "arm.hpp"
#include "tep/reflect.hpp"

namespace {

struct e2e_proto {
    static constexpr char data[] = {
#embed "e2e.proto"
        , '\0'};
    static constexpr std::string_view text{data, sizeof data - 1};
};

}  // namespace

consteval { tep::define_messages<e2e_proto>(); }

namespace {

using Person = tep::message_t<e2e_proto, "bench.e2e.Person">;
using Order = tep::message_t<e2e_proto, "bench.e2e.Order">;

// The handler: the response is a copy of the request, in both arms.
struct echo {
    Person EchoPerson(const Person& q) { return q; }
    Order EchoOrder(const Order& q) { return q; }
};

class reflection_arm final : public e2e::arm {
public:
    tep::grpc::status call(std::string_view path, std::span<const std::byte> request,
                           std::vector<std::byte>& response) override {
        return endpoint_.invoke(path, request, response);
    }

private:
    echo impl_;
    tep::endpoint<e2e_proto, "bench.e2e.Echo", echo> endpoint_{impl_};
};

}  // namespace

std::unique_ptr<e2e::arm> e2e::make_reflection_arm() { return std::make_unique<reflection_arm>(); }
