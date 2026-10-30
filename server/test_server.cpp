// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The transport, end to end over loopback: a blocking nghttp2 client sends gRPC calls to the server
// running in-process on a free port, for each arm, and checks every response body and its
// grpc-status trailer. Concurrent streams on one connection, an unknown method, and a compressed
// message are covered. On Windows the server's listener starts Winsock before the client connects.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "arm.hpp"
#include "check.hpp"
#include "h2_server.hpp"
#include "h2_test_client.hpp"
#if TEP_E2E_ARM_P
#include "requests.hpp"
#endif

namespace {

using e2e_test::call;
using e2e_test::client;
using e2e_test::framed;

// The benchmark's request messages; without protobuf (a transport-only build) bytes of the same
// sizes, which only arm S serves.
#if TEP_E2E_ARM_P
std::string small_message() { return e2e::small_request(); }
std::string medium_message() { return e2e::medium_request(); }
#else
std::string small_message() { return std::string(34, 's'); }
std::string medium_message() { return std::string(953, 'm'); }
#endif

std::vector<call> make_calls() {
    const std::string small = small_message();
    const std::string medium = medium_message();
    std::vector<call> calls;
    for (int i = 0; i < 50; ++i) calls.emplace_back("/bench.e2e.Echo/EchoPerson", framed(small));
    for (int i = 0; i < 10; ++i) calls.emplace_back("/bench.e2e.Echo/EchoOrder", framed(medium));
    calls.emplace_back("/bench.e2e.Echo/Missing", framed(small));
    calls.emplace_back("/bench.e2e.Echo/EchoPerson", framed(small, true));
    return calls;
}

void check_calls(const char* name, const std::vector<call>& calls) {
    int ok = 0;
    for (std::size_t i = 0; i < 60; ++i)
        ok += calls[i].closed && calls[i].grpc_status == "0" && calls[i].response == calls[i].body ? 1 : 0;
    std::printf("%s: %d of 60 echoes correct\n", name, ok);
    TEP_CHECK(ok == 60);
    TEP_CHECK(calls[60].closed && calls[60].grpc_status == "12" && calls[60].response.empty());
    TEP_CHECK(calls[61].closed && calls[61].grpc_status == "13" && calls[61].response.empty());
}

// One server, `connections` clients at once, each on its own connection with every call in
// flight. With two or more CPUs, worker 0 accepts and hands connections to the others, so the
// handover, its lock and the cross-thread wakeup run.
void exercise(const char* name, std::unique_ptr<e2e::arm> (*make)(), std::vector<int> cpus, int connections) {
    e2e::server_options options;
    options.port = 0;
    options.cpus = std::move(cpus);
    options.make_arm = make;
    e2e::server s(options);
    std::thread serving([&s] { s.run(); });

    std::vector<std::vector<call>> per_connection;
    for (int i = 0; i < connections; ++i) per_connection.push_back(make_calls());
    const std::uint16_t port = s.port();
    std::vector<std::thread> clients;
    for (auto& calls : per_connection)
        clients.emplace_back([port, &calls] {
            client c(port);
            c.run(calls);
        });
    for (std::thread& t : clients) t.join();
    for (const auto& calls : per_connection) check_calls(name, calls);

    s.stop();
    serving.join();
    // The server's own counts agree: per connection 60 ok, one unimplemented, one internal.
    const auto counted = s.calls();
    const auto n = static_cast<std::uint64_t>(connections);
    std::uint64_t total = 0;
    for (const std::uint64_t c : counted) total += c;
    TEP_CHECK(counted[0] == 60 * n && counted[12] == n && counted[13] == n && total == 62 * n);
}

}  // namespace

int main() {
#if TEP_E2E_ARM_R
    exercise("arm R", e2e::make_reflection_arm, {}, 1);
    exercise("arm R, 2 workers", e2e::make_reflection_arm, {0, 1}, 4);
#endif
#if TEP_E2E_ARM_P
    exercise("arm P", e2e::make_protoc_arm, {}, 1);
    exercise("arm P, 2 workers", e2e::make_protoc_arm, {0, 1}, 4);
    exercise("arm Q", e2e::make_protoc_reuse_arm, {}, 1);
    exercise("arm Q, 2 workers", e2e::make_protoc_reuse_arm, {0, 1}, 4);
#endif
    exercise("arm S", e2e::make_stub_arm, {}, 1);
    exercise("arm S, 2 workers", e2e::make_stub_arm, {0, 1}, 4);
    return tep_test::finish("test_server");
}
