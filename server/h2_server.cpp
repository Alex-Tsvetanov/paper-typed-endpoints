// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
#include "h2_server.hpp"

#include <nghttp2/nghttp2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "net.hpp"
#include "tep/grpc.hpp"

namespace e2e {

namespace {

using tep::grpc::status;

constexpr std::size_t read_chunk = 64 * 1024;
constexpr std::uint32_t stream_window = 1u << 20;
constexpr std::int32_t connection_window = 1 << 24;
constexpr std::size_t max_body = tep::grpc::prefix_size + tep::grpc::default_max_message;
// Event tokens: connections use their address, which is never 1 or 2.
constexpr std::uint64_t listener_token = 1;
constexpr std::uint64_t wakeup_token = 2;

nghttp2_nv header(std::string_view name, std::string_view value) {
    return {reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data())),
            reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())), name.size(), value.size(),
            NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE};
}

constexpr std::array<std::string_view, 17> status_text = {"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7", "8",
                                                          "9",  "10", "11", "12", "13", "14", "15", "16"};

// One request and its response. Streams are pooled per connection, and their buffers keep their
// capacity, so a steady load allocates nothing in the transport.
struct stream {
    std::int32_t id = 0;
    std::string path;
    std::vector<std::byte> body;
    std::vector<std::byte> response;
    std::size_t sent = 0;
    bool rejected = false;   // not a POST of application/grpc
    bool too_large = false;  // a body over the receive limit
};

class worker;

struct connection {
    worker* owner = nullptr;
    net::socket_t fd = net::invalid_socket;
    nghttp2_session* session = nullptr;
    std::vector<std::byte> out;  // produced by nghttp2, not yet written
    std::size_t out_sent = 0;
    bool watching_write = false;
    bool closing = false;
    std::vector<std::unique_ptr<stream>> streams;
    std::vector<stream*> idle;

    connection() = default;
    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;
    ~connection() {
        if (session != nullptr) nghttp2_session_del(session);
        net::close_socket(fd);
    }

    stream* acquire(std::int32_t id) {
        if (idle.empty()) {
            streams.push_back(std::make_unique<stream>());
            idle.push_back(streams.back().get());
        }
        stream* s = idle.back();
        idle.pop_back();
        s->id = id;
        s->path.clear();
        s->body.clear();
        s->response.clear();
        s->sent = 0;
        s->rejected = false;
        s->too_large = false;
        return s;
    }
    void release(stream* s) { idle.push_back(s); }
};

}  // namespace

struct server_state {
    server_options options;
    net::socket_t listener = net::invalid_socket;
    std::atomic<bool> stopping{false};
    nghttp2_session_callbacks* callbacks = nullptr;
    std::vector<std::unique_ptr<worker>> workers;

    explicit server_state(server_options o);
    server_state(const server_state&) = delete;
    server_state& operator=(const server_state&) = delete;
    ~server_state();
};

