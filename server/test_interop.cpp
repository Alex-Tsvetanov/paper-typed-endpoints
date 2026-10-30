// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Interoperability with grpc-go (design note section 6; functional, never timed), both directions.
// The pinned grpc-go client (interop/go/interop_client) calls arms R and P of the server, run in
// this process, and checks every field of every reply; ghz (Alex's decision 4) then calls both
// rpcs and must report every call OK; the server's own counts must agree. Then the grpc-go server
// (interop/go/interop_server) answers requests encoded here by tep: it checks each one field by
// field against the same known messages, and this side decodes each reply with tep and compares
// it with what it sent. The Go programs come from tools/build_interop.sh (TEP_INTEROP_BIN).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "arm.hpp"
#include "check.hpp"
#include "h2_server.hpp"
#include "h2_test_client.hpp"
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
using Item = tep::message_t<e2e_proto, "bench.e2e.Item">;
using Order = tep::message_t<e2e_proto, "bench.e2e.Order">;

namespace fs = std::filesystem;

// The messages of interop/go/expect, built with tep's types.
Person small() { return Person{.name = "Ada Lovelace", .id = 1815, .email = "ada@example.org"}; }
Person edge() {
    return Person{.name = "\xc3\xa9t\xc3\xa9 \xe2\x82\xac", .id = std::numeric_limits<std::int32_t>::min(), .email = {}};
}
Order medium() {
    Order o{};
    o.order_id = "order-0000000042";
    o.created_unix_ms = 1793300000000;
    o.customer = Person{.name = "Grace Hopper", .id = 1906, .email = "grace@example.org"};
    for (int i = 0; i < 12; ++i) {
        Item it{};
        it.id = 100000 + i * 7919;
        it.name = "item-" + std::to_string(i) + "-standard-widget";
        it.price = 9.99 + i * 1.25;
        for (int t = 0; t < 4; ++t) it.tags.push_back(i * 10 + t);
        it.available = i % 3 != 0;
        o.items.push_back(std::move(it));
    }
    o.labels = {"express", "gift-wrap", "fragile", "priority-customer", "eu-warehouse"};
    o.note = std::string(256, 'n');
    o.priority = -3;
    o.checksum = 0x0123'4567'89AB'CDEFull;
    return o;
}
Order edge_order() {
    Order o{};
    o.created_unix_ms = -1;
    o.customer = Person{};  // present and empty
    Item first{};
    first.id = -5;
    first.price = -1.5;
    first.tags = {-1, 0, std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min()};
    o.items.push_back(std::move(first));
    o.items.push_back(Item{});
    o.labels = {""};
    o.priority = std::numeric_limits<std::int32_t>::min();
    o.checksum = std::numeric_limits<std::uint64_t>::max();
    return o;
}

const std::vector<Person> persons{small(), edge(), Person{}};
const std::vector<Order> orders{medium(), edge_order(), Order{}};

