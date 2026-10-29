// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Portable proto3 front end: lexer, recursive-descent parser, semantic checks and name
// resolution. Plain C++23, usable at run time and in constant evaluation. It never throws:
// the first error is returned with its line and column. The reflection layer turns that error
// into a compile-time diagnostic.
//
// Lifetime: names in the returned file and the token of an error view the source text, so the
// source must outlive them.
#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace tep::schema {

inline constexpr std::uint32_t npos = 0xFFFF'FFFFu;
inline constexpr std::uint32_t max_field_number = (1u << 29) - 1;
inline constexpr std::uint32_t first_implementation_number = 19000;
inline constexpr std::uint32_t last_implementation_number = 19999;
inline constexpr std::uint32_t max_nesting = 32;

enum class scalar : std::uint8_t {
    double_, float_, int64, uint64, int32, fixed64, fixed32, bool_,
    string, bytes, uint32, sfixed32, sfixed64, sint32, sint64,
};

enum class field_kind : std::uint8_t { scalar, enumeration, message };

[[nodiscard]] constexpr bool is_packable(scalar s) noexcept {
    return s != scalar::string && s != scalar::bytes;
}

enum class errc : std::uint8_t {
    unexpected_token,
    unexpected_end,
    unterminated_comment,
    unterminated_string,
    invalid_character,
    expected_identifier,
    expected_integer,
    expected_string,
    integer_overflow,
    missing_syntax,
    syntax_not_proto3,
    duplicate_package,
    field_number_out_of_range,
    field_number_in_implementation_range,
    duplicate_field_number,
    duplicate_field_name,
    field_number_reserved,
    field_name_reserved,
    bad_reserved_range,
    reserved_overlap,
    duplicate_reserved_name,
    json_name_conflict,
    duplicate_definition,
    unknown_type,
    spaced_type_name,
    not_a_message_type,
    recursive_message,
    empty_enum,
    enum_first_value_not_zero,
    duplicate_enum_value_name,
    enum_value_alias,
    allow_alias_unused,
    enum_value_name_conflict,
    enum_value_out_of_range,
    packed_on_non_packable,
    unsupported_option,
    unsupported_map,
    unsupported_oneof,
    unsupported_optional,
    unsupported_label,
    unsupported_streaming,
    unsupported_import,
    unsupported_extensions,
    unsupported_group,
    unsupported_editions,
    nesting_too_deep,
};

[[nodiscard]] constexpr std::string_view describe(errc e) noexcept {
    switch (e) {
        case errc::unexpected_token: return "unexpected token";
        case errc::unexpected_end: return "unexpected end of schema";
        case errc::unterminated_comment: return "unterminated block comment";
        case errc::unterminated_string: return "unterminated string literal";
        case errc::invalid_character: return "invalid character";
        case errc::expected_identifier: return "expected an identifier";
        case errc::expected_integer: return "expected an integer";
        case errc::expected_string: return "expected a string literal";
        case errc::integer_overflow: return "integer literal out of range";
        case errc::missing_syntax: return "missing 'syntax = \"proto3\";' as the first statement";
        case errc::syntax_not_proto3: return "only syntax \"proto3\" is supported";
        case errc::duplicate_package: return "more than one package statement";
        case errc::field_number_out_of_range: return "field number must be in 1 to 536870911";
        case errc::field_number_in_implementation_range:
            return "field numbers 19000 to 19999 are reserved for the protobuf implementation";
        case errc::duplicate_field_number: return "duplicate field number";
        case errc::duplicate_field_name: return "duplicate field name";
        case errc::field_number_reserved: return "field number is reserved";
        case errc::field_name_reserved: return "field name is reserved";
        case errc::bad_reserved_range: return "reserved range has its start after its end";
        case errc::reserved_overlap: return "reserved ranges overlap";
        case errc::duplicate_reserved_name: return "name is reserved twice";
        case errc::json_name_conflict: return "two fields have the same JSON name";
        case errc::duplicate_definition: return "name is defined twice";
        case errc::unknown_type: return "unknown type name";
        case errc::spaced_type_name:
            return "a qualified type name with spaces or comments between its parts is not supported";
        case errc::not_a_message_type: return "rpc request and response types must be messages";
        case errc::recursive_message: return "recursive message types are not supported";
        case errc::empty_enum: return "enum has no values";
        case errc::enum_first_value_not_zero: return "the first enum value must be zero in proto3";
        case errc::duplicate_enum_value_name: return "duplicate enum value name";
        case errc::enum_value_alias: return "two enum values share a number without allow_alias";
        case errc::allow_alias_unused: return "allow_alias is set but no two values share a number";
        case errc::enum_value_name_conflict:
            return "two enum values have the same name once the enum name prefix and case are ignored";
        case errc::enum_value_out_of_range: return "enum value must fit in int32";
        case errc::packed_on_non_packable:
            return "[packed] is only valid on repeated numeric, bool and enum fields";
        case errc::unsupported_option:
            return "unknown or unsupported option (custom options need an import, which is not supported)";
        case errc::unsupported_map: return "map fields are not supported";
        case errc::unsupported_oneof: return "oneof is not supported";
        case errc::unsupported_optional: return "proto3 optional fields are not supported";
        case errc::unsupported_label: return "only the 'repeated' label is supported";
        case errc::unsupported_streaming: return "streaming rpcs are not supported (unary only)";
        case errc::unsupported_import: return "import is not supported";
        case errc::unsupported_extensions: return "extensions are not supported";
        case errc::unsupported_group: return "groups are not supported";
        case errc::unsupported_editions: return "editions are not supported";
        case errc::nesting_too_deep: return "definitions are nested too deeply";
    }
    return "unknown error";
}

struct location {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct error {
    errc code{};
    location loc{};
    std::string_view token{};
};

struct field {
    std::string_view name;
    std::uint32_t number = 0;
    field_kind kind = field_kind::scalar;
    scalar type = scalar::int32;
    std::string_view type_name;      // as written, a view of the source, for enum and message fields
    std::uint32_t type_index = npos; // resolved index into file::enums or file::messages
    bool repeated = false;
    bool packed = false;             // effective encoding of a repeated field
    std::int8_t packed_option = -1;  // -1 unset, 0 [packed = false], 1 [packed = true]
    location loc{};                  // the field name
    location type_loc{};
    location number_loc{};
    std::string_view type_text;      // the type as written, viewing the source
    std::string_view number_text;
};

struct reserved_range {
    std::int64_t lo = 0;
    std::int64_t hi = 0;  // inclusive
    location loc{};
    std::string_view text;
};

struct reserved_name {
    std::string_view name;
    location loc{};
};

struct message {
    std::string_view name;
    std::string_view full_name;  // a view of file::name_buffer
    std::uint32_t parent = npos;
    std::vector<field> fields;
    std::vector<reserved_range> reserved_ranges;
    std::vector<reserved_name> reserved_names;
    location loc{};
};

struct enum_value {
    std::string_view name;
    std::int32_t number = 0;
    location loc{};
};

struct enumeration {
    std::string_view name;
    std::string_view full_name;  // a view of file::name_buffer
    std::uint32_t parent = npos;
    std::vector<enum_value> values;
    std::vector<reserved_range> reserved_ranges;
    std::vector<reserved_name> reserved_names;
    bool allow_alias = false;
    location loc{};
};

struct rpc {
    std::string_view name;
    std::string_view request_name;   // as written, a view of the source
    std::string_view response_name;  // as written, a view of the source
    std::uint32_t request = npos;
    std::uint32_t response = npos;
    location loc{};
    location request_loc{};
    location response_loc{};
};

struct service {
    std::string_view name;
    std::string_view full_name;  // a view of file::name_buffer
    std::vector<rpc> rpcs;
    location loc{};
};

struct file {
    // Full names view name_buffer, so a file can be moved but not copied.
    constexpr file() = default;
    constexpr file(file&&) noexcept = default;
    constexpr file& operator=(file&&) noexcept = default;
    file(const file&) = delete;
    file& operator=(const file&) = delete;
    constexpr ~file() = default;

    std::string package;
    std::vector<char> name_buffer;  // every full name, back to back
    std::vector<message> messages;
    std::vector<enumeration> enums;
    std::vector<service> services;
    std::vector<std::uint32_t> definition_order;  // message indices, dependencies first

