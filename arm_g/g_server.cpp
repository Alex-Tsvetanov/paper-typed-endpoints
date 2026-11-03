// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Arm G (M2, hypotheses-m2.md): the echo service of the end-to-end experiment served by gRPC C++
// itself, as the reference a production C++ user would compare against. It uses the async
// completion-queue API, which gRPC's performance guide recommends over the callback API for truly
// high-QPS workloads, and registers enough outstanding requests for the load's concurrency, as the
// guide asks. Each call slot is reused, as the gRPC project's own benchmark server reuses its
// contexts: a new ServerContext per call, the request and reply messages kept and cleared.
//
// Usage: g_server [--port N] [--threads T] [--slots K]
//   --threads  threads polling the one completion queue (default 1)
//   --slots    outstanding requests registered per method (default 2048; the load has 1,536 streams)
// It prints "g_server: arm G, port N, T thread(s)" when it serves, and on SIGTERM or SIGINT
// "g_server: calls ok N, failed F" (F: calls whose reply could not be finished) and exits 0.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "e2e.grpc.pb.h"

namespace {

std::atomic<std::uint64_t> calls_ok{0};
std::atomic<std::uint64_t> calls_failed{0};

// A slot asks for a new call only while the server runs; the flag and that request share one
// lock, so no request reaches the completion queue after it is shut down.
std::mutex shutdown_mutex;
bool shutting_down = false;

class slot_base {
public:
    virtual ~slot_base() = default;
    // Called with the completion of this slot's last operation.
    virtual void proceed(bool ok) = 0;
};

// One reusable call slot of one method: request, then echo the request as the reply, then request
// again with a fresh ServerContext. Message is the method's request and reply type.
template <class Message, auto Method>
class slot final : public slot_base {
public:
    slot(bench::e2e::Echo::AsyncService& service, grpc::ServerCompletionQueue& cq) : service_(service), cq_(cq) {
        request();
    }

    void proceed(bool ok) override {
        if (state_ == state::requested) {
            if (!ok) return;  // the server is shutting down: this slot is done
            reply_ = request_;
            state_ = state::finishing;
            writer_->Finish(reply_, grpc::Status::OK, this);
            return;
        }
        (ok ? calls_ok : calls_failed).fetch_add(1, std::memory_order_relaxed);
        const std::lock_guard lock(shutdown_mutex);
        if (!shutting_down) request();
    }

private:
    enum class state { requested, finishing };

    void request() {
        context_ = std::make_unique<grpc::ServerContext>();
        writer_ = std::make_unique<grpc::ServerAsyncResponseWriter<Message>>(context_.get());
        request_.Clear();
        reply_.Clear();
        state_ = state::requested;
        (service_.*Method)(context_.get(), &request_, writer_.get(), &cq_, &cq_, this);
    }

    bench::e2e::Echo::AsyncService& service_;
    grpc::ServerCompletionQueue& cq_;
    std::unique_ptr<grpc::ServerContext> context_;
    std::unique_ptr<grpc::ServerAsyncResponseWriter<Message>> writer_;
    Message request_;
    Message reply_;
    state state_ = state::requested;
};

using person_slot = slot<bench::e2e::Person, &bench::e2e::Echo::AsyncService::RequestEchoPerson>;
using order_slot = slot<bench::e2e::Order, &bench::e2e::Echo::AsyncService::RequestEchoOrder>;

void poll(grpc::ServerCompletionQueue& cq) {
    void* tag = nullptr;
    bool ok = false;
    while (cq.Next(&tag, &ok)) static_cast<slot_base*>(tag)->proceed(ok);
}

std::atomic<bool> stop_requested{false};

extern "C" void on_signal(int) { stop_requested.store(true); }

int usage() {
    std::fprintf(stderr, "usage: g_server [--port N] [--threads T] [--slots K]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    int port = 50051, threads = 1, slots = 2048;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (i + 1 >= argc) return usage();
        const int v = std::atoi(argv[++i]);
        if (a == "--port" && v > 0 && v < 65536) port = v;
        else if (a == "--threads" && v >= 1 && v <= 64) threads = v;
        else if (a == "--slots" && v >= 1 && v <= 1'000'000) slots = v;
        else return usage();
    }
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);

    bench::e2e::Echo::AsyncService service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:" + std::to_string(port), grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    if (!server) {
        std::fprintf(stderr, "g_server: cannot listen on port %d\n", port);
        return 1;
    }
    std::vector<std::unique_ptr<slot_base>> all;
    all.reserve(2 * static_cast<std::size_t>(slots));
    for (int k = 0; k < slots; ++k) {
        all.push_back(std::make_unique<person_slot>(service, *cq));
        all.push_back(std::make_unique<order_slot>(service, *cq));
    }
    std::vector<std::thread> pollers;
    for (int t = 0; t < threads; ++t) pollers.emplace_back(poll, std::ref(*cq));
    std::printf("g_server: arm G, port %d, %d thread(s)\n", port, threads);
    std::fflush(stdout);

    while (!stop_requested.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        const std::lock_guard lock(shutdown_mutex);
        shutting_down = true;
    }
    server->Shutdown();
    cq->Shutdown();
    for (auto& t : pollers) t.join();
    std::printf("g_server: calls ok %llu, failed %llu\n", static_cast<unsigned long long>(calls_ok.load()),
                static_cast<unsigned long long>(calls_failed.load()));
    return 0;
}