namespace {

stream* stream_of(nghttp2_session* session, std::int32_t id) {
    return static_cast<stream*>(nghttp2_session_get_stream_user_data(session, id));
}

nghttp2_ssize read_response(nghttp2_session* session, std::int32_t stream_id, std::uint8_t* buf, std::size_t length,
                            std::uint32_t* flags, nghttp2_data_source* source, void*) {
    auto* s = static_cast<stream*>(source->ptr);
    const std::size_t n = std::min(length, s->response.size() - s->sent);
    std::memcpy(buf, s->response.data() + s->sent, n);
    s->sent += n;
    if (s->sent == s->response.size()) {
        // The trailers end the stream: grpc-status comes after the message.
        *flags |= NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
        const nghttp2_nv trailers[] = {header("grpc-status", "0")};
        if (nghttp2_submit_trailer(session, stream_id, trailers, 1) != 0) return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return static_cast<nghttp2_ssize>(n);
}

class worker {
public:
    worker(server_state& server, int cpu) : server_(server), cpu_(cpu), arm_(server.options.make_arm()) {}

    // From the accepting worker's thread. The flag, not the wakeup, is what makes this worker adopt
    // the connection, so a lost wakeup only delays it to the next poll timeout.
    void hand_over(net::socket_t fd) {
        {
            const std::lock_guard lock(mutex_);
            handed_.push_back(fd);
        }
        pending_.store(true, std::memory_order_release);
        wake_.signal();
    }

    void signal() noexcept { wake_.signal(); }

    void run(bool accepts);

    // Written by this worker's thread only; read after it has been joined.
    [[nodiscard]] const server::call_counts& calls() const noexcept { return calls_; }

    // The response to a complete request: the arm's message with grpc-status 0 in trailers, or a
    // trailers-only response with the error's grpc-status.
    int respond(connection& c, stream& s) {
        status st = status::ok;
        if (s.rejected) {
            st = status::internal;
        } else if (s.too_large) {
            st = status::resource_exhausted;
        } else {
            const auto frame = tep::grpc::parse_unary(s.body);
            if (!frame) {
                st = frame.error() == tep::grpc::frame_error::too_large ? status::resource_exhausted : status::internal;
            } else if (frame->compressed) {
                st = status::internal;  // no grpc-encoding is negotiated
            } else {
                // An arm may throw (protobuf allocates), and nothing thrown here may unwind through
                // nghttp2's C callbacks: mapped as the reflection endpoint maps it. The transport's
                // own allocations in the other callbacks are not guarded; running out of memory
                // there stops the server.
                try {
                    st = arm_->call(s.path, frame->payload, s.response);
                } catch (const std::bad_alloc&) {
                    st = status::resource_exhausted;
                } catch (...) {
                    st = status::unknown;
                }
                if (st != status::ok) s.response.clear();
            }
        }
        ++calls_[static_cast<std::size_t>(st)];
        if (st == status::ok) {
            const nghttp2_nv headers[] = {header(":status", "200"), header("content-type", "application/grpc")};
            nghttp2_data_provider2 data{};
            data.source.ptr = &s;
            data.read_callback = read_response;
            return nghttp2_submit_response2(c.session, s.id, headers, 2, &data);
        }
        const nghttp2_nv headers[] = {header(":status", "200"), header("content-type", "application/grpc"),
                                      header("grpc-status", status_text[static_cast<std::size_t>(st)])};
        return nghttp2_submit_response2(c.session, s.id, headers, 3, nullptr);
    }

private:
    void add_connection(net::socket_t fd);
    void accept_all();
    void read(connection& c);
    void flush(connection& c);
    void close_marked();
    void adopt_handed(std::vector<net::socket_t>& adopted);

    server_state& server_;
    int cpu_;
    std::unique_ptr<arm> arm_;
    net::poller poller_;
    net::wakeup wake_;
    std::mutex mutex_;
    std::vector<net::socket_t> handed_;
    std::atomic<bool> pending_{false};
    std::vector<std::unique_ptr<connection>> connections_;
    std::vector<connection*> marked_;
    std::size_t next_ = 0;
    server::call_counts calls_{};
    std::array<std::byte, read_chunk> buffer_{};
};

int on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user) {
    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
    stream* s = static_cast<connection*>(user)->acquire(frame->hd.stream_id);
    return nghttp2_session_set_stream_user_data(session, frame->hd.stream_id, s) == 0 ? 0
                                                                                     : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int on_header(nghttp2_session* session, const nghttp2_frame* frame, const std::uint8_t* name, std::size_t namelen,
              const std::uint8_t* value, std::size_t valuelen, std::uint8_t, void*) {
    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
    stream* s = stream_of(session, frame->hd.stream_id);
    if (s == nullptr) return 0;
    const std::string_view n(reinterpret_cast<const char*>(name), namelen);
    const std::string_view v(reinterpret_cast<const char*>(value), valuelen);
    if (n == ":path") {
        s->path.assign(v);
    } else if (n == ":method") {
        s->rejected = s->rejected || v != "POST";
    } else if (n == "content-type") {
        s->rejected = s->rejected || !v.starts_with("application/grpc");
    }
    return 0;
}

int on_data_chunk(nghttp2_session* session, std::uint8_t, std::int32_t stream_id, const std::uint8_t* data,
                  std::size_t len, void*) {
    stream* s = stream_of(session, stream_id);
    if (s == nullptr || s->too_large) return 0;
    if (s->body.size() + len > max_body) {
        s->too_large = true;
        return 0;
    }
    const std::size_t at = s->body.size();
    s->body.resize(at + len);
    std::memcpy(s->body.data() + at, data, len);
    return 0;
}

int on_frame(nghttp2_session* session, const nghttp2_frame* frame, void* user) {
    if (frame->hd.type != NGHTTP2_DATA && frame->hd.type != NGHTTP2_HEADERS) return 0;
    if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0) return 0;
    stream* s = stream_of(session, frame->hd.stream_id);
    if (s == nullptr) return 0;
    auto* c = static_cast<connection*>(user);
    return c->owner->respond(*c, *s) == 0 ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int on_close(nghttp2_session* session, std::int32_t stream_id, std::uint32_t, void* user) {
    if (stream* s = stream_of(session, stream_id); s != nullptr) static_cast<connection*>(user)->release(s);
    return 0;
}

void worker::add_connection(net::socket_t fd) {
    auto c = std::make_unique<connection>();
    c->owner = this;
    c->fd = fd;
    if (nghttp2_session_server_new(&c->session, server_.callbacks, c.get()) != 0) return;
    const nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, server_.options.max_concurrent_streams},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, stream_window},
    };
    if (nghttp2_submit_settings(c->session, NGHTTP2_FLAG_NONE, settings, 2) != 0) return;
    if (nghttp2_session_set_local_window_size(c->session, NGHTTP2_FLAG_NONE, 0, connection_window) != 0) return;
    poller_.add(fd, reinterpret_cast<std::uint64_t>(c.get()), false);
    connection& ref = *c;
    connections_.push_back(std::move(c));
    flush(ref);
    if (ref.closing) marked_.push_back(&ref);
}

