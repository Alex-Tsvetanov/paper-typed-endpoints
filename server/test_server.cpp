// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The transport, end to end over loopback: a blocking nghttp2 client sends gRPC calls to the server
// running in-process on a free port, for each arm, and checks every response body and its
// grpc-status trailer. Concurrent streams on one connection, an unknown method, and a compressed
// message are covered. On Windows the server's listener starts Winsock before the client connects.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <nghttp2/nghttp2.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "arm.hpp"
#include "check.hpp"
#include "h2_server.hpp"
#if TEP_E2E_ARM_P
#include "requests.hpp"
#endif

namespace {

// The blocking client's few socket calls, on both systems.
#ifdef _WIN32
using native_socket = SOCKET;
void close_native(native_socket s) { ::closesocket(s); }
void set_receive_timeout(native_socket s, int seconds) {
    const DWORD ms = static_cast<DWORD>(seconds) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
}
long send_some(native_socket s, const std::uint8_t* data, std::size_t n) {
    return ::send(s, reinterpret_cast<const char*>(data), static_cast<int>(std::min<std::size_t>(n, 1 << 30)), 0);
}
long recv_some(native_socket s, std::uint8_t* data, std::size_t n) {
    return ::recv(s, reinterpret_cast<char*>(data), static_cast<int>(std::min<std::size_t>(n, 1 << 30)), 0);
}
#else
using native_socket = int;
void close_native(native_socket s) { ::close(s); }
void set_receive_timeout(native_socket s, int seconds) {
    timeval limit{};
    limit.tv_sec = seconds;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
}
long send_some(native_socket s, const std::uint8_t* data, std::size_t n) { return ::send(s, data, n, MSG_NOSIGNAL); }
long recv_some(native_socket s, std::uint8_t* data, std::size_t n) { return ::recv(s, data, n, 0); }
#endif

std::vector<std::byte> framed(const std::string& message, bool compressed = false) {
    std::vector<std::byte> out(tep::grpc::prefix_size + message.size());
    tep::grpc::write_prefix(out.data(), compressed, static_cast<std::uint32_t>(message.size()));
    std::memcpy(out.data() + tep::grpc::prefix_size, message.data(), message.size());
    return out;
}

nghttp2_nv header(std::string_view name, std::string_view value) {
    return {reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data())),
            reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())), name.size(), value.size(),
            NGHTTP2_NV_FLAG_NONE};
}

struct call {
    call(std::string p, std::vector<std::byte> b) : path(std::move(p)), body(std::move(b)) {}

    std::string path;
    std::vector<std::byte> body;
    std::size_t sent = 0;
    std::vector<std::byte> response;
    std::string grpc_status;
    bool closed = false;
};

// One connection, blocking I/O with a receive timeout, every call submitted at once.
class client {
public:
    explicit client(std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        set_receive_timeout(fd_, 10);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        TEP_CHECK(::connect(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0);
        nghttp2_session_callbacks* cbs = nullptr;
        nghttp2_session_callbacks_new(&cbs);
        nghttp2_session_callbacks_set_on_header_callback(cbs, on_header);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data);
        nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_close);
        nghttp2_session_client_new(&session_, cbs, this);
        nghttp2_session_callbacks_del(cbs);
        nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, nullptr, 0);
    }
    client(const client&) = delete;
    client& operator=(const client&) = delete;
    ~client() {
        nghttp2_session_del(session_);
        close_native(fd_);
    }

    // Runs every call to completion, or until the server goes quiet for the timeout.
    void run(std::vector<call>& calls) {
        for (call& c : calls) {
            const nghttp2_nv headers[] = {
                header(":method", "POST"),
                header(":scheme", "http"),
                header(":path", c.path),
                header(":authority", "127.0.0.1"),
                header("content-type", "application/grpc"),
                header("te", "trailers"),
            };
            nghttp2_data_provider2 data{};
            data.source.ptr = &c;
            data.read_callback = read_body;
            TEP_CHECK(nghttp2_submit_request2(session_, nullptr, headers, 6, &data, &c) > 0);
        }
        std::vector<std::uint8_t> buffer(64 * 1024);
        for (;;) {
            for (;;) {
                const std::uint8_t* out = nullptr;
                const nghttp2_ssize n = nghttp2_session_mem_send2(session_, &out);
                if (n <= 0) break;
                std::size_t done = 0;
                while (done < static_cast<std::size_t>(n)) {
                    const long w = send_some(fd_, out + done, static_cast<std::size_t>(n) - done);
                    if (w <= 0) return;
                    done += static_cast<std::size_t>(w);
                }
            }
            if (std::all_of(calls.begin(), calls.end(), [](const call& c) { return c.closed; })) return;
            const long r = recv_some(fd_, buffer.data(), buffer.size());
            if (r <= 0) return;
            if (nghttp2_session_mem_recv2(session_, buffer.data(), static_cast<std::size_t>(r)) < 0) return;
        }
    }

private:
    static nghttp2_ssize read_body(nghttp2_session*, std::int32_t, std::uint8_t* buf, std::size_t length,
                                   std::uint32_t* flags, nghttp2_data_source* source, void*) {
        auto* c = static_cast<call*>(source->ptr);
        const std::size_t n = std::min(length, c->body.size() - c->sent);
        std::memcpy(buf, c->body.data() + c->sent, n);
        c->sent += n;
        if (c->sent == c->body.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
        return static_cast<nghttp2_ssize>(n);
    }
    static int on_header(nghttp2_session* s, const nghttp2_frame* frame, const std::uint8_t* name, std::size_t namelen,
                         const std::uint8_t* value, std::size_t valuelen, std::uint8_t, void*) {
        auto* c = static_cast<call*>(nghttp2_session_get_stream_user_data(s, frame->hd.stream_id));
        if (c != nullptr && std::string_view(reinterpret_cast<const char*>(name), namelen) == "grpc-status")
            c->grpc_status.assign(reinterpret_cast<const char*>(value), valuelen);
        return 0;
    }
    static int on_data(nghttp2_session* s, std::uint8_t, std::int32_t id, const std::uint8_t* data, std::size_t len,
                       void*) {
        auto* c = static_cast<call*>(nghttp2_session_get_stream_user_data(s, id));
        if (c != nullptr) {
            const std::size_t at = c->response.size();
            c->response.resize(at + len);
            std::memcpy(c->response.data() + at, data, len);
        }
        return 0;
    }
    static int on_close(nghttp2_session* s, std::int32_t id, std::uint32_t, void*) {
        if (auto* c = static_cast<call*>(nghttp2_session_get_stream_user_data(s, id)); c != nullptr) c->closed = true;
        return 0;
    }

    native_socket fd_{};
    nghttp2_session* session_ = nullptr;
};

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
#endif
    exercise("arm S", e2e::make_stub_arm, {}, 1);
    exercise("arm S, 2 workers", e2e::make_stub_arm, {0, 1}, 4);
    return tep_test::finish("test_server");
}
