// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The transport, end to end over loopback: a blocking nghttp2 client sends gRPC calls to the server
// running in-process on a free port, for each arm, and checks every response body and its
// grpc-status trailer. Concurrent streams on one connection, an unknown method, and a compressed
// message are covered.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <nghttp2/nghttp2.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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
#include "requests.hpp"

namespace {

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
        timeval limit{};
        limit.tv_sec = 10;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
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
        ::close(fd_);
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
                    const ssize_t w = ::send(fd_, out + done, static_cast<std::size_t>(n) - done, MSG_NOSIGNAL);
                    if (w <= 0) return;
                    done += static_cast<std::size_t>(w);
                }
            }
            if (std::all_of(calls.begin(), calls.end(), [](const call& c) { return c.closed; })) return;
            const ssize_t r = ::recv(fd_, buffer.data(), buffer.size(), 0);
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

    int fd_ = -1;
    nghttp2_session* session_ = nullptr;
};

void exercise(const char* name, std::unique_ptr<e2e::arm> (*make)()) {
    e2e::server_options options;
    options.port = 0;
    options.make_arm = make;
    e2e::server s(options);
    std::thread serving([&s] { s.run(); });

    const std::string small = e2e::small_request();
    const std::string medium = e2e::medium_request();
    std::vector<call> calls;
    for (int i = 0; i < 50; ++i) calls.emplace_back("/bench.e2e.Echo/EchoPerson", framed(small));
    for (int i = 0; i < 10; ++i) calls.emplace_back("/bench.e2e.Echo/EchoOrder", framed(medium));
    calls.emplace_back("/bench.e2e.Echo/Missing", framed(small));
    calls.emplace_back("/bench.e2e.Echo/EchoPerson", framed(small, true));
    {
        client c(s.port());
        c.run(calls);
    }
    int ok = 0;
    for (std::size_t i = 0; i < 60; ++i)
        ok += calls[i].closed && calls[i].grpc_status == "0" && calls[i].response == calls[i].body ? 1 : 0;
    std::printf("%s: %d of 60 echoes correct\n", name, ok);
    TEP_CHECK(ok == 60);
    TEP_CHECK(calls[60].closed && calls[60].grpc_status == "12" && calls[60].response.empty());
    TEP_CHECK(calls[61].closed && calls[61].grpc_status == "13" && calls[61].response.empty());

    s.stop();
    serving.join();
}

}  // namespace

int main() {
    exercise("arm R", e2e::make_reflection_arm);
    exercise("arm P", e2e::make_protoc_arm);
    return tep_test::finish("test_server");
}