// Runs a program and returns its exit status (-1 when it did not exit normally).
int run(const std::string& program, const std::string& args) {
#ifdef _WIN32
    const std::string command = "\"\"" + program + "\" " + args + "\"";  // cmd /c strips the outer quotes
    return std::system(command.c_str());
#else
    const int st = std::system(("'" + program + "' " + args).c_str());
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

std::string quoted(const fs::path& p) { return "\"" + p.generic_string() + "\""; }

std::string interop_program(const char* name) { return std::string(TEP_INTEROP_BIN) + "/" + name; }

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// ghz calls one rpc `calls` times with the request in `data`; its report must say all were OK.
void ghz_calls(std::uint16_t port, const fs::path& dir, const char* rpc, const char* data, int calls) {
    const fs::path report = dir / (std::string(rpc) + ".report.json");
    const int exit_status =
        run(TEP_GHZ, "--insecure --proto " + quoted(TEP_E2E_PROTO) + " --call bench.e2e.Echo." + rpc + " -D " +
                         quoted(dir / data) + " -n " + std::to_string(calls) + " -c 2 -O json -o " + quoted(report) +
                         " 127.0.0.1:" + std::to_string(port));
    const std::string text = read_text(report);
    const std::string ok = "\"statusCodeDistribution\":{\"OK\":" + std::to_string(calls) + "}";
    std::printf("ghz %s: exit status %d, %s\n", rpc, exit_status,
                text.find(ok) != std::string::npos ? "every call OK" : "not every call OK");
    TEP_CHECK(exit_status == 0);
    TEP_CHECK(text.find(ok) != std::string::npos);
    TEP_CHECK(text.find("\"errorDistribution\":{}") != std::string::npos);
    TEP_CHECK(text.find("\"endReason\":\"normal\"") != std::string::npos);
}

constexpr int ghz_per_rpc = 10;

// The grpc-go client, then ghz, against one arm of the server.
void go_client_calls(const char* name, std::unique_ptr<e2e::arm> (*make)(), const fs::path& dir) {
    e2e::server_options options;
    options.port = 0;
    options.make_arm = make;
    e2e::server s(options);
    std::thread serving([&s] { s.run(); });
    const int exit_status = run(interop_program("interop_client"), "-addr 127.0.0.1:" + std::to_string(s.port()));
    std::printf("%s: interop_client exit status %d\n", name, exit_status);
    TEP_CHECK(exit_status == 0);
    ghz_calls(s.port(), dir, "EchoPerson", "person.json", ghz_per_rpc);
    ghz_calls(s.port(), dir, "EchoOrder", "order.json", ghz_per_rpc);
    s.stop();
    serving.join();
    // Every Person and Order echoed, ghz's calls OK, one unknown method.
    const auto counted = s.calls();
    const auto echoes = static_cast<std::uint64_t>(persons.size() + orders.size()) + 2 * std::uint64_t{ghz_per_rpc};
    TEP_CHECK(counted[0] == echoes && counted[12] == 1);
}

template <class M>
std::vector<std::byte> framed_message(const M& m) {
    std::vector<std::byte> out;
    TEP_CHECK(tep::encode_framed(m, out));
    return out;
}

template <class M>
bool echoed(const e2e_test::call& c, const M& sent) {
    if (!c.closed || c.grpc_status != "0") return false;
    const auto frame = tep::grpc::parse_unary(c.response);
    if (!frame || frame->compressed) return false;
    const auto back = tep::decode<M>(frame->payload);
    return back.has_value() && *back == sent;
}

// The port the grpc-go server writes once it listens, or 0 if it exits or a deadline passes first.
std::uint16_t wait_for_port(const fs::path& file, const std::atomic<bool>& exited) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && !exited.load()) {
        if (fs::exists(file)) {
            const int port = std::atoi(read_text(file).c_str());
            if (port > 0 && port < 65536) return static_cast<std::uint16_t>(port);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}

// tep-encoded requests to the grpc-go server.
void tep_client_calls(const fs::path& dir) {
    const fs::path port_file = dir / "go_server.port";
    int exit_status = -1;
    std::atomic<bool> exited{false};
    std::thread go_server([&] {
        exit_status = run(interop_program("interop_server"),
                          "-port-file " + quoted(port_file) + " -persons " + std::to_string(persons.size()) +
                              " -orders " + std::to_string(orders.size()) + " -unknown 1 -timeout 30s");
        exited = true;
    });
    const std::uint16_t port = wait_for_port(port_file, exited);
    TEP_CHECK(port != 0);
    if (port != 0) {
        std::vector<e2e_test::call> sent;
        for (const Person& p : persons) sent.emplace_back("/bench.e2e.Echo/EchoPerson", framed_message(p));
        for (const Order& o : orders) sent.emplace_back("/bench.e2e.Echo/EchoOrder", framed_message(o));
        sent.emplace_back("/bench.e2e.Echo/Missing", framed_message(small()));
        e2e_test::client c(port);
        c.run(sent);
        std::size_t ok = 0;
        for (std::size_t i = 0; i < persons.size(); ++i) ok += echoed(sent[i], persons[i]) ? 1u : 0u;
        for (std::size_t i = 0; i < orders.size(); ++i) ok += echoed(sent[persons.size() + i], orders[i]) ? 1u : 0u;
        std::printf("tep client: %zu of %zu replies decode to the request\n", ok, sent.size() - 1);
        TEP_CHECK(ok == sent.size() - 1);
        TEP_CHECK(sent.back().closed && sent.back().grpc_status == "12" && sent.back().response.empty());
    }
    go_server.join();
    std::printf("interop_server exit status %d\n", exit_status);
    TEP_CHECK(exit_status == 0);
}

}  // namespace

int main() {
#ifdef _WIN32
    WSADATA wsa{};
    TEP_CHECK(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("tep_interop_" + std::to_string(stamp));
    fs::create_directories(dir);
    TEP_CHECK(run(interop_program("interop_client"), "-write-json " + quoted(dir)) == 0);
    go_client_calls("arm R", e2e::make_reflection_arm, dir);
    go_client_calls("arm P", e2e::make_protoc_arm, dir);
    tep_client_calls(dir);
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    return tep_test::finish("test_interop");
}
