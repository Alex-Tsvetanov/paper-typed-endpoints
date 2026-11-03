// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Functional test of arm G (no timing): starts g_server with two polling threads, calls both
// methods with the benchmark's requests, first from one thread and then from four at once, checks
// that every reply equals its request byte for byte, stops the server with SIGTERM, and checks that
// it exited 0 (a sanitizer report makes it exit otherwise) and counted exactly the calls made.
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "e2e.grpc.pb.h"
#include "requests.hpp"

extern char** environ;

namespace {

constexpr int calls_per_method = 500;
constexpr int threads = 4;
std::atomic<int> failures{0};

void fail(const char* what) {
    std::fprintf(stderr, "test_g: %s\n", what);
    failures.fetch_add(1);
}

template <class Message, class Call>
void echo(Call call, const std::string& bytes, int count) {
    Message request;
    if (!request.ParseFromString(bytes)) return fail("the benchmark request does not parse");
    for (int i = 0; i < count; ++i) {
        grpc::ClientContext context;
        Message reply;
        const grpc::Status status = call(&context, request, &reply);
        if (!status.ok()) return fail(status.error_message().c_str());
        if (reply.SerializeAsString() != request.SerializeAsString()) return fail("a reply differs from its request");
    }
}

void both(bench::e2e::Echo::Stub& stub, int count) {
    echo<bench::e2e::Person>([&](auto* c, const auto& q, auto* r) { return stub.EchoPerson(c, q, r); },
                             e2e::small_request(), count);
    echo<bench::e2e::Order>([&](auto* c, const auto& q, auto* r) { return stub.EchoOrder(c, q, r); },
                            e2e::medium_request(), count);
}

}  // namespace

int main() {
    const int port = 40000 + static_cast<int>(getpid() % 20000);
    const std::string port_text = std::to_string(port);
    const std::string log = "test_g-server-" + std::to_string(getpid()) + ".log";
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    std::string program = G_SERVER, a1 = "--port", a3 = "--threads", a4 = "2", a5 = "--slots", a6 = "64";
    char* args[] = {program.data(), a1.data(), const_cast<char*>(port_text.c_str()), a3.data(), a4.data(),
                    a5.data(), a6.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, G_SERVER, &actions, nullptr, args, environ) != 0) {
        std::fprintf(stderr, "test_g: cannot start %s\n", G_SERVER);
        return 1;
    }
    auto channel = grpc::CreateChannel("127.0.0.1:" + port_text, grpc::InsecureChannelCredentials());
    if (!channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(30))) {
        fail("g_server did not accept a connection");
    } else {
        auto stub = bench::e2e::Echo::NewStub(channel);
        both(*stub, calls_per_method);
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) pool.emplace_back([&] { both(*stub, calls_per_method); });
        for (auto& t : pool) t.join();
    }
    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) fail("g_server did not exit with status 0");
    std::ifstream in(log);
    std::string line, last;
    while (std::getline(in, line)) last = line;
    const long long want = 2LL * calls_per_method * (1 + threads);
    if (last != "g_server: calls ok " + std::to_string(want) + ", failed 0") {
        std::fprintf(stderr, "test_g: the server's last line is \"%s\", want %lld calls\n", last.c_str(), want);
        failures.fetch_add(1);
    }
    std::remove(log.c_str());
    std::printf("test_g: %s (%lld calls)\n", failures.load() == 0 ? "ok" : "FAILED", want);
    return failures.load() == 0 ? 0 : 1;
}
