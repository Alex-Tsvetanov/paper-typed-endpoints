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

// A service of 17 rpcs: every path reaches its own handler, and an unknown one is UNIMPLEMENTED.
struct big_proto {
    static constexpr std::string_view text = R"(syntax = "proto3";
package tep.big;
message Num { int32 v = 1; }
service Big {
  rpc R1(Num) returns (Num);
  rpc R2(Num) returns (Num);
  rpc R3(Num) returns (Num);
  rpc R4(Num) returns (Num);
  rpc R5(Num) returns (Num);
  rpc R6(Num) returns (Num);
  rpc R7(Num) returns (Num);
  rpc R8(Num) returns (Num);
  rpc R9(Num) returns (Num);
  rpc R10(Num) returns (Num);
  rpc R11(Num) returns (Num);
  rpc R12(Num) returns (Num);
  rpc R13(Num) returns (Num);
  rpc R14(Num) returns (Num);
  rpc R15(Num) returns (Num);
  rpc R16(Num) returns (Num);
  rpc R17(Num) returns (Num);
}
)";
};

consteval { tep::define_messages<big_proto>(); }

using Num = tep::message_t<big_proto, "tep.big.Num">;

struct big_impl {
    Num R1(const Num& q) { return Num{q.v + 1}; }
    Num R2(const Num& q) { return Num{q.v + 2}; }
    Num R3(const Num& q) { return Num{q.v + 3}; }
    Num R4(const Num& q) { return Num{q.v + 4}; }
    Num R5(const Num& q) { return Num{q.v + 5}; }
    Num R6(const Num& q) { return Num{q.v + 6}; }
    Num R7(const Num& q) { return Num{q.v + 7}; }
    Num R8(const Num& q) { return Num{q.v + 8}; }
    Num R9(const Num& q) { return Num{q.v + 9}; }
    Num R10(const Num& q) { return Num{q.v + 10}; }
    Num R11(const Num& q) { return Num{q.v + 11}; }
    Num R12(const Num& q) { return Num{q.v + 12}; }
    Num R13(const Num& q) { return Num{q.v + 13}; }
    Num R14(const Num& q) { return Num{q.v + 14}; }
    Num R15(const Num& q) { return Num{q.v + 15}; }
    Num R16(const Num& q) { return Num{q.v + 16}; }
    Num R17(const Num& q) { return Num{q.v + 17}; }
};

using big_endpoint = tep::endpoint<big_proto, "tep.big.Big", big_impl>;
static_assert(big_endpoint::method_count() == 17);

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

void test_many_rpcs() {
    big_impl impl;
    big_endpoint ep{impl};
    const auto request = tep::encode(Num{100});
    std::vector<std::byte> response;
    for (std::int32_t k = 1; k <= 17; ++k) {
        const std::string path = "/tep.big.Big/R" + std::to_string(k);
        TEP_CHECK(ep.invoke(path, request, response) == tep::grpc::status::ok);
        const auto frame = tep::grpc::parse_unary(response);
        TEP_CHECK(frame.has_value());
        if (frame) {
            const auto back = tep::decode<Num>(frame->payload);
            TEP_CHECK(back && back->v == 100 + k);
        }
    }
    TEP_CHECK(ep.invoke("/tep.big.Big/R18", request, response) == tep::grpc::status::unimplemented);
    TEP_CHECK(response.empty());
    TEP_CHECK(ep.invoke("/tep.big.Big/R9", tep_test::bytes({0x08}), response) == tep::grpc::status::internal);
}

}  // namespace

int main() {
    test_unary_call();
    test_status_paths();
    test_many_rpcs();
    return tep_test::finish("test_endpoint");
}
