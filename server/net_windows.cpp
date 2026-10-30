// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// net.hpp on Windows: WSAPoll, a loopback datagram socket connected to itself as the wakeup,
// non-blocking sockets, thread affinity masks. WSAPoll keeps no state between calls, so the poller
// holds the watched sockets and passes all of them on every wait; a worker watches only its own
// connections, the wakeup and (worker 0) the listener. Builds with MinGW-w64 GCC and with MSVC.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <climits>
#include <system_error>
#include <vector>

#include "net.hpp"

static_assert(sizeof(SOCKET) == sizeof(e2e::net::socket_t));

namespace e2e::net {

namespace {

[[noreturn]] void fail(const char* what) {
    throw std::system_error(::WSAGetLastError(), std::system_category(), what);
}

SOCKET raw(socket_t s) noexcept { return static_cast<SOCKET>(s); }

// Winsock is started once per process, before the first socket, and left running until exit.
void start_winsock() {
    static const int rc = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data);
    }();
    if (rc != 0) throw std::system_error(rc, std::system_category(), "WSAStartup");
}

void set_non_blocking(SOCKET s) {
    u_long on = 1;
    // FIONBIO is an unsigned expansion; ioctlsocket takes it as a long.
    if (::ioctlsocket(s, static_cast<long>(FIONBIO), &on) != 0) fail("ioctlsocket FIONBIO");
}

int clamp_length(std::size_t n) noexcept { return n > INT_MAX ? INT_MAX : static_cast<int>(n); }

}  // namespace

socket_t listen_tcp(std::uint16_t port, int backlog) {
    start_winsock();
    const SOCKET s = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) fail("WSASocket");
    try {
        set_non_blocking(s);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(port);
        if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) fail("bind");
        if (::listen(s, backlog) != 0) fail("listen");
    } catch (...) {
        ::closesocket(s);
        throw;
    }
    return static_cast<socket_t>(s);
}

std::uint16_t local_port(socket_t s) {
    sockaddr_in a{};
    int n = sizeof a;
    if (::getsockname(raw(s), reinterpret_cast<sockaddr*>(&a), &n) != 0) fail("getsockname");
    return ntohs(a.sin_port);
}

socket_t accept_one(socket_t listener) {
    const SOCKET c = ::accept(raw(listener), nullptr, nullptr);
    if (c == INVALID_SOCKET) {
        const int e = ::WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET || e == WSAEINTR) return invalid_socket;
        fail("accept");
    }
    try {
        set_non_blocking(c);
        const BOOL one = TRUE;
        if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one) != 0)
            fail("setsockopt TCP_NODELAY");
    } catch (...) {
        ::closesocket(c);
        throw;
    }
    return static_cast<socket_t>(c);
}

void close_socket(socket_t s) noexcept {
    if (s != invalid_socket) ::closesocket(raw(s));
}

std::ptrdiff_t read_some(socket_t s, std::span<std::byte> buffer) noexcept {
    const int n = ::recv(raw(s), reinterpret_cast<char*>(buffer.data()), clamp_length(buffer.size()), 0);
    if (n >= 0) return n;
    return ::WSAGetLastError() == WSAEWOULDBLOCK ? would_block : failed;
}

std::ptrdiff_t write_some(socket_t s, std::span<const std::byte> data) noexcept {
    const int n = ::send(raw(s), reinterpret_cast<const char*>(data.data()), clamp_length(data.size()), 0);
    if (n >= 0) return n;
    return ::WSAGetLastError() == WSAEWOULDBLOCK ? would_block : failed;
}

void pin_this_thread(int cpu) {
    if (cpu < 0 || cpu >= static_cast<int>(sizeof(DWORD_PTR) * CHAR_BIT))
        throw std::system_error(ERROR_INVALID_PARAMETER, std::system_category(), "pin_this_thread: CPU out of range");
    if (::SetThreadAffinityMask(::GetCurrentThread(), DWORD_PTR{1} << cpu) == 0)
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "SetThreadAffinityMask");
}

