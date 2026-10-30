// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Protobuf wire primitives: tags, varints, zigzag, fixed-width values, a bounds-checked reader,
// unchecked forward and backward writers for buffers sized in advance, and a strict UTF-8 check.
// Plain C++23.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace tep::wire {

enum class wire_type : std::uint8_t { varint = 0, i64 = 1, len = 2, sgroup = 3, egroup = 4, i32 = 5 };

inline constexpr std::size_t max_varint_size = 10;

[[nodiscard]] constexpr std::uint32_t make_tag(std::uint32_t number, wire_type t) noexcept {
    return (number << 3) | static_cast<std::uint32_t>(t);
}

[[nodiscard]] constexpr std::size_t varint_size(std::uint64_t v) noexcept {
    // 1 + floor(bit_width / 7), with 0 taking one byte.
    const auto bits = static_cast<std::size_t>(std::bit_width(v | 1u));
    return (bits + 6) / 7;
}

[[nodiscard]] constexpr std::uint32_t zigzag32(std::int32_t v) noexcept {
    return (static_cast<std::uint32_t>(v) << 1) ^ static_cast<std::uint32_t>(v >> 31);
}
[[nodiscard]] constexpr std::uint64_t zigzag64(std::int64_t v) noexcept {
    return (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63);
}
[[nodiscard]] constexpr std::int32_t unzigzag32(std::uint32_t v) noexcept {
    return static_cast<std::int32_t>((v >> 1) ^ (~(v & 1u) + 1u));
}
[[nodiscard]] constexpr std::int64_t unzigzag64(std::uint64_t v) noexcept {
    return static_cast<std::int64_t>((v >> 1) ^ (~(v & 1u) + 1u));
}

// An int32 or enum value is sign-extended to 64 bits on the wire, so a negative value takes
// ten bytes, as in every protobuf implementation.
[[nodiscard]] constexpr std::uint64_t int32_wire(std::int32_t v) noexcept {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
}

// Writes into a buffer whose size was computed beforehand. The caller guarantees room.
struct writer {
    std::byte* p;

    constexpr void varint(std::uint64_t v) noexcept {
        while (v >= 0x80) {
            *p++ = static_cast<std::byte>(static_cast<std::uint8_t>(v) | 0x80u);
            v >>= 7;
        }
        *p++ = static_cast<std::byte>(v);
    }
    constexpr void fixed32(std::uint32_t v) noexcept {
        for (int i = 0; i < 4; ++i) *p++ = static_cast<std::byte>(v >> (8 * i));
    }
    constexpr void fixed64(std::uint64_t v) noexcept {
        for (int i = 0; i < 8; ++i) *p++ = static_cast<std::byte>(v >> (8 * i));
    }
    void bytes(const void* data, std::size_t n) noexcept {
        if (n != 0) std::memcpy(p, data, n);
        p += n;
    }
};

// Writes backwards into a buffer whose size was computed beforehand: each call moves p down by
// the size of what it writes, and writes there. A message written from its last field to its
// first knows each length before it writes the length's prefix, so no nested size is computed
// twice. The caller guarantees room below p.
struct back_writer {
    std::byte* p;

    constexpr void varint(std::uint64_t v) noexcept {
        p -= varint_size(v);
        std::byte* q = p;
        while (v >= 0x80) {
            *q++ = static_cast<std::byte>(static_cast<std::uint8_t>(v) | 0x80u);
            v >>= 7;
        }
        *q = static_cast<std::byte>(v);
    }
    constexpr void fixed32(std::uint32_t v) noexcept {
        p -= 4;
        for (int i = 0; i < 4; ++i) p[i] = static_cast<std::byte>(v >> (8 * i));
    }
    constexpr void fixed64(std::uint64_t v) noexcept {
        p -= 8;
        for (int i = 0; i < 8; ++i) p[i] = static_cast<std::byte>(v >> (8 * i));
    }
    void bytes(const void* data, std::size_t n) noexcept {
        p -= n;
        if (n != 0) std::memcpy(p, data, n);
    }
};

enum class read_error : std::uint8_t {
    truncated,
    malformed_varint,
    length_overflow,
    malformed_group,  // an end-group without its start, or with another field number
    too_deep,         // groups nested deeper than max_group_depth
    bad_wire_type,
};

inline constexpr int max_group_depth = 64;

// A bounds-checked cursor. Every read checks the remaining length first.
struct reader {
    const std::byte* p;
    const std::byte* end;

    constexpr explicit reader(std::span<const std::byte> s) noexcept : p(s.data()), end(s.data() + s.size()) {}

