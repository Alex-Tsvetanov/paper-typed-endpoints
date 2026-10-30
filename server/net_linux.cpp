// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// net.hpp on Linux: epoll, eventfd, non-blocking sockets, sched affinity.
#include "net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace e2e::net {

namespace {

[[noreturn]] void fail(const char* what) { throw std::system_error(errno, std::generic_category(), what); }

}  // namespace

socket_t listen_tcp(std::uint16_t port, int backlog) {
    const socket_t s = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (s < 0) fail("socket");
    const int one = 1;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0) fail("setsockopt SO_REUSEADDR");
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) fail("bind");
    if (::listen(s, backlog) != 0) fail("listen");
    return s;
}

std::uint16_t local_port(socket_t s) {
    sockaddr_in a{};
    socklen_t n = sizeof a;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&a), &n) != 0) fail("getsockname");
    return ntohs(a.sin_port);
}

socket_t accept_one(socket_t listener) {
    const socket_t c = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (c < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EINTR) return invalid_socket;
        fail("accept4");
    }
    const int one = 1;
    if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) != 0) {
        ::close(c);
        fail("setsockopt TCP_NODELAY");
    }
    return c;
}

void close_socket(socket_t s) noexcept {
    if (s >= 0) ::close(s);
}

std::ptrdiff_t read_some(socket_t s, std::span<std::byte> buffer) noexcept {
    for (;;) {
        const ssize_t n = ::recv(s, buffer.data(), buffer.size(), 0);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        return errno == EAGAIN || errno == EWOULDBLOCK ? would_block : failed;
    }
}

std::ptrdiff_t write_some(socket_t s, std::span<const std::byte> data) noexcept {
    for (;;) {
        const ssize_t n = ::send(s, data.data(), data.size(), MSG_NOSIGNAL);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        return errno == EAGAIN || errno == EWOULDBLOCK ? would_block : failed;
    }
}

void pin_this_thread(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(cpu), &set);
    const int rc = ::pthread_setaffinity_np(::pthread_self(), sizeof set, &set);
    if (rc != 0) throw std::system_error(rc, std::generic_category(), "pthread_setaffinity_np");
}

wakeup::wakeup() : fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    if (fd_ < 0) fail("eventfd");
}

wakeup::~wakeup() { ::close(fd_); }

void wakeup::signal() noexcept {
    const std::uint64_t one = 1;
    [[maybe_unused]] const ssize_t n = ::write(fd_, &one, sizeof one);
}

void wakeup::drain() noexcept {
    std::uint64_t count = 0;
    [[maybe_unused]] const ssize_t n = ::read(fd_, &count, sizeof count);
}

poller::poller() : fd_(::epoll_create1(EPOLL_CLOEXEC)) {
    if (fd_ < 0) fail("epoll_create1");
}

poller::~poller() { ::close(fd_); }

namespace {

epoll_event watch(std::uint64_t token, bool watch_write) {
    epoll_event e{};
    e.events = EPOLLIN | EPOLLRDHUP | (watch_write ? static_cast<std::uint32_t>(EPOLLOUT) : 0u);
    e.data.u64 = token;
    return e;
}

}  // namespace

void poller::add(socket_t s, std::uint64_t token, bool watch_write) {
    epoll_event e = watch(token, watch_write);
    if (::epoll_ctl(fd_, EPOLL_CTL_ADD, s, &e) != 0) fail("epoll_ctl ADD");
}

void poller::modify(socket_t s, std::uint64_t token, bool watch_write) {
    epoll_event e = watch(token, watch_write);
    if (::epoll_ctl(fd_, EPOLL_CTL_MOD, s, &e) != 0) fail("epoll_ctl MOD");
}

void poller::remove(socket_t s) noexcept { ::epoll_ctl(fd_, EPOLL_CTL_DEL, s, nullptr); }

std::size_t poller::wait(std::span<event> out, int timeout_ms) {
    epoll_event raw[256];
    const int max = static_cast<int>(out.size() < 256 ? out.size() : 256);
    int n = ::epoll_wait(fd_, raw, max, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) return 0;
        fail("epoll_wait");
    }
    for (int i = 0; i < n; ++i) {
        const std::uint32_t ev = raw[i].events;
        out[static_cast<std::size_t>(i)] = {raw[i].data.u64, (ev & EPOLLIN) != 0, (ev & EPOLLOUT) != 0,
                                            (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0};
    }
    return static_cast<std::size_t>(n);
}

}  // namespace e2e::net