void worker::accept_all() {
    for (;;) {
        const net::socket_t fd = net::accept_one(server_.listener);
        if (fd == net::invalid_socket) return;
        worker& target = *server_.workers[next_++ % server_.workers.size()];
        if (&target == this) {
            add_connection(fd);
        } else {
            target.hand_over(fd);
        }
    }
}

void worker::read(connection& c) {
    for (;;) {
        const std::ptrdiff_t n = net::read_some(c.fd, buffer_);
        if (n == net::would_block) break;
        if (n <= 0) {  // closed by the peer, or failed
            c.closing = true;
            return;
        }
        const nghttp2_ssize used = nghttp2_session_mem_recv2(
            c.session, reinterpret_cast<const std::uint8_t*>(buffer_.data()), static_cast<std::size_t>(n));
        if (used < 0) {
            c.closing = true;
            return;
        }
    }
    flush(c);
}

// Moves everything nghttp2 has to send into c.out and writes as much as the socket takes.
void worker::flush(connection& c) {
    for (;;) {
        for (;;) {
            const std::uint8_t* data = nullptr;
            const nghttp2_ssize produced = nghttp2_session_mem_send2(c.session, &data);
            if (produced < 0) {
                c.closing = true;
                return;
            }
            if (produced == 0) break;
            const std::size_t at = c.out.size();
            c.out.resize(at + static_cast<std::size_t>(produced));
            std::memcpy(c.out.data() + at, data, static_cast<std::size_t>(produced));
        }
        if (c.out_sent == c.out.size()) break;
        const std::ptrdiff_t written = net::write_some(c.fd, std::span<const std::byte>(c.out).subspan(c.out_sent));
        if (written == net::would_block) break;
        if (written < 0) {
            c.closing = true;
            return;
        }
        c.out_sent += static_cast<std::size_t>(written);
        if (c.out_sent == c.out.size()) {
            c.out.clear();
            c.out_sent = 0;
        }
    }
    const bool want_write = c.out_sent < c.out.size();
    if (want_write != c.watching_write) {
        poller_.modify(c.fd, reinterpret_cast<std::uint64_t>(&c), want_write);
        c.watching_write = want_write;
    }
    if (!want_write && nghttp2_session_want_read(c.session) == 0 && nghttp2_session_want_write(c.session) == 0)
        c.closing = true;
}