    [[nodiscard]] constexpr std::uint32_t find_message(std::string_view full) const noexcept {
        for (std::size_t i = 0; i < messages.size(); ++i)
            if (messages[i].full_name == full) return static_cast<std::uint32_t>(i);
        return npos;
    }
    [[nodiscard]] constexpr std::uint32_t find_enum(std::string_view full) const noexcept {
        for (std::size_t i = 0; i < enums.size(); ++i)
            if (enums[i].full_name == full) return static_cast<std::uint32_t>(i);
        return npos;
    }
    [[nodiscard]] constexpr std::uint32_t find_service(std::string_view full) const noexcept {
        for (std::size_t i = 0; i < services.size(); ++i)
            if (services[i].full_name == full) return static_cast<std::uint32_t>(i);
        return npos;
    }
};

// ---------------------------------------------------------------------------------------------
// Lexer

enum class tok : std::uint8_t { end, ident, integer, floating, string, symbol };

struct token {
    tok kind = tok::end;
    std::string_view text;  // for strings: the contents between the quotes, escapes not decoded
    location loc{};
};

namespace detail {

// Equality by pointer walk: in constant evaluation every call and every checked index is charged,
// so string_view's operator== (through char_traits) costs several times more.
[[nodiscard]] constexpr bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    const char* p = a.data();
    const char* q = b.data();
    for (const char* const e = p + a.size(); p != e; ++p, ++q)
        if (*p != *q) return false;
    return true;
}

// Append-only storage whose capacity is fixed when it is made, for trivially destructible
// elements. In constant evaluation a construct_at into allocated storage costs about a fifth of
// a vector's push_back, so the lexer and the parser keep their working lists here; the
// capacity is an upper bound computed up front. Storage is released without destructor calls.
template <class T>
class fixed_buffer {
    static_assert(std::is_trivially_destructible_v<T>);

public:
    constexpr fixed_buffer() noexcept = default;
    constexpr explicit fixed_buffer(std::size_t capacity)
        : data_(std::allocator<T>{}.allocate(capacity)), capacity_(capacity) {}
    constexpr fixed_buffer(fixed_buffer&& o) noexcept : data_(o.data_), size_(o.size_), capacity_(o.capacity_) {
        o.data_ = nullptr;
        o.size_ = 0;
        o.capacity_ = 0;
    }
    constexpr fixed_buffer& operator=(fixed_buffer&& o) noexcept {
        if (this != &o) {
            release();
            data_ = o.data_;
            size_ = o.size_;
            capacity_ = o.capacity_;
            o.data_ = nullptr;
            o.size_ = 0;
            o.capacity_ = 0;
        }
        return *this;
    }
    fixed_buffer(const fixed_buffer&) = delete;
    fixed_buffer& operator=(const fixed_buffer&) = delete;
    constexpr ~fixed_buffer() { release(); }