    [[nodiscard]] constexpr bool done() const noexcept { return p == end; }
    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return static_cast<std::size_t>(end - p); }

    // At most ten bytes. Bits above 64 in the tenth byte are dropped, as libprotobuf does.
    [[nodiscard]] constexpr bool varint(std::uint64_t& out, read_error& err) noexcept {
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < max_varint_size; ++i) {
            if (p == end) {
                err = read_error::truncated;
                return false;
            }
            const auto b = static_cast<std::uint8_t>(*p++);
            v |= static_cast<std::uint64_t>(b & 0x7Fu) << (7 * i);
            if ((b & 0x80u) == 0) {
                out = v;
                return true;
            }
        }
        err = read_error::malformed_varint;
        return false;
    }
    [[nodiscard]] constexpr bool fixed32(std::uint32_t& out, read_error& err) noexcept {
        if (remaining() < 4) {
            err = read_error::truncated;
            return false;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(*p++)) << (8 * i);
        out = v;
        return true;
    }
    [[nodiscard]] constexpr bool fixed64(std::uint64_t& out, read_error& err) noexcept {
        if (remaining() < 8) {
            err = read_error::truncated;
            return false;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(*p++)) << (8 * i);
        out = v;
        return true;
    }
    // A varint length followed by that many bytes, which must all be present.
    [[nodiscard]] constexpr bool length_delimited(std::span<const std::byte>& out, read_error& err) noexcept {
        std::uint64_t n = 0;
        if (!varint(n, err)) return false;
        if (n > remaining()) {
            err = n > 0x7FFF'FFFFu ? read_error::length_overflow : read_error::truncated;
            return false;
        }
        out = std::span<const std::byte>(p, static_cast<std::size_t>(n));
        p += n;
        return true;
    }
    // Skips one field value of the given wire type and field number. A group (deprecated, but
    // valid input for proto3 parsers, which skip it as an unknown field) is skipped up to its
    // matching end-group tag, nested at most max_group_depth deep.
    [[nodiscard]] constexpr bool skip(wire_type t, std::uint32_t number, read_error& err, int depth = 0) noexcept {
        switch (t) {
            case wire_type::varint: {
                std::uint64_t ignored = 0;
                return varint(ignored, err);
            }
            case wire_type::i64:
                if (remaining() < 8) {
                    err = read_error::truncated;
                    return false;
                }
                p += 8;
                return true;
            case wire_type::len: {
                std::span<const std::byte> ignored;
                return length_delimited(ignored, err);
            }
            case wire_type::i32:
                if (remaining() < 4) {
                    err = read_error::truncated;
                    return false;
                }
                p += 4;
                return true;
            case wire_type::sgroup: {
                if (depth >= max_group_depth) {
                    err = read_error::too_deep;
                    return false;
                }
                for (;;) {
                    std::uint64_t tag = 0;
                    if (!varint(tag, err)) return false;
                    if (tag > 0xFFFF'FFFFu || (tag >> 3) == 0) {
                        err = read_error::malformed_group;
                        return false;
                    }
                    const auto n = static_cast<std::uint32_t>(tag >> 3);
                    const auto w = static_cast<wire_type>(tag & 7u);
                    if (w == wire_type::egroup) {
                        if (n == number) return true;
                        err = read_error::malformed_group;
                        return false;
                    }
                    if (!skip(w, n, err, depth + 1)) return false;
                }
            }
            case wire_type::egroup:
                err = read_error::malformed_group;
                return false;
        }
        err = read_error::bad_wire_type;
        return false;
    }
};

// Strict UTF-8 (RFC 3629): no overlong forms, no surrogates, nothing above U+10FFFF.
[[nodiscard]] constexpr bool valid_utf8(std::string_view s) noexcept {
    const auto at = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = at(i);
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        unsigned char lo = 0x80;
        unsigned char hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
        } else if (c == 0xE0) {
            len = 3;
            lo = 0xA0;
        } else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) {
            len = 3;
        } else if (c == 0xED) {
            len = 3;
            hi = 0x9F;
        } else if (c == 0xF0) {
            len = 4;
            lo = 0x90;
        } else if (c >= 0xF1 && c <= 0xF3) {
            len = 4;
        } else if (c == 0xF4) {
            len = 4;
            hi = 0x8F;
        } else {
            return false;
        }
        if (n - i < len) return false;
        if (at(i + 1) < lo || at(i + 1) > hi) return false;
        for (std::size_t k = 2; k < len; ++k)
            if (at(i + k) < 0x80 || at(i + k) > 0xBF) return false;
        i += len;
    }
    return true;
}

}  // namespace tep::wire
