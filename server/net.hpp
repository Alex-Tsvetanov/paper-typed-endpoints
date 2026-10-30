// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The few socket, readiness and thread calls the server needs. Linux: epoll and eventfd
// (net_linux.cpp). Windows is to follow behind the same interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace e2e::net {

using socket_t = int;
inline constexpr socket_t invalid_socket = -1;

// A non-blocking IPv4 listener on all addresses. Port 0 picks a free port. Throws std::system_error.
socket_t listen_tcp(std::uint16_t port, int backlog);
std::uint16_t local_port(socket_t s);

// One pending connection, non-blocking with TCP_NODELAY, or invalid_socket when none is pending.
socket_t accept_one(socket_t listener);
void close_socket(socket_t s) noexcept;

// Bytes moved; 0 on an orderly close (read only); would_block; or failed.
inline constexpr std::ptrdiff_t would_block = -1;
inline constexpr std::ptrdiff_t failed = -2;
std::ptrdiff_t read_some(socket_t s, std::span<std::byte> buffer) noexcept;
std::ptrdiff_t write_some(socket_t s, std::span<const std::byte> data) noexcept;

// Pins the calling thread to one CPU. Throws std::system_error.
void pin_this_thread(int cpu);

// Wakes a poller from another thread or from a signal handler (signal() is async-signal-safe).
class wakeup {
public:
    wakeup();
    wakeup(const wakeup&) = delete;
    wakeup& operator=(const wakeup&) = delete;
    ~wakeup();
    [[nodiscard]] socket_t handle() const noexcept { return fd_; }
    void signal() noexcept;
    void drain() noexcept;

private:
    socket_t fd_;
};

struct event {
    std::uint64_t token;
    bool readable;
    bool writable;
    bool hangup;  // error or peer hang-up
};

// Level-triggered readiness: reads are always watched, writes only on request.
class poller {
public:
    poller();
    poller(const poller&) = delete;
    poller& operator=(const poller&) = delete;
    ~poller();
    void add(socket_t s, std::uint64_t token, bool watch_write);
    void modify(socket_t s, std::uint64_t token, bool watch_write);
    void remove(socket_t s) noexcept;
    // Waits at most timeout_ms; returns the number of events written to out.
    std::size_t wait(std::span<event> out, int timeout_ms);

private:
    int fd_;
};

}  // namespace e2e::net