void worker::close_marked() {
    for (connection* c : marked_) {
        poller_.remove(c->fd);
        std::erase_if(connections_, [c](const std::unique_ptr<connection>& p) { return p.get() == c; });
    }
    marked_.clear();
}

void worker::adopt_handed(std::vector<net::socket_t>& adopted) {
    // A plain load first: the exchange is a locked read-modify-write, needed only when a flag is set.
    if (!pending_.load(std::memory_order_relaxed) || !pending_.exchange(false, std::memory_order_acquire)) return;
    {
        const std::lock_guard lock(mutex_);
        adopted.swap(handed_);
    }
    for (const net::socket_t fd : adopted) add_connection(fd);
    adopted.clear();
}

void worker::run(bool accepts) {
    if (cpu_ >= 0) net::pin_this_thread(cpu_);
    if (accepts) poller_.add(server_.listener, listener_token, false);
    poller_.add(wake_.handle(), wakeup_token, false);
    std::array<net::event, 256> events{};
    std::vector<net::socket_t> adopted;
    while (!server_.stopping.load(std::memory_order_relaxed)) {
        const std::size_t n = poller_.wait(events, 200);
        for (std::size_t i = 0; i < n; ++i) {
            const net::event& e = events[i];
            if (e.token == listener_token) {
                accept_all();
            } else if (e.token == wakeup_token) {
                wake_.drain();
            } else {
                auto* c = reinterpret_cast<connection*>(e.token);
                if (c->closing) continue;
                if (e.readable || e.hangup) read(*c);
                if (!c->closing && e.writable) flush(*c);
                if (c->closing) marked_.push_back(c);
            }
        }
        close_marked();
        adopt_handed(adopted);
    }
    for (const auto& c : connections_) poller_.remove(c->fd);
    connections_.clear();
    const std::lock_guard lock(mutex_);
    for (const net::socket_t fd : handed_) net::close_socket(fd);
    handed_.clear();
}

}  // namespace

server_state::server_state(server_options o) : options(std::move(o)) {
    listener = net::listen_tcp(options.port, 1024);
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_close);
}

server_state::~server_state() {
    workers.clear();
    nghttp2_session_callbacks_del(callbacks);
    net::close_socket(listener);
}

server::server(server_options options) : state_(std::make_unique<server_state>(std::move(options))) {
    if (state_->options.cpus.empty()) {
        state_->workers.push_back(std::make_unique<worker>(*state_, -1));
    } else {
        for (const int cpu : state_->options.cpus) state_->workers.push_back(std::make_unique<worker>(*state_, cpu));
    }
}

server::~server() = default;

std::uint16_t server::port() const { return net::local_port(state_->listener); }

void server::run() {
    std::vector<std::thread> threads;
    threads.reserve(state_->workers.size());
    for (std::size_t i = 0; i < state_->workers.size(); ++i) {
        threads.emplace_back([this, i] {
            try {
                state_->workers[i]->run(i == 0);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "worker %zu: %s\n", i, e.what());
                stop();
            }
        });
    }
    for (std::thread& t : threads) t.join();
}

server::call_counts server::calls() const {
    call_counts sum{};
    for (const auto& w : state_->workers)
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += w->calls()[i];
    return sum;
}

void server::stop() noexcept {
    state_->stopping.store(true, std::memory_order_relaxed);
    for (const auto& w : state_->workers) w->signal();
}

}  // namespace e2e
