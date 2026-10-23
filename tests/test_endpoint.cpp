// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Unary endpoints synthesized from a service definition, called in process with protoc-encoded
// requests; the responses are compared with protoc's encoding.

#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.hpp"
#include "tep/reflect.hpp"

struct greeter_proto {
    static constexpr char data[] = {
#embed "golden/greeter.proto"
        , '\0'};
    static constexpr std::string_view text{data, sizeof data - 1};
};

consteval { tep::define_messages<greeter_proto>(); }

using HelloRequest = tep::message_t<greeter_proto, "tep.demo.HelloRequest">;
using HelloReply = tep::message_t<greeter_proto, "tep.demo.HelloReply">;

struct greeter_impl {
    int calls = 0;

    HelloReply SayHello(const HelloRequest& q) {
        ++calls;
        HelloReply r{};
        r.message = "hello " + q.name;
        for (std::int32_t i = 0; i < q.times; ++i) r.echoes.push_back(i);
        return r;
    }

    tep::grpc::status SayNothing(const HelloRequest& q, HelloReply&) {
        if (q.name == "throw") throw std::runtime_error("handler failure");
        if (q.name == "oom") throw std::bad_alloc();
        return q.name.empty() ? tep::grpc::status::invalid_argument : tep::grpc::status::ok;
    }
};

using greeter_endpoint = tep::endpoint<greeter_proto, "tep.demo.Greeter", greeter_impl>;

static_assert(greeter_endpoint::method_count() == 2);
static_assert(greeter_endpoint::path(0) == "/tep.demo.Greeter/SayHello");
static_assert(greeter_endpoint::path(1) == "/tep.demo.Greeter/SayNothing");
static_assert(greeter_endpoint::path(2).empty(), "out of range is an empty path");

namespace {

std::string golden(const char* name) { return std::string(TEP_GOLDEN_DIR) + "/" + name; }

std::vector<std::byte> framed(const std::vector<std::byte>& payload) {
    std::vector<std::byte> out(tep::grpc::prefix_size + payload.size());
    tep::grpc::write_prefix(out.data(), false, static_cast<std::uint32_t>(payload.size()));
    for (std::size_t i = 0; i < payload.size(); ++i) out[tep::grpc::prefix_size + i] = payload[i];
    return out;
}

void test_unary_call() {
    greeter_impl impl;
    greeter_endpoint ep{impl};
    const auto request = tep_test::read_file(golden("hello_request.bin"));
    const auto reply = tep_test::read_file(golden("hello_reply.bin"));
    std::vector<std::byte> response;
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/SayHello", request, response) == tep::grpc::status::ok);
    TEP_CHECK(response == framed(reply));
    TEP_CHECK(ep.invoke_framed("/tep.demo.Greeter/SayHello", framed(request), response) == tep::grpc::status::ok);
    TEP_CHECK(response == framed(reply));
    TEP_CHECK(impl.calls == 2);
}

void test_status_paths() {
    greeter_impl impl;
    greeter_endpoint ep{impl};
    std::vector<std::byte> response{std::byte{1}};
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/Missing", {}, response) == tep::grpc::status::unimplemented);
    TEP_CHECK(response.empty());
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/SayNothing", {}, response) == tep::grpc::status::invalid_argument);
    TEP_CHECK(response.empty());
    const auto request = tep_test::read_file(golden("hello_request.bin"));
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/SayNothing", request, response) == tep::grpc::status::ok);
    TEP_CHECK(response == framed({}));  // an empty reply is a five-byte frame
    // A request that does not decode is INTERNAL, as in gRPC servers.
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/SayHello", tep_test::bytes({0x0A, 0x05, 't'}), response) ==
              tep::grpc::status::internal);
    TEP_CHECK(impl.calls == 0);
    // Exceptions from a handler become a status and never leave invoke.
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/SayNothing", tep_test::bytes({0x0A, 0x05, 't', 'h', 'r', 'o', 'w'}),
                        response) == tep::grpc::status::unknown);
    TEP_CHECK(response.empty());
    TEP_CHECK(ep.invoke("/tep.demo.Greeter/SayNothing", tep_test::bytes({0x0A, 0x03, 'o', 'o', 'm'}), response) ==
              tep::grpc::status::resource_exhausted);
    // Framing errors. A compressed message without a negotiated encoding is INTERNAL.
    auto compressed = framed(request);
    compressed[0] = std::byte{1};
    TEP_CHECK(ep.invoke_framed("/tep.demo.Greeter/SayHello", compressed, response) == tep::grpc::status::internal);
    TEP_CHECK(ep.invoke_framed("/tep.demo.Greeter/SayHello", tep_test::bytes({0, 0xFF, 0xFF, 0xFF, 0xFF}), response) ==
              tep::grpc::status::resource_exhausted);
    TEP_CHECK(ep.invoke_framed("/tep.demo.Greeter/SayHello", tep_test::bytes({0, 0, 0}), response) ==
              tep::grpc::status::internal);
}

}  // namespace

int main() {
    test_unary_call();
    test_status_paths();
    return tep_test::finish("test_endpoint");
}
