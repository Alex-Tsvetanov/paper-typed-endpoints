// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// gRPC unary message framing: the 5-byte length prefix (a compression flag and a big-endian
// length), status codes, and the "/package.Service/Method" path. Plain C++23. The HTTP/2
// transport that carries these bytes is a separate layer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tep::grpc {

enum class status : std::uint8_t {
    ok = 0,
    cancelled = 1,
    unknown = 2,
    invalid_argument = 3,
    deadline_exceeded = 4,
    not_found = 5,
    already_exists = 6,
    permission_denied = 7,
    resource_exhausted = 8,
    failed_precondition = 9,
    aborted = 10,
    out_of_range = 11,
    unimplemented = 12,
    internal = 13,
    unavailable = 14,
    data_loss = 15,
    unauthenticated = 16,
};

inline constexpr std::size_t prefix_size = 5;

// Default receive limit of gRPC implementations (4 MiB).
inline constexpr std::uint32_t default_max_message = 4u * 1024u * 1024u;

enum class frame_error : std::uint8_t {
    incomplete,       // fewer than 5 bytes, or fewer payload bytes than the prefix states
    trailing_bytes,   // a unary request carries exactly one message
    too_large,        // over the receive limit
    bad_flag,         // the compression flag is neither 0 nor 1
};

struct frame_view {
    bool compressed = false;
    std::span<const std::byte> payload;
};

[[nodiscard]] constexpr std::uint32_t read_be32(const std::byte* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

constexpr void write_prefix(std::byte* out, bool compressed, std::uint32_t length) noexcept {
    out[0] = static_cast<std::byte>(compressed ? 1 : 0);
    out[1] = static_cast<std::byte>(length >> 24);
    out[2] = static_cast<std::byte>(length >> 16);
    out[3] = static_cast<std::byte>(length >> 8);
    out[4] = static_cast<std::byte>(length);
}

// Parses a whole unary request body: exactly one length-prefixed message.
[[nodiscard]] constexpr std::expected<frame_view, frame_error> parse_unary(
    std::span<const std::byte> body, std::uint32_t max_message = default_max_message) noexcept {
    if (body.size() < prefix_size) return std::unexpected(frame_error::incomplete);
    const auto flag = static_cast<std::uint8_t>(body[0]);
    if (flag > 1) return std::unexpected(frame_error::bad_flag);
    const std::uint32_t length = read_be32(body.data() + 1);
    if (length > max_message) return std::unexpected(frame_error::too_large);
    const std::size_t have = body.size() - prefix_size;
    if (have < length) return std::unexpected(frame_error::incomplete);
    if (have > length) return std::unexpected(frame_error::trailing_bytes);
    return frame_view{flag == 1, body.subspan(prefix_size, length)};
}

struct method_path {
    std::string_view service;  // "package.Service"
    std::string_view method;   // "Method"
};

// Splits "/package.Service/Method". Both parts must be non-empty; no further '/' is allowed.
// A plain loop, not string_view::find: GCC 16.2.1 miscompiles find from an offset when a
// constexpr function gets a string literal (STATE.md, compiler findings).
[[nodiscard]] constexpr std::optional<method_path> split_path(std::string_view path) noexcept {
    if (path.size() < 4 || path.front() != '/') return std::nullopt;
    std::size_t slash = 0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        if (slash != 0) return std::nullopt;  // a third '/'
        slash = i;
    }
    if (slash <= 1 || slash + 1 == path.size()) return std::nullopt;
    return method_path{path.substr(1, slash - 1), path.substr(slash + 1)};
}

}  // namespace tep::grpc
