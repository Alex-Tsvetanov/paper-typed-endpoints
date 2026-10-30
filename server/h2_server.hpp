// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The end-to-end benchmark's transport (design note section 5): cleartext HTTP/2 with prior
// knowledge (libnghttp2), unary gRPC calls served by one arm. One worker thread per server CPU,
// pinned to it, each with its own event loop; worker 0 accepts and hands connections out in turn,
// so every worker serves an equal share of them. The same transport serves both arms.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "arm.hpp"

namespace e2e {

struct server_options {
    std::uint16_t port = 50051;  // 0 picks a free port
    std::vector<int> cpus;       // one worker per CPU, pinned to it; empty: one unpinned worker
    std::uint32_t max_concurrent_streams = 1024;
    std::function<std::unique_ptr<arm>()> make_arm;  // each worker gets its own arm
};

struct server_state;  // h2_server.cpp

class server {
public:
    explicit server(server_options options);  // listens; throws std::system_error
    server(const server&) = delete;
    server& operator=(const server&) = delete;
    ~server();

    [[nodiscard]] std::uint16_t port() const;
    // Serves until stop(); returns after every worker has closed its connections.
    void run();
    // From any thread or from a signal handler.
    void stop() noexcept;

private:
    std::unique_ptr<server_state> state_;
};

}  // namespace e2e