    // Precondition: size() < capacity().
    constexpr void push_back(const T& v) noexcept {
        std::construct_at(data_ + size_, v);
        ++size_;
    }
    // Precondition: n <= size().
    constexpr void truncate(std::size_t n) noexcept { size_ = n; }
    [[nodiscard]] constexpr T* data() noexcept { return data_; }
    [[nodiscard]] constexpr const T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return capacity_; }

private:
    constexpr void release() noexcept {
        if (data_ != nullptr) std::allocator<T>{}.deallocate(data_, capacity_);
    }
    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

[[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr bool is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }
[[nodiscard]] constexpr bool is_hex(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
// A switch, not string_view::find (see split_path in grpc.hpp for the GCC 16.2.1 defect).
[[nodiscard]] constexpr bool is_symbol_char(char c) noexcept {
    switch (c) {
        case '{': case '}': case '(': case ')': case '[': case ']': case ';': case '=':
        case ',': case '<': case '>': case '.': case '-': case '+': case ':': return true;
        default: return false;
    }
}
// Character classes for the lexer's loops: one table lookup per character instead of a chain of
// comparisons and calls.
inline constexpr std::uint8_t cls_space = 1;  // blank other than a newline
inline constexpr std::uint8_t cls_alpha = 2;  // starts and continues an identifier
inline constexpr std::uint8_t cls_digit = 4;
inline constexpr std::uint8_t cls_symbol = 8;
inline constexpr std::uint8_t cls_alnum = cls_alpha | cls_digit;

struct char_classes {
    std::uint8_t of[256];
};

[[nodiscard]] constexpr char_classes make_char_classes() noexcept {
    char_classes t{};
    for (int i = 0; i < 256; ++i) {
        const auto c = static_cast<char>(i);
        const bool blank = c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
        t.of[i] = static_cast<std::uint8_t>((blank ? cls_space : 0) | (is_alpha(c) ? cls_alpha : 0) |
                                            (is_digit(c) ? cls_digit : 0) | (is_symbol_char(c) ? cls_symbol : 0));
    }
    return t;
}

inline constexpr char_classes char_class = make_char_classes();

[[nodiscard]] constexpr int hex_value(char c) noexcept {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

// Parses a protobuf integer literal (decimal, hex or octal). Returns false on a malformed
// literal; sets overflow when the value does not fit in 64 bits.
[[nodiscard]] constexpr bool parse_integer(std::string_view t, std::uint64_t& out,
                                           bool& overflow) noexcept {
    overflow = false;
    out = 0;
    if (t.empty()) return false;
    std::uint64_t base = 10;
    std::size_t i = 0;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        i = 2;
    } else if (t.size() > 1 && t[0] == '0') {
        base = 8;
        i = 1;
    }
    for (; i < t.size(); ++i) {
        const char c = t[i];
        std::uint64_t d = 0;
        if (base == 16) {
            if (!is_hex(c)) return false;
            d = static_cast<std::uint64_t>(hex_value(c));
        } else {
            if (!is_digit(c)) return false;
            d = static_cast<std::uint64_t>(c - '0');
            if (d >= base) return false;
        }
        if (out > (~std::uint64_t{0} - d) / base) overflow = true;
        out = out * base + d;
    }
    return true;
}

[[nodiscard]] constexpr bool is_integer_literal(std::string_view t) noexcept {
    std::uint64_t v = 0;
    bool overflow = false;
    return parse_integer(t, v, overflow);
}

}  // namespace detail

// Splits the whole source into tokens. One pass; tokens view the source. It walks pointers and
// derives a column from the start of the line, because constant evaluation charges for every
// checked index and every call: this keeps the cost per character small. A token takes at least
// one character, so the buffer holds the source's length plus the end token.
[[nodiscard]] constexpr std::expected<detail::fixed_buffer<token>, error> tokenize(std::string_view src) {
    detail::fixed_buffer<token> out(src.size() + 1);
    const std::uint8_t* const cls = detail::char_class.of;
    const char* const end = src.data() + src.size();
    const char* p = src.data();
    const char* line_start = p;
    std::uint32_t line = 1;
    const auto view = [](const char* a, const char* b) {
        return std::string_view(a, static_cast<std::size_t>(b - a));
    };
    while (p != end) {
        const char c = *p;
        const std::uint8_t k = cls[static_cast<unsigned char>(c)];
        if ((k & detail::cls_space) != 0) {
            ++p;
            continue;
        }
        if (c == '\n') {
            ++p;
            ++line;
            line_start = p;
            continue;
        }
        const char* const b = p;
        const location start{line, static_cast<std::uint32_t>(p - line_start) + 1};
        if ((k & detail::cls_alpha) != 0) {
            do {
                ++p;
            } while (p != end && (cls[static_cast<unsigned char>(*p)] & detail::cls_alnum) != 0);
            out.push_back({tok::ident, view(b, p), start});
            continue;
        }
        if ((k & detail::cls_digit) != 0 ||
            (c == '.' && end - p > 1 && (cls[static_cast<unsigned char>(p[1])] & detail::cls_digit) != 0)) {
            const bool hex = end - b > 1 && b[0] == '0' && (b[1] == 'x' || b[1] == 'X');
            while (p != end) {
                const char d = *p;
                if ((cls[static_cast<unsigned char>(d)] & detail::cls_alnum) != 0 || d == '.') {
                    ++p;
                } else if ((d == '+' || d == '-') && !hex && (p[-1] == 'e' || p[-1] == 'E')) {
                    ++p;
                } else {
                    break;
                }
            }
            const std::string_view text = view(b, p);
            out.push_back({detail::is_integer_literal(text) ? tok::integer : tok::floating, text, start});
            continue;
        }
        if ((k & detail::cls_symbol) != 0) {
            ++p;
            out.push_back({tok::symbol, view(b, p), start});
            continue;
        }
        if (c == '/' && end - p > 1 && p[1] == '/') {
            while (p != end && *p != '\n') ++p;
            continue;
        }
        if (c == '/' && end - p > 1 && p[1] == '*') {
            p += 2;
            bool closed = false;
            while (p != end) {
                if (*p == '*' && end - p > 1 && p[1] == '/') {
                    p += 2;
                    closed = true;
                    break;
                }
                if (*p == '\n') {
                    ++line;
                    line_start = p + 1;
                }
                ++p;
            }
            if (!closed) return std::unexpected(error{errc::unterminated_comment, start, "/*"});
            continue;
        }
        if (c == '"' || c == '\'') {
            ++p;
            const char* const content = p;
            bool closed = false;
            while (p != end) {
                const char d = *p;
                if (d == '\n') break;
                if (d == '\\') {
                    // An escape may not end the input or the line: protoc rejects a backslash before
                    // a newline, and stepping over it would also lose the line count.
                    if (end - p < 2 || p[1] == '\n') break;
                    p += 2;
                    continue;
                }
                if (d == c) {
                    closed = true;
                    break;
                }
                ++p;
            }
            if (!closed) return std::unexpected(error{errc::unterminated_string, start, view(b, b + 1)});
            out.push_back({tok::string, view(content, p), start});
            ++p;
            continue;
        }
        return std::unexpected(error{errc::invalid_character, start, view(b, b + 1)});
    }
    out.push_back({tok::end, {}, location{line, static_cast<std::uint32_t>(p - line_start) + 1}});
    return out;
}

// ---------------------------------------------------------------------------------------------
// Parser

namespace detail {

using namespace std::string_view_literals;

// A scalar type name is picked by its first letter, length and last letter, then compared once.
[[nodiscard]] constexpr bool find_scalar(std::string_view n, scalar& out) noexcept {
    const std::size_t len = n.size();
    if (len < 4 || len > 8) return false;
    const char last = n.data()[len - 1];
    std::string_view want;
    scalar type{};
    const auto pick = [&](std::string_view w, scalar t) {
        want = w;
        type = t;
    };
    switch (n.data()[0]) {
        case 'b':
            if (len == 4) pick("bool"sv, scalar::bool_);
            else pick("bytes"sv, scalar::bytes);
            break;
        case 'd': pick("double"sv, scalar::double_); break;
        case 'f':
            if (len == 5) pick("float"sv, scalar::float_);
            else if (last == '4') pick("fixed64"sv, scalar::fixed64);
            else pick("fixed32"sv, scalar::fixed32);
            break;
        case 'i':
            if (last == '4') pick("int64"sv, scalar::int64);
            else pick("int32"sv, scalar::int32);
            break;
        case 's':
            if (len == 8 && last == '4') pick("sfixed64"sv, scalar::sfixed64);
            else if (len == 8) pick("sfixed32"sv, scalar::sfixed32);
            else if (last == 'g') pick("string"sv, scalar::string);
            else if (last == '4') pick("sint64"sv, scalar::sint64);
            else pick("sint32"sv, scalar::sint32);
            break;
        case 'u':
            if (last == '4') pick("uint64"sv, scalar::uint64);
            else pick("uint32"sv, scalar::uint32);
            break;
        default: return false;
    }
    if (!same(n, want)) return false;
    out = type;
    return true;
}

class parser {
public:
    // The token count bounds every list: a field takes at least five tokens, a message, enum or
    // service four, an rpc nine. Reserving those bounds up front keeps the lists from growing.
    constexpr explicit parser(fixed_buffer<token> toks)
        : buf_(std::move(toks)), toks_(buf_.data()), ntoks_(buf_.size()), pending_(ntoks_ / 5 + 1) {
        out_.messages.reserve(ntoks_ / 4 + 1);
        out_.enums.reserve(ntoks_ / 4 + 1);
        out_.services.reserve(ntoks_ / 4 + 1);
    }

    [[nodiscard]] constexpr bool parse_file() {
        if (!parse_syntax()) return false;
        while (!at_end()) {
            const token& t = peek();
            if (is_symbol(t, ';')) {
                ++i_;
            } else if (is_word(t, "package"sv)) {
                if (!parse_package()) return false;
            } else if (is_word(t, "import"sv)) {
                return fail(errc::unsupported_import, t);
            } else if (is_word(t, "option"sv)) {
                if (!parse_option(level::file)) return false;
            } else if (is_word(t, "message"sv)) {
                if (!parse_message(npos)) return false;
            } else if (is_word(t, "enum"sv)) {
                if (!parse_enum(npos)) return false;
            } else if (is_word(t, "service"sv)) {
                if (!parse_service()) return false;
            } else if (is_word(t, "extend"sv)) {
                return fail(errc::unsupported_extensions, t);
            } else {
                return fail(errc::unexpected_token, t);
            }
        }
        return true;
    }

    [[nodiscard]] constexpr file& result() noexcept { return out_; }
    [[nodiscard]] constexpr const error& failure() const noexcept { return err_; }

private:
    enum class level : std::uint8_t { file, message, field, enumeration, enum_value, service, rpc };

    fixed_buffer<token> buf_;
    const token* toks_;
    std::size_t ntoks_;
    fixed_buffer<field> pending_;  // fields of the messages being parsed, innermost last
    std::size_t i_ = 0;
    file out_{};
    error err_{};
    std::uint32_t depth_ = 0;
    bool have_package_ = false;

    [[nodiscard]] constexpr const token& peek(std::size_t ahead = 0) const noexcept {
        const std::size_t k = i_ + ahead;
        return toks_[k < ntoks_ ? k : ntoks_ - 1];
    }
    [[nodiscard]] constexpr bool at_end() const noexcept { return peek().kind == tok::end; }
    // The source text from the start of token a to the end of token b.
    [[nodiscard]] constexpr std::string_view span(std::size_t a, std::size_t b) const noexcept {
        const std::string_view x = toks_[a].text;
        const std::string_view y = toks_[b].text;
        return {x.data(), static_cast<std::size_t>(y.data() + y.size() - x.data())};
    }
    // A symbol token is one character.
    [[nodiscard]] static constexpr bool is_symbol(const token& t, char c) noexcept {
        return t.kind == tok::symbol && *t.text.data() == c;
    }
    [[nodiscard]] static constexpr bool is_word(const token& t, std::string_view w) noexcept {
        return t.kind == tok::ident && same(t.text, w);
    }
    constexpr bool fail(errc code, const token& t) {
        err_ = error{t.kind == tok::end ? errc::unexpected_end : code, t.loc, t.text};
        return false;
    }
    constexpr bool fail_at(errc code, location loc, std::string_view text) {
        err_ = error{code, loc, text};
        return false;
    }
    [[nodiscard]] constexpr bool expect_symbol(char c) {
        if (!is_symbol(peek(), c)) return fail(errc::unexpected_token, peek());
        ++i_;
        return true;
    }
    [[nodiscard]] constexpr bool expect_ident(std::string_view& out) {
        const token& t = peek();
        if (t.kind != tok::ident) return fail(errc::expected_identifier, t);
        out = t.text;
        ++i_;
        return true;
    }
    [[nodiscard]] constexpr bool expect_integer(std::uint64_t& out) {
        const token& t = peek();
        if (t.kind != tok::integer) return fail(errc::expected_integer, t);
        bool overflow = false;
        (void)parse_integer(t.text, out, overflow);
        if (overflow) return fail(errc::integer_overflow, t);
        ++i_;
        return true;
    }
    // ['.'] ident ('.' ident)*, as one view of the source. A name with spaces or comments between
    // its parts is refused (protoc accepts it; schemas do not write it), so every type name is a
    // view and no string is built: in constant evaluation a built string costs far more.
    [[nodiscard]] constexpr bool parse_type_ref(std::string_view& out) {
        const std::size_t first = i_;
        if (is_symbol(peek(), '.')) ++i_;
        std::string_view part;
        if (!expect_ident(part)) return false;
        while (is_symbol(peek(), '.')) {
            ++i_;
            if (!expect_ident(part)) return false;
        }
        for (std::size_t k = first + 1; k < i_; ++k) {
            const std::string_view a = toks_[k - 1].text;
            if (a.data() + a.size() != toks_[k].text.data())
                return fail_at(errc::spaced_type_name, toks_[first].loc, span(first, i_ - 1));
        }
        out = span(first, i_ - 1);
        return true;
    }

    [[nodiscard]] constexpr bool parse_syntax() {
        const token& t = peek();
        if (is_word(t, "edition"sv)) return fail(errc::unsupported_editions, t);
        if (!is_word(t, "syntax"sv)) return fail(errc::missing_syntax, t);
        ++i_;
        if (!expect_symbol('=')) return false;
        const token& v = peek();
        if (v.kind != tok::string) return fail(errc::expected_string, v);
        if (v.text != "proto3"sv) return fail(errc::syntax_not_proto3, v);
        ++i_;
        return expect_symbol(';');
    }

    [[nodiscard]] constexpr bool parse_package() {
        const token& kw = peek();
        if (have_package_) return fail(errc::duplicate_package, kw);
        ++i_;
        if (is_symbol(peek(), '.')) return fail(errc::unexpected_token, peek());
        std::string_view name;
        if (!parse_type_ref(name)) return false;
        out_.package = std::string(name);
        have_package_ = true;
        return expect_symbol(';');
    }

    // A constant: identifier, signed number, one or more adjacent strings, or a {...} aggregate.
    [[nodiscard]] constexpr bool skip_constant() {
        const token& t = peek();
        if (t.kind == tok::ident || t.kind == tok::integer || t.kind == tok::floating) {
            ++i_;
            return true;
        }
        if (is_symbol(t, '-') || is_symbol(t, '+')) {
            ++i_;
            const token& n = peek();
            if (n.kind != tok::integer && n.kind != tok::floating && n.kind != tok::ident)
                return fail(errc::unexpected_token, n);
            ++i_;
            return true;
        }
        if (t.kind == tok::string) {
            while (peek().kind == tok::string) ++i_;
            return true;
        }
        if (is_symbol(t, '{')) {
            std::uint32_t open = 0;
            do {
                const token& k = peek();
                if (k.kind == tok::end) return fail(errc::unexpected_end, k);
                if (is_symbol(k, '{')) ++open;
                if (is_symbol(k, '}')) --open;
                ++i_;
            } while (open != 0);
            return true;
        }
        return fail(errc::unexpected_token, t);
    }

    // optionName: ident ('.' ident)* | '(' ['.'] ident ('.' ident)* ')' ('.' ident)*
    // `name` views the whole name as written; `custom` is set for a parenthesized name.
    [[nodiscard]] constexpr bool parse_option_name(std::string_view& name, location& loc, bool& custom) {
        const std::size_t first = i_;
        loc = peek().loc;
        custom = false;
        if (is_symbol(peek(), '(')) {
            custom = true;
            ++i_;
            std::string_view ignored;
            if (!parse_type_ref(ignored)) return false;
            if (!expect_symbol(')')) return false;
        } else {
            std::string_view part;
            if (!expect_ident(part)) return false;
        }
        while (is_symbol(peek(), '.')) {
            ++i_;
            std::string_view part;
            if (!expect_ident(part)) return false;
        }
        name = span(first, i_ - 1);
        return true;
    }

    // Options from descriptor.proto, per level. Options that change the wire format
    // (message_set_wire_format, map_entry, default) are left out and so refused. A custom option
    // needs an import, which is not supported, so it is refused as protoc would refuse it.
    [[nodiscard]] static constexpr bool known_option(level lv, std::string_view name) noexcept {
        static constexpr std::string_view file_options[] = {
            "java_package", "java_outer_classname", "java_multiple_files", "java_generate_equals_and_hash",
            "java_string_check_utf8", "optimize_for", "go_package", "cc_generic_services", "java_generic_services",
            "py_generic_services", "deprecated", "cc_enable_arenas", "objc_class_prefix", "csharp_namespace",
            "swift_prefix", "php_class_prefix", "php_namespace", "php_metadata_namespace", "ruby_package"};
        static constexpr std::string_view message_options[] = {"no_standard_descriptor_accessor", "deprecated",
                                                        "deprecated_legacy_json_field_conflicts"};
        static constexpr std::string_view field_options[] = {"ctype", "packed", "jstype", "lazy", "unverified_lazy",
                                                      "deprecated", "weak", "debug_redact", "retention", "targets",
                                                      "json_name"};
        static constexpr std::string_view enum_options[] = {"allow_alias", "deprecated",
                                                     "deprecated_legacy_json_field_conflicts"};
        static constexpr std::string_view enum_value_options[] = {"deprecated", "debug_redact"};
        static constexpr std::string_view service_options[] = {"deprecated"};
        static constexpr std::string_view rpc_options[] = {"deprecated", "idempotency_level"};
        const auto in = [&](const auto& list) {
            for (const std::string_view o : list)
                if (o == name) return true;
            return false;
        };
        switch (lv) {
            case level::file: return in(file_options);
            case level::message: return in(message_options);
            case level::field: return in(field_options);
            case level::enumeration: return in(enum_options);
            case level::enum_value: return in(enum_value_options);
            case level::service: return in(service_options);
            case level::rpc: return in(rpc_options);
        }
        return false;
    }

    [[nodiscard]] constexpr bool check_option_name(level lv, std::string_view name, location loc, bool custom) {
        if (name.starts_with("features"sv)) return fail_at(errc::unsupported_editions, loc, name);
        if (custom || !known_option(lv, name)) return fail_at(errc::unsupported_option, loc, name);
        return true;
    }

    // `option name = constant;` at file, message, enum, service or rpc level.
    [[nodiscard]] constexpr bool parse_option(level lv, bool* allow_alias = nullptr) {
        ++i_;  // 'option'
        std::string_view name;
        location loc{};
        bool custom = false;
        if (!parse_option_name(name, loc, custom)) return false;
        if (!check_option_name(lv, name, loc, custom)) return false;
        if (!expect_symbol('=')) return false;
        const token& value = peek();
        if (lv == level::enumeration && name == "allow_alias"sv) {
            if (!is_word(value, "true"sv) && !is_word(value, "false"sv)) return fail(errc::unexpected_token, value);
            if (allow_alias != nullptr) *allow_alias = is_word(value, "true"sv);
        }
        if (!skip_constant()) return false;
        return expect_symbol(';');
    }

    // '[' option (',' option)* ']' on a field or an enum value. Only `packed` changes the wire
    // format.
    [[nodiscard]] constexpr bool parse_field_options(field& f, level lv) {
        ++i_;  // '['
        for (;;) {
            std::string_view name;
            location loc{};
            bool custom = false;
            if (!parse_option_name(name, loc, custom)) return false;
            if (!check_option_name(lv, name, loc, custom)) return false;
            if (!expect_symbol('=')) return false;
            const token& value = peek();
            if (name == "packed"sv) {
                if (f.packed_option != -1) return fail_at(errc::unsupported_option, loc, name);
                if (is_word(value, "true"sv)) {
                    f.packed_option = 1;
                } else if (is_word(value, "false"sv)) {
                    f.packed_option = 0;
                } else {
                    return fail(errc::unexpected_token, value);
                }
            }
            if (!skip_constant()) return false;
            if (is_symbol(peek(), ',')) {
                ++i_;
                continue;
            }
            return expect_symbol(']');
        }
    }

    [[nodiscard]] constexpr bool read_reserved_bound(std::int64_t& v, bool is_enum) {
        const std::int64_t max_value = is_enum ? 2147483647 : std::int64_t{max_field_number};
        bool negative = false;
        if (is_enum && is_symbol(peek(), '-')) {
            negative = true;
            ++i_;
        }
        const token& t = peek();
        std::uint64_t u = 0;
        if (!expect_integer(u)) return false;
        if (u > static_cast<std::uint64_t>(max_value) + (negative ? 1u : 0u) || (!is_enum && u == 0))
            return fail_at(is_enum ? errc::enum_value_out_of_range : errc::field_number_out_of_range, t.loc,
                           t.text);
        v = negative ? -static_cast<std::int64_t>(u) : static_cast<std::int64_t>(u);
        return true;
    }

    // reserved 1, 2, 9 to 11, 40 to max;   or   reserved "a", "b";
    [[nodiscard]] constexpr bool parse_reserved(std::vector<reserved_range>& ranges,
                                                std::vector<reserved_name>& names, bool is_enum) {
        ++i_;  // 'reserved'
        if (peek().kind == tok::string) {
            for (;;) {
                const token& s = peek();
                if (s.kind != tok::string) return fail(errc::expected_string, s);
                names.push_back({s.text, s.loc});
                ++i_;
                if (!is_symbol(peek(), ',')) break;
                ++i_;
            }
            return expect_symbol(';');
        }
        for (;;) {
            const std::size_t first = i_;
            reserved_range r{};
            r.loc = peek().loc;
            if (!read_reserved_bound(r.lo, is_enum)) return false;
            r.hi = r.lo;
            if (is_word(peek(), "to"sv)) {
                ++i_;
                if (is_word(peek(), "max"sv)) {
                    r.hi = is_enum ? 2147483647 : std::int64_t{max_field_number};
                    ++i_;
                } else if (!read_reserved_bound(r.hi, is_enum)) {
                    return false;
                }
            }
            r.text = span(first, i_ - 1);
            if (r.lo > r.hi) return fail_at(errc::bad_reserved_range, r.loc, r.text);
            ranges.push_back(r);
            if (!is_symbol(peek(), ',')) break;
            ++i_;
        }
        return expect_symbol(';');
    }

    [[nodiscard]] constexpr bool parse_field(bool repeated) {
        field f{};
        f.repeated = repeated;
        const token& type_tok = peek();
        if (is_word(type_tok, "group"sv)) return fail(errc::unsupported_group, type_tok);
        f.type_loc = type_tok.loc;
        const std::size_t type_first = i_;
        if (type_tok.kind == tok::ident && find_scalar(type_tok.text, f.type)) {
            f.kind = field_kind::scalar;
            ++i_;
        } else {
            f.kind = field_kind::message;  // provisional; resolution may make it an enum
            if (!parse_type_ref(f.type_name)) return false;
        }
        f.type_text = span(type_first, i_ - 1);
        const token& name_tok = peek();
        if (!expect_ident(f.name)) return false;
        f.loc = name_tok.loc;
        if (!expect_symbol('=')) return false;
        const token& num_tok = peek();
        f.number_loc = num_tok.loc;
        f.number_text = num_tok.text;
        std::uint64_t number = 0;
        if (!expect_integer(number)) return false;
        if (number == 0 || number > max_field_number)
            return fail_at(errc::field_number_out_of_range, num_tok.loc, num_tok.text);
        f.number = static_cast<std::uint32_t>(number);
        if (is_symbol(peek(), '[') && !parse_field_options(f, level::field)) return false;
        if (!expect_symbol(';')) return false;
        pending_.push_back(f);
        return true;
    }

    [[nodiscard]] constexpr bool parse_message_item(std::uint32_t index) {
        const token& t = peek();
        if (is_symbol(t, ';')) {
            ++i_;
            return true;
        }
        // Only words starting with e, m, o or r can be keywords here. Scalar types and most type
        // names go straight to the field, without a keyword comparison.
        const char first = t.kind == tok::ident ? t.text.front() : '\0';
        if (first == 'e' || first == 'm' || first == 'o' || first == 'r') {
            if (is_word(t, "message"sv)) return parse_message(index);
            if (is_word(t, "enum"sv)) return parse_enum(index);
            if (is_word(t, "reserved"sv))
                return parse_reserved(out_.messages[index].reserved_ranges, out_.messages[index].reserved_names,
                                      false);
            if (is_word(t, "option"sv)) return parse_option(level::message);
            if (is_word(t, "oneof"sv)) return fail(errc::unsupported_oneof, t);
            if (is_word(t, "map"sv) && is_symbol(peek(1), '<')) return fail(errc::unsupported_map, t);
            if (is_word(t, "optional"sv)) return fail(errc::unsupported_optional, t);
            if (is_word(t, "required"sv)) return fail(errc::unsupported_label, t);
            if (is_word(t, "extensions"sv) || is_word(t, "extend"sv)) return fail(errc::unsupported_extensions, t);
            if (is_word(t, "repeated"sv)) {
                ++i_;
                return parse_field(true);
            }
        }
        return parse_field(false);
    }

    [[nodiscard]] constexpr bool parse_message(std::uint32_t parent) {
        const token& kw = peek();
        ++i_;  // 'message'
        if (depth_ >= max_nesting) return fail(errc::nesting_too_deep, kw);
        std::string_view name;
        const token& name_tok = peek();
        if (!expect_ident(name)) return false;
        if (!expect_symbol('{')) return false;
        const auto index = static_cast<std::uint32_t>(out_.messages.size());
        message m{};
        m.name = name;
        m.parent = parent;
        m.loc = name_tok.loc;
        out_.messages.push_back(std::move(m));
        ++depth_;
        const std::size_t mark = pending_.size();
        for (;;) {
            const token& t = peek();
            if (t.kind == tok::end) return fail(errc::unexpected_end, t);
            if (is_symbol(t, '}')) {
                ++i_;
                break;
            }
            if (!parse_message_item(index)) return false;
        }
        // Nested messages took their fields off the stack, so this message's are contiguous.
        out_.messages[index].fields.assign(pending_.data() + mark, pending_.data() + pending_.size());
        pending_.truncate(mark);
        --depth_;
        return true;
    }

    [[nodiscard]] constexpr bool parse_enum_value(enumeration& e) {
        const token& t = peek();
        enum_value v{};
        v.loc = t.loc;
        if (!expect_ident(v.name)) return false;
        if (!expect_symbol('=')) return false;
        bool negative = false;
        if (is_symbol(peek(), '-')) {
            negative = true;
            ++i_;
        }
        const token& num_tok = peek();
        std::uint64_t u = 0;
        if (!expect_integer(u)) return false;
        if (u > (negative ? 2147483648ull : 2147483647ull))
            return fail_at(errc::enum_value_out_of_range, num_tok.loc, num_tok.text);
        v.number = negative ? static_cast<std::int32_t>(-static_cast<std::int64_t>(u)) : static_cast<std::int32_t>(u);
        if (is_symbol(peek(), '[')) {
            field ignored{};
            if (!parse_field_options(ignored, level::enum_value)) return false;
        }
        if (!expect_symbol(';')) return false;
        e.values.push_back(v);
        return true;
    }

    [[nodiscard]] constexpr bool parse_enum(std::uint32_t parent) {
        const token& kw = peek();
        ++i_;  // 'enum'
        if (depth_ >= max_nesting) return fail(errc::nesting_too_deep, kw);
        enumeration e{};
        const token& name_tok = peek();
        if (!expect_ident(e.name)) return false;
        e.parent = parent;
        e.loc = name_tok.loc;
        if (!expect_symbol('{')) return false;
        for (;;) {
            const token& t = peek();
            if (t.kind == tok::end) return fail(errc::unexpected_end, t);
            if (is_symbol(t, '}')) {
                ++i_;
                break;
            }
            if (is_symbol(t, ';')) {
                ++i_;
            } else if (is_word(t, "option"sv)) {
                if (!parse_option(level::enumeration, &e.allow_alias)) return false;
            } else if (is_word(t, "reserved"sv)) {
                if (!parse_reserved(e.reserved_ranges, e.reserved_names, true)) return false;
            } else if (!parse_enum_value(e)) {
                return false;
            }
        }
        out_.enums.push_back(std::move(e));
        return true;
    }

    [[nodiscard]] constexpr bool parse_rpc_type(std::string_view& name, location& loc) {
        if (!expect_symbol('(')) return false;
        const token& t = peek();
        if (is_word(t, "stream"sv) && (peek(1).kind == tok::ident || is_symbol(peek(1), '.')))
            return fail(errc::unsupported_streaming, t);
        loc = t.loc;
        if (!parse_type_ref(name)) return false;
        return expect_symbol(')');
    }

    [[nodiscard]] constexpr bool parse_rpc(service& s) {
        ++i_;  // 'rpc'
        rpc r{};
        r.loc = peek().loc;
        if (!expect_ident(r.name)) return false;
        if (!parse_rpc_type(r.request_name, r.request_loc)) return false;
        const token& ret = peek();
        if (!is_word(ret, "returns"sv)) return fail(errc::unexpected_token, ret);
        ++i_;
        if (!parse_rpc_type(r.response_name, r.response_loc)) return false;
        if (is_symbol(peek(), '{')) {
            ++i_;
            for (;;) {
                const token& b = peek();
                if (b.kind == tok::end) return fail(errc::unexpected_end, b);
                if (is_symbol(b, '}')) {
                    ++i_;
                    break;
                }
                if (is_symbol(b, ';')) {
                    ++i_;
                } else if (is_word(b, "option"sv)) {
                    if (!parse_option(level::rpc)) return false;
                } else {
                    return fail(errc::unexpected_token, b);
                }
            }
        } else if (!expect_symbol(';')) {
            return false;
        }
        s.rpcs.push_back(std::move(r));
        return true;
    }

    [[nodiscard]] constexpr bool parse_service() {
        ++i_;  // 'service'
        service s{};
        const token& name_tok = peek();
        if (!expect_ident(s.name)) return false;
        s.loc = name_tok.loc;
        if (!expect_symbol('{')) return false;
        s.rpcs.reserve(ntoks_ / 9 + 1);
        for (;;) {
            const token& t = peek();
            if (t.kind == tok::end) return fail(errc::unexpected_end, t);
            if (is_symbol(t, '}')) {
                ++i_;
                break;
            }
            if (is_symbol(t, ';')) {
                ++i_;
            } else if (is_word(t, "option"sv)) {
                if (!parse_option(level::service)) return false;
            } else if (is_word(t, "rpc"sv)) {
                if (!parse_rpc(s)) return false;
            } else {
                return fail(errc::unexpected_token, t);
            }
        }
        out_.services.push_back(std::move(s));
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// Semantic checks

enum class symbol : std::uint8_t { message, enumeration, service, package, field, enum_value, rpc };

// One name in one scope. Scope ids: a message's is its index; package component k's is
// messages.size() + k; then the root; then one per service, which holds its rpc names.
struct name_entry {
    std::string_view name;  // the simple name, viewing the source or file::package
    std::uint32_t scope = 0;
    std::uint32_t hash = 0;
    symbol kind = symbol::message;
    std::uint32_t index = npos;  // into file::messages, enums or services; a package component's scope id
    location loc{};
};

[[nodiscard]] constexpr bool is_type(symbol k) noexcept { return k == symbol::message || k == symbol::enumeration; }
// protoc's aggregates: a dotted name may continue inside them.
[[nodiscard]] constexpr bool is_aggregate(symbol k) noexcept {
    return k == symbol::message || k == symbol::enumeration || k == symbol::service || k == symbol::package;
}

[[nodiscard]] constexpr bool before(location a, location b) noexcept {
    return a.line != b.line ? a.line < b.line : a.column < b.column;
}

[[nodiscard]] constexpr char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
[[nodiscard]] constexpr char upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

// Hashing and equality walk pointers: in constant evaluation every call and every checked index
// is charged, so string_view's operator== (through char_traits) costs several times more.
[[nodiscard]] constexpr std::uint32_t fnv1a(std::uint32_t h, std::string_view s) noexcept {
    const char* p = s.data();
    for (const char* const e = p + s.size(); p != e; ++p) {
        h ^= static_cast<unsigned char>(*p);
        h *= 16777619u;
    }
    return h;
}
inline constexpr std::uint32_t fnv_basis = 2166136261u;

[[nodiscard]] constexpr std::uint32_t scoped_hash(std::uint32_t scope, std::string_view name) noexcept {
    return fnv1a(fnv_basis ^ (scope * 2654435761u), name);
}

// The index of the first field whose name an earlier field already has, or npos. One pass over
// an open-addressing hash set, so the cost stays linear in constant evaluation. Names crafted to
// collide in the hash would make probing quadratic, so a long probe run falls back to a sort.
[[nodiscard]] constexpr std::uint32_t first_duplicate_name(const std::vector<field>& fields,
                                                           std::vector<std::uint32_t>& slots) {
    constexpr std::size_t max_probe = 32;
    std::size_t size = 16;
    while (size < 2 * fields.size()) size *= 2;
    slots.assign(size, 0);
    const std::size_t mask = size - 1;
    const field* const fs = fields.data();
    std::uint32_t* const sl = slots.data();
    const auto count = static_cast<std::uint32_t>(fields.size());
    for (std::uint32_t k = 0; k < count; ++k) {
        std::size_t i = fnv1a(fnv_basis, fs[k].name) & mask;
        std::size_t probes = 0;
        while (sl[i] != 0) {
            if (same(fs[sl[i] - 1].name, fs[k].name)) return k;
            if (++probes > max_probe) {
                std::vector<std::pair<std::string_view, std::uint32_t>> sorted;
                sorted.reserve(fields.size());
                for (std::uint32_t j = 0; j < count; ++j) sorted.emplace_back(fs[j].name, j);
                std::sort(sorted.begin(), sorted.end());
                std::uint32_t first = npos;
                for (std::size_t j = 1; j < sorted.size(); ++j)
                    if (sorted[j].first == sorted[j - 1].first)
                        first = std::min(first, std::max(sorted[j].second, sorted[j - 1].second));
                return first;
            }
            i = (i + 1) & mask;
        }
        sl[i] = k + 1;
    }
    return npos;
}

[[nodiscard]] constexpr bool in_ranges(const std::vector<reserved_range>& rs, std::int64_t v) noexcept {
    for (const auto& r : rs)
        if (v >= r.lo && v <= r.hi) return true;
    return false;
}

[[nodiscard]] constexpr bool reserved(const std::vector<reserved_name>& names, std::string_view n) noexcept {
    for (const auto& x : names)
        if (x.name == n) return true;
    return false;
}

// protoc's default JSON name: each underscore is dropped and the next character capitalized.
[[nodiscard]] constexpr std::string json_name(std::string_view n) {
    std::string out;
    bool up = false;
    for (const char c : n) {
        if (c == '_') {
            up = true;
            continue;
        }
        out.push_back(up ? upper(c) : c);
        up = false;
    }
    return out;
}

// protoc's proto3 enum check: remove the enum name as a prefix (ignoring case and underscores)
// when something is left, then convert to PascalCase.
[[nodiscard]] constexpr std::string enum_value_key(std::string_view enum_name, std::string_view value) {
    std::size_t i = 0;
    std::size_t j = 0;
    bool strip = true;
    for (; i < value.size() && j < enum_name.size(); ++i) {
        if (value[i] == '_') continue;
        while (j < enum_name.size() && enum_name[j] == '_') ++j;
        if (j == enum_name.size()) break;
        if (lower(value[i]) != lower(enum_name[j++])) {
            strip = false;
            break;
        }
    }
    while (j < enum_name.size() && enum_name[j] == '_') ++j;
    if (j < enum_name.size()) strip = false;
    while (strip && i < value.size() && value[i] == '_') ++i;
    if (!strip || i == value.size()) i = 0;
    std::string out;
    bool up = true;
    for (; i < value.size(); ++i) {
        if (value[i] == '_') {
            up = true;
            continue;
        }
        out.push_back(up ? upper(value[i]) : lower(value[i]));
        up = false;
    }
    return out;
}

class checker {
public:
    constexpr explicit checker(file& f) : f_(f) {}

    [[nodiscard]] constexpr bool run() {
        if (!check_messages() || !check_enums()) return false;
        build_full_names();
        if (!build_names()) return false;
        return resolve() && check_packed() && order_messages();
    }

    [[nodiscard]] constexpr const error& failure() const noexcept { return err_; }

private:
    file& f_;
    error err_{};
    fixed_buffer<name_entry> names_;    // every name of every scope, in one hash table
    std::vector<std::uint32_t> slots_;  // open addressing: an index into names_ plus one; 0 is empty
    std::uint32_t mask_ = 0;
    std::uint32_t root_ = 0;            // scope ids, see name_entry
    std::uint32_t file_scope_ = 0;      // where top-level definitions live

    constexpr bool fail(errc code, location loc, std::string_view token) {
        err_ = error{code, loc, token};
        return false;
    }

    [[nodiscard]] constexpr bool check_reserved(const std::vector<reserved_range>& ranges,
                                                const std::vector<reserved_name>& names) {
        for (std::size_t b = 1; b < ranges.size(); ++b)
            for (std::size_t a = 0; a < b; ++a)
                if (ranges[a].lo <= ranges[b].hi && ranges[b].lo <= ranges[a].hi)
                    return fail(errc::reserved_overlap, ranges[b].loc, ranges[b].text);
        for (std::size_t b = 1; b < names.size(); ++b)
            for (std::size_t a = 0; a < b; ++a)
                if (names[a].name == names[b].name)
                    return fail(errc::duplicate_reserved_name, names[b].loc, names[b].name);
        return true;
    }

    [[nodiscard]] constexpr bool check_json_names(const message& m) {
        const field* const fs = m.fields.data();
        const field* const fe = fs + m.fields.size();
        bool any_underscore = false;
        for (const field* f = fs; f != fe && !any_underscore; ++f) {
            const char* c = f->name.data();
            for (const char* const e = c + f->name.size(); c != e; ++c) any_underscore = any_underscore || *c == '_';
        }
        if (!any_underscore) return true;  // then every JSON name is the field name, already unique
        std::vector<std::pair<std::string, std::uint32_t>> json;
        json.reserve(m.fields.size());
        for (std::uint32_t k = 0; k < m.fields.size(); ++k) json.emplace_back(json_name(fs[k].name), k);
        std::sort(json.begin(), json.end());
        for (std::size_t k = 1; k < json.size(); ++k) {
            if (json[k].first == json[k - 1].first) {
                const field& fd = fs[std::max(json[k].second, json[k - 1].second)];
                return fail(errc::json_name_conflict, fd.loc, fd.name);
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool check_message(const message& m,
                                               std::vector<std::pair<std::uint32_t, std::uint32_t>>& numbers,
                                               std::vector<std::uint32_t>& slots) {
        const field* const fs = m.fields.data();
        const field* const fe = fs + m.fields.size();
        const bool any_reserved = !m.reserved_ranges.empty() || !m.reserved_names.empty();
        // Field numbers usually increase in declaration order, and then none repeats.
        bool increasing = true;
        std::uint32_t previous = 0;
        for (const field* f = fs; f != fe; ++f) {
            if (f->number >= first_implementation_number && f->number <= last_implementation_number)
                return fail(errc::field_number_in_implementation_range, f->number_loc, f->number_text);
            if (any_reserved) {
                if (in_ranges(m.reserved_ranges, f->number))
                    return fail(errc::field_number_reserved, f->number_loc, f->number_text);
                if (reserved(m.reserved_names, f->name)) return fail(errc::field_name_reserved, f->loc, f->name);
            }
            increasing = increasing && f->number > previous;
            previous = f->number;
        }
        if (!increasing) {
            numbers.clear();
            for (std::uint32_t k = 0; k < m.fields.size(); ++k) numbers.emplace_back(fs[k].number, k);
            std::sort(numbers.begin(), numbers.end());
            for (std::size_t k = 1; k < numbers.size(); ++k) {
                if (numbers[k].first == numbers[k - 1].first) {
                    const field& fd = fs[std::max(numbers[k].second, numbers[k - 1].second)];
                    return fail(errc::duplicate_field_number, fd.number_loc, fd.number_text);
                }
            }
        }
        if (const std::uint32_t later = first_duplicate_name(m.fields, slots); later != npos) {
            const field& fd = fs[later];
            return fail(errc::duplicate_field_name, fd.loc, fd.name);
        }
        return check_reserved(m.reserved_ranges, m.reserved_names) && check_json_names(m);
    }

    [[nodiscard]] constexpr bool check_messages() {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> numbers;
        std::vector<std::uint32_t> slots;
        for (const auto& m : f_.messages)
            if (!check_message(m, numbers, slots)) return false;
        return true;
    }

    [[nodiscard]] constexpr bool check_enum(const enumeration& e) {
        if (e.values.empty()) return fail(errc::empty_enum, e.loc, e.name);
        if (e.values.front().number != 0)
            return fail(errc::enum_first_value_not_zero, e.values.front().loc, e.values.front().name);
        bool aliased = false;
        for (std::size_t a = 0; a < e.values.size(); ++a) {
            const enum_value& v = e.values[a];
            if (in_ranges(e.reserved_ranges, v.number)) return fail(errc::field_number_reserved, v.loc, v.name);
            if (reserved(e.reserved_names, v.name)) return fail(errc::field_name_reserved, v.loc, v.name);
            for (std::size_t b = 0; b < a; ++b) {
                if (e.values[b].name == v.name) return fail(errc::duplicate_enum_value_name, v.loc, v.name);
                if (e.values[b].number == v.number) {
                    if (!e.allow_alias) return fail(errc::enum_value_alias, v.loc, v.name);
                    aliased = true;
                }
            }
        }
        if (e.allow_alias && !aliased) return fail(errc::allow_alias_unused, e.loc, e.name);
        if (!check_reserved(e.reserved_ranges, e.reserved_names)) return false;
        // Values that collide once the prefix and case are ignored, unless they are aliases.
        std::vector<std::pair<std::string, std::uint32_t>> keys;
        keys.reserve(e.values.size());
        for (std::uint32_t k = 0; k < e.values.size(); ++k)
            keys.emplace_back(enum_value_key(e.name, e.values[k].name), k);
        std::sort(keys.begin(), keys.end());
        for (std::size_t k = 1; k < keys.size(); ++k) {
            if (keys[k].first != keys[k - 1].first) continue;
            const enum_value& x = e.values[keys[k - 1].second];
            const enum_value& y = e.values[keys[k].second];
            if (x.number == y.number) continue;
            const enum_value& later = keys[k].second > keys[k - 1].second ? y : x;
            return fail(errc::enum_value_name_conflict, later.loc, later.name);
        }
        return true;
    }

    [[nodiscard]] constexpr bool check_enums() {
        for (const auto& e : f_.enums)
            if (!check_enum(e)) return false;
        return true;
    }

    // Full names, built once into one buffer: a message's or an enum's is its parent message's
    // (or the package's) plus a dot and its name; a service's is the package's plus its name.
    // Parents come before their children in the message list.
    constexpr void build_full_names() {
        const std::string_view pkg = f_.package;
        const std::size_t nm = f_.messages.size();
        const std::size_t ne = f_.enums.size();
        const std::size_t ns = f_.services.size();
        std::vector<std::uint32_t> off(nm + ne + ns, 0);
        std::vector<std::uint32_t> len(nm + ne + ns, 0);
        const auto base_len = [&](std::uint32_t parent) -> std::size_t {
            return parent == npos ? pkg.size() : len[parent];
        };
        std::size_t total = 0;
        const auto plan = [&](std::size_t slot, std::uint32_t parent, std::string_view name) {
            const std::size_t base = base_len(parent);
            off[slot] = static_cast<std::uint32_t>(total);
            len[slot] = static_cast<std::uint32_t>((base == 0 ? 0 : base + 1) + name.size());
            total += len[slot];
        };
        for (std::size_t i = 0; i < nm; ++i) plan(i, f_.messages[i].parent, f_.messages[i].name);
        for (std::size_t i = 0; i < ne; ++i) plan(nm + i, f_.enums[i].parent, f_.enums[i].name);
        for (std::size_t i = 0; i < ns; ++i) plan(nm + ne + i, npos, f_.services[i].name);
        f_.name_buffer.resize(total);
        char* const data = f_.name_buffer.data();
        const auto fill = [&](std::size_t slot, std::uint32_t parent, std::string_view name) {
            char* p = data + off[slot];
            const char* base = parent == npos ? pkg.data() : data + off[parent];
            const std::size_t n = base_len(parent);
            for (std::size_t k = 0; k < n; ++k) *p++ = base[k];
            if (n != 0) *p++ = '.';
            for (const char c : name) *p++ = c;
            return std::string_view(data + off[slot], len[slot]);
        };
        for (std::size_t i = 0; i < nm; ++i) f_.messages[i].full_name = fill(i, f_.messages[i].parent, f_.messages[i].name);
        for (std::size_t i = 0; i < ne; ++i) f_.enums[i].full_name = fill(nm + i, f_.enums[i].parent, f_.enums[i].name);
        for (std::size_t i = 0; i < ns; ++i) f_.services[i].full_name = fill(nm + ne + i, npos, f_.services[i].name);
    }

    // C++-like scoping, as protoc does it. Every name goes into one table keyed by (scope, simple
    // name): package components, messages, enums and enum values (a value lives beside its enum),
    // services, rpcs (one scope per service), and the fields of messages that also hold nested
    // types (duplicate field names were checked already). A name defined twice in a scope is
    // reported at its second definition; with several, at the earliest such point in the source.
    // Names crafted to collide in the hash only lengthen probe runs, and the constant-evaluation
    // limit bounds those.
    [[nodiscard]] constexpr bool build_names() {
        const auto nm = static_cast<std::uint32_t>(f_.messages.size());
        const std::string_view pkg = f_.package;
        const char* const pkg_data = pkg.data();
        std::uint32_t parts = 0;
        if (!pkg.empty()) {
            parts = 1;
            for (std::size_t k = 0; k < pkg.size(); ++k) parts += pkg_data[k] == '.' ? 1u : 0u;
        }
        root_ = nm + parts;
        file_scope_ = parts == 0 ? root_ : root_ - 1;
        const message* const ms = f_.messages.data();
        std::vector<std::uint8_t> crowded(nm, 0);
        std::uint8_t* const crowd = crowded.data();
        std::size_t count = parts + nm;
        for (std::uint32_t i = 0; i < nm; ++i)
            if (ms[i].parent != npos) crowd[ms[i].parent] = 1;
        for (const enumeration& e : f_.enums) {
            count += 1 + e.values.size();
            if (e.parent != npos) crowd[e.parent] = 1;
        }
        for (std::uint32_t i = 0; i < nm; ++i)
            if (crowd[i] != 0) count += ms[i].fields.size();
        for (const service& sv : f_.services) count += 1 + sv.rpcs.size();

        names_ = fixed_buffer<name_entry>(count);
        const auto add = [&](std::string_view name, std::uint32_t scope, symbol kind, std::uint32_t index,
                             location loc) { names_.push_back({name, scope, scoped_hash(scope, name), kind, index, loc}); };
        const auto scope_of = [&](std::uint32_t parent) { return parent == npos ? file_scope_ : parent; };
        for (std::uint32_t k = 0, start = 0; k < parts; ++k) {
            std::uint32_t end = start;
            while (end < pkg.size() && pkg_data[end] != '.') ++end;
            add(std::string_view(pkg_data + start, end - start), k == 0 ? root_ : nm + k - 1, symbol::package, nm + k,
                {});
            start = end + 1;
        }
        for (std::uint32_t i = 0; i < nm; ++i) add(ms[i].name, scope_of(ms[i].parent), symbol::message, i, ms[i].loc);
        for (std::uint32_t i = 0; i < f_.enums.size(); ++i) {
            const enumeration& e = f_.enums.data()[i];
            add(e.name, scope_of(e.parent), symbol::enumeration, i, e.loc);
            for (const enum_value& v : e.values) add(v.name, scope_of(e.parent), symbol::enum_value, npos, v.loc);
        }
        for (std::uint32_t i = 0; i < nm; ++i)
            if (crowd[i] != 0)
                for (const field& fd : ms[i].fields) add(fd.name, i, symbol::field, npos, fd.loc);
        for (std::uint32_t i = 0; i < f_.services.size(); ++i) {
            const service& sv = f_.services.data()[i];
            add(sv.name, file_scope_, symbol::service, i, sv.loc);
            for (const rpc& r : sv.rpcs) add(r.name, root_ + 1 + i, symbol::rpc, npos, r.loc);
        }

        std::uint32_t size = 16;
        while (size < 2 * names_.size()) size *= 2;
        slots_.assign(size, 0);
        mask_ = size - 1;
        std::uint32_t* const slots = slots_.data();
        const name_entry* const names = names_.data();
        const name_entry* second = nullptr;  // the earliest second definition
        const auto total = static_cast<std::uint32_t>(names_.size());
        for (std::uint32_t k = 0; k < total; ++k) {
            const name_entry& n = names[k];
            std::uint32_t i = n.hash & mask_;
            while (slots[i] != 0) {
                const name_entry& o = names[slots[i] - 1];
                if (o.hash == n.hash && o.scope == n.scope && same(o.name, n.name)) break;
                i = (i + 1) & mask_;
            }
            if (slots[i] == 0) {
                slots[i] = k + 1;
                continue;
            }
            // The table keeps the earliest definition; the later one of the pair is a second one.
            const name_entry& o = names[slots[i] - 1];
            const bool new_first = before(n.loc, o.loc);
            const name_entry& later = new_first ? o : n;
            if (new_first) slots[i] = k + 1;
            if (second == nullptr || before(later.loc, second->loc)) second = &later;
        }
        if (second != nullptr) return fail(errc::duplicate_definition, second->loc, second->name);
        return true;
    }

    [[nodiscard]] constexpr const name_entry* find(std::uint32_t scope, std::string_view name) const noexcept {
        const std::uint32_t h = scoped_hash(scope, name);
        const std::uint32_t* const slots = slots_.data();
        const name_entry* const names = names_.data();
        for (std::uint32_t i = h & mask_;; i = (i + 1) & mask_) {
            const std::uint32_t s = slots[i];
            if (s == 0) return nullptr;
            const name_entry& e = names[s - 1];
            if (e.hash == h && e.scope == scope && same(e.name, name)) return &e;
        }
    }

    [[nodiscard]] constexpr std::uint32_t parent_scope(std::uint32_t s) const noexcept {
        const auto nm = static_cast<std::uint32_t>(f_.messages.size());
        if (s < nm) {
            const std::uint32_t p = f_.messages.data()[s].parent;
            return p == npos ? file_scope_ : p;
        }
        if (s < root_) return s == nm ? root_ : s - 1;
        return npos;
    }

    // The scope that holds a symbol's members, or npos when it holds none (an enum's values live
    // beside it).
    [[nodiscard]] constexpr std::uint32_t inner_scope(const name_entry& e) const noexcept {
        if (e.kind == symbol::message || e.kind == symbol::package) return e.index;
        if (e.kind == symbol::service) return root_ + 1 + e.index;
        return npos;
    }

    // protoc's rule. A name with a leading dot is fully qualified. Otherwise its first component
    // is searched from the innermost scope outward: for a simple name the first type found wins
    // (other symbols are skipped); for a dotted name the first aggregate found decides, and the
    // rest must exist inside it.
    [[nodiscard]] constexpr const name_entry* resolve_name(std::uint32_t scope, std::string_view name) const noexcept {
        const char* p = name.data();
        const char* const end = p + name.size();
        const bool absolute = *p == '.';
        if (absolute) ++p;
        const char* dot = p;
        while (dot != end && *dot != '.') ++dot;
        const std::string_view first(p, static_cast<std::size_t>(dot - p));
        const name_entry* e = nullptr;
        if (absolute) {
            e = find(root_, first);
        } else {
            for (std::uint32_t s = scope; s != npos; s = parent_scope(s)) {
                const name_entry* c = find(s, first);
                if (c != nullptr && (dot == end ? is_type(c->kind) : is_aggregate(c->kind))) {
                    e = c;
                    break;
                }
            }
        }
        while (e != nullptr && dot != end) {
            p = dot + 1;
            dot = p;
            while (dot != end && *dot != '.') ++dot;
            const std::uint32_t inner = inner_scope(*e);
            e = inner == npos ? nullptr : find(inner, std::string_view(p, static_cast<std::size_t>(dot - p)));
        }
        return e != nullptr && is_type(e->kind) ? e : nullptr;
    }

    [[nodiscard]] constexpr bool resolve() {
        message* const ms = f_.messages.data();
        const auto nm = static_cast<std::uint32_t>(f_.messages.size());
        for (std::uint32_t i = 0; i < nm; ++i) {
            field* const fe = ms[i].fields.data() + ms[i].fields.size();
            for (field* fd = ms[i].fields.data(); fd != fe; ++fd) {
                if (fd->type_name.empty()) continue;
                const name_entry* e = resolve_name(i, fd->type_name);
                if (e == nullptr) return fail(errc::unknown_type, fd->type_loc, fd->type_text);
                fd->kind = e->kind == symbol::message ? field_kind::message : field_kind::enumeration;
                fd->type_index = e->index;
            }
        }
        for (service& sv : f_.services) {
            rpc* const re = sv.rpcs.data() + sv.rpcs.size();
            for (rpc* r = sv.rpcs.data(); r != re; ++r) {
                const name_entry* q = resolve_name(file_scope_, r->request_name);
                if (q == nullptr) return fail(errc::unknown_type, r->request_loc, r->name);
                if (q->kind != symbol::message) return fail(errc::not_a_message_type, r->request_loc, r->name);
                const name_entry* a = resolve_name(file_scope_, r->response_name);
                if (a == nullptr) return fail(errc::unknown_type, r->response_loc, r->name);
                if (a->kind != symbol::message) return fail(errc::not_a_message_type, r->response_loc, r->name);
                r->request = q->index;
                r->response = a->index;
            }
        }
        return true;
    }

    // An explicit [packed] is only valid on a repeated numeric, bool or enum field.
    [[nodiscard]] constexpr bool check_packed() {
        for (message& m : f_.messages) {
            field* const fe = m.fields.data() + m.fields.size();
            for (field* fd = m.fields.data(); fd != fe; ++fd) {
                if (!fd->repeated) {
                    if (fd->packed_option != -1) return fail(errc::packed_on_non_packable, fd->loc, fd->name);
                    continue;
                }
                const bool packable =
                    fd->kind == field_kind::enumeration || (fd->kind == field_kind::scalar && is_packable(fd->type));
                if (fd->packed_option != -1 && !packable) return fail(errc::packed_on_non_packable, fd->loc, fd->name);
                fd->packed = packable && fd->packed_option != 0;
            }
        }
        return true;
    }

    // Depth-first post-order over message-typed fields: dependencies come first. A field that
    // reaches a message still on the stack closes a cycle.
    [[nodiscard]] constexpr bool order_messages() {
        const std::size_t n = f_.messages.size();
        const message* const ms = f_.messages.data();
        std::vector<std::uint8_t> state(n, 0);  // 0 new, 1 on stack, 2 done
        std::uint8_t* const st = state.data();
        struct frame {
            std::uint32_t msg;
            const field* next;
            const field* end;
        };
        fixed_buffer<frame> stack(n);  // a message enters at most once
        const auto enter = [&](std::uint32_t m) {
            st[m] = 1;
            const field* const fs = ms[m].fields.data();
            stack.push_back({m, fs, fs + ms[m].fields.size()});
        };
        f_.definition_order.resize(n);
        std::uint32_t* order = f_.definition_order.data();
        for (std::uint32_t root = 0; root < n; ++root) {
            if (st[root] != 0) continue;
            enter(root);
            while (stack.size() != 0) {
                frame& top = stack.data()[stack.size() - 1];
                while (top.next != top.end && top.next->kind != field_kind::message) ++top.next;
                if (top.next == top.end) {
                    st[top.msg] = 2;
                    *order++ = top.msg;
                    stack.truncate(stack.size() - 1);
                    continue;
                }
                const field& fd = *top.next++;
                if (st[fd.type_index] == 1) return fail(errc::recursive_message, fd.type_loc, fd.type_text);
                if (st[fd.type_index] == 0) enter(fd.type_index);
            }
        }
        return true;
    }
};

}  // namespace detail

// Parses and checks a proto3 schema. On success every type reference is resolved and
// `definition_order` lists messages with their dependencies first.
[[nodiscard]] constexpr std::expected<file, error> parse(std::string_view source) {
    auto toks = tokenize(source);
    if (!toks) return std::unexpected(toks.error());
    detail::parser p(std::move(*toks));
    if (!p.parse_file()) return std::unexpected(p.failure());
    file f = std::move(p.result());
    detail::checker c(f);
    if (!c.run()) return std::unexpected(c.failure());
    return f;
}

}  // namespace tep::schema