// A UDP socket bound to 127.0.0.1 and connected to its own address: signal() sends it one byte,
// which makes it readable in WSAPoll; drain() reads every pending byte.
wakeup::wakeup() {
    start_winsock();
    const SOCKET s = ::WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) fail("WSASocket (wakeup)");
    try {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) fail("bind (wakeup)");
        int n = sizeof a;
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&a), &n) != 0) fail("getsockname (wakeup)");
        if (::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) fail("connect (wakeup)");
        set_non_blocking(s);
    } catch (...) {
        ::closesocket(s);
        throw;
    }
    fd_ = static_cast<socket_t>(s);
}

wakeup::~wakeup() { ::closesocket(raw(fd_)); }

void wakeup::signal() noexcept {
    const char one = 1;
    [[maybe_unused]] const int n = ::send(raw(fd_), &one, 1, 0);
}

void wakeup::drain() noexcept {
    char sink[64];
    while (::recv(raw(fd_), sink, sizeof sink, 0) > 0) {
    }
}

struct poller::watch_set {
    std::vector<WSAPOLLFD> fds;
    std::vector<std::uint64_t> tokens;  // tokens[i] belongs to fds[i]
    std::size_t start = 0;              // where the next wait starts reporting, so none starves

    std::size_t find(socket_t s) const noexcept {
        for (std::size_t i = 0; i < fds.size(); ++i)
            if (fds[i].fd == raw(s)) return i;
        return fds.size();
    }
};

namespace {

SHORT interest(bool watch_write) noexcept { return static_cast<SHORT>(POLLRDNORM | (watch_write ? POLLWRNORM : 0)); }

}  // namespace

poller::poller() : set_(std::make_unique<watch_set>()) { start_winsock(); }

poller::~poller() = default;

void poller::add(socket_t s, std::uint64_t token, bool watch_write) {
    set_->fds.push_back(WSAPOLLFD{raw(s), interest(watch_write), 0});
    set_->tokens.push_back(token);
}

void poller::modify(socket_t s, std::uint64_t token, bool watch_write) {
    const std::size_t i = set_->find(s);
    if (i == set_->fds.size())
        throw std::system_error(WSAENOTSOCK, std::system_category(), "poller::modify: socket not watched");
    set_->fds[i].events = interest(watch_write);
    set_->tokens[i] = token;
}

void poller::remove(socket_t s) noexcept {
    const std::size_t i = set_->find(s);
    if (i == set_->fds.size()) return;
    set_->fds[i] = set_->fds.back();
    set_->tokens[i] = set_->tokens.back();
    set_->fds.pop_back();
    set_->tokens.pop_back();
}

std::size_t poller::wait(std::span<event> out, int timeout_ms) {
    if (set_->fds.empty()) {
        ::Sleep(static_cast<DWORD>(timeout_ms < 0 ? 0 : timeout_ms));
        return 0;
    }
    const int ready = ::WSAPoll(set_->fds.data(), static_cast<ULONG>(set_->fds.size()), timeout_ms);
    if (ready == SOCKET_ERROR) fail("WSAPoll");
    if (ready == 0) return 0;
    // At most out.size() events are reported; the scan starts where the last one stopped, so with
    // more ready sockets than that every socket is reported within a few waits.
    const std::size_t count = set_->fds.size();
    const std::size_t limit = std::min(out.size(), static_cast<std::size_t>(ready));
    std::size_t n = 0;
    std::size_t i = set_->start % count;
    for (std::size_t seen = 0; seen < count && n < limit; ++seen, i = i + 1 == count ? 0 : i + 1) {
        const SHORT ev = set_->fds[i].revents;
        if (ev == 0) continue;
        out[n++] = {set_->tokens[i], (ev & POLLRDNORM) != 0, (ev & POLLWRNORM) != 0,
                    (ev & (POLLERR | POLLHUP | POLLNVAL)) != 0};
    }
    set_->start = i;
    return n;
}

}  // namespace e2e::net
